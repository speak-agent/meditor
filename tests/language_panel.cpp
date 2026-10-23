// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.document;
import meditor.platform;
import meditor.ui.workbench;
import meditor.test.fixture;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Language panel contract failed at line " + std::to_string(at.line()));
}
void write(const std::filesystem::path& path, std::string_view text) { std::ofstream file(path, std::ios::binary); file << text; }
std::string read(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}; }
}
int main(int argc, char** argv) {
    if (argc > 1) return meditor::testing::run_tool(argc, argv);
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-language-ui-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
#if defined(_WIN32)
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    const auto fixture = meditor::executable_directory() / std::filesystem::path(argv[0]).filename();
    require(std::filesystem::is_regular_file(fixture));
    const auto tools = root / "tools";
    for (const auto* name : {"mcpp", "mcppls"}) {
        const auto bin = tools / name / "bin"; std::filesystem::create_directories(bin);
        std::filesystem::copy_file(fixture, bin / (name + suffix));
        for (const auto& item : std::filesystem::directory_iterator(fixture.parent_path()))
            if (item.path().extension() == ".dll") std::filesystem::copy_file(item.path(), bin / item.path().filename());
    }
    const auto project = root / "project", settings = root / "settings.json";
    std::filesystem::create_directories(project);
    const std::string original = "\xef\xbb\xbf" "export module sample;\r\nexport int value() { return 42; }\r\n";
    std::vector<std::string> arguments {"meditor", "--workspace", project.string(), "--open", "main.cppm", "--tool-root", tools.string(), "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    const auto key = [](std::string value) { return UiSelector::Key(std::move(value)); };
    for (const auto* language : {"en", "zh"}) for (const auto viewport : {Size{390, 844}, Size{1200, 800}}) {
        write(project / "main.cppm", original); write(project / ".meditor-fixture-lsp-mode", "gui-symbols");
        write(settings, meditor::Json({{"schemaVersion", 1}, {"revision", 1}, {"values", {{"theme", "dark"}, {"reducedMotion", true}, {"language", language}}}}).dump());
        Application app(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(app, {.viewport = viewport}); ui.Pump();
        if (viewport.width < 600) ui.Find(key("explorer-toggle")).Tap();
        ui.Find(key("action-outline")).Tap();
        require(ui.Find(key("language-panel")).Exists() && !ui.Find(key("language-row-0")).Exists());
        ui.Find(key("action-trust")).Tap(); ui.Find(key("language-refresh")).Tap();
        auto wait = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (ui.Find(key("language-cancel")).One().enabled && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100));
            }
            require(!ui.Find(key("language-cancel")).One().enabled);
        };
        wait(); require(ui.Find(key("language-row-0")).One().enabled);
        require(ui.Find(key("language-panel")).One().in_viewport && ui.Find(key("language-row-0")).One().in_viewport);
        ui.Find(key("language-row-0")).Tap();
        require(ui.Find(key("code-editor")).One().focused && ui.ActiveTextInput().has_value());
        require(ui.ActiveTextInput()->selection == TextSelection{33, 38});
        ui.Find(key("code-editor")).EnterText("other");
        require(!ui.Find(key("language-row-0")).One().enabled && read(project / "main.cppm") == original);
        ui.PressKey(Key::Z, {.control = true});
        ui.Find(key("action-problems")).Tap(); wait();
        require(ui.Find(key("language-row-0")).One().enabled && !ui.Find(key("language-unverified")).Exists());
        ui.Find(key("language-row-0")).Tap(); require(ui.Find(key("code-editor")).One().focused);
        ui.Find(key("language-close")).Tap(); require(!ui.Find(key("language-panel")).Exists());
        require(ui.Find(key("code-editor")).One().focused);
        ui.PressKey(Key::O, {.control = true, .shift = true}); wait();
        require(ui.Find(key("language-row-0")).One().enabled);
        ui.PressKey(Key::Escape); require(!ui.Find(key("language-panel")).Exists() && ui.Find(key("code-editor")).One().focused);
        ui.PressKey(Key::S, {.control = true}); require(read(project / "main.cppm") == original);
    }
    // Real delayed protocol replies exercise cancellation and continued editing, without injecting UI state.
    for (const auto* mode : {"gui-slow-symbols", "gui-slow-edit", "gui-unversioned", "gui-invalid-symbols", "gui-many-symbols", "empty-symbols"}) {
        const bool slow = mode == std::string_view("gui-slow-symbols") || mode == std::string_view("gui-slow-edit");
        write(project / "main.cppm", original); write(project / ".meditor-fixture-lsp-mode", mode);
        std::filesystem::remove(project / ".meditor-fixture-symbol-requested");
        Application app(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(app, {.viewport = {1200, 800}}); ui.Pump();
        ui.Find(key("action-trust")).Tap();
        ui.Find(key(mode == std::string_view("gui-unversioned") ? "action-problems" : "action-outline")).Tap();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        if (slow) {
            while (!std::filesystem::exists(project / ".meditor-fixture-symbol-requested") && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100));
            }
            require(std::filesystem::exists(project / ".meditor-fixture-symbol-requested") && ui.Find(key("language-cancel")).One().enabled);
            if (mode == std::string_view("gui-slow-symbols")) ui.Find(key("language-cancel")).Tap();
            ui.Find(key("code-editor")).Tap(); ui.PressKey(Key::End, {.control = true}); ui.Find(key("code-editor")).EnterText("// edited\n");
        }
        const auto settle = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline &&
            (ui.Find(key("language-cancel")).One().enabled || (slow && std::chrono::steady_clock::now() < settle))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100));
        }
        require(!ui.Find(key("language-cancel")).One().enabled);
        if (mode == std::string_view("gui-unversioned")) require(ui.Find(key("language-unverified")).Exists() && !ui.Find(key("language-row-0")).One().enabled);
        else if (slow) require(!ui.Find(key("language-row-0")).Exists() && ui.Find(key("code-editor")).One().focused);
        else if (mode == std::string_view("gui-many-symbols")) require(ui.Find(key("language-truncated")).Exists() && ui.Find(key("language-row-199")).Exists() && !ui.Find(key("language-row-200")).Exists());
        else {
            require(!ui.Find(key("language-row-0")).Exists());
            const meditor::i18n::Localizer messages("zh");
            require(ui.Find(key("language-state")).One().text == messages.text(mode == std::string_view("gui-invalid-symbols") ? "language.invalidResult" : "language.empty"));
        }
        require(read(project / "main.cppm") == original);
    }
    std::println("Language panels: native fixture protocol, en/zh desktop/phone layouts, navigation focus, stale results, cancellation and unversioned diagnostics passed");
}

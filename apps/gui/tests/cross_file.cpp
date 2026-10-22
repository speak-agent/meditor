// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.document;
import meditor.platform;
import meditor.test.fixture;
import meditor.ui.workbench;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Cross-file navigation contract failed at line " + std::to_string(at.line()));
}
}
int main(int argc, char** argv) {
    if (argc > 1) return meditor::testing::run_tool(argc, argv);
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-cross-file-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const auto fixture = meditor::executable_directory() / std::filesystem::path(argv[0]).filename();
    require(std::filesystem::is_regular_file(fixture));
    const auto project = root / "project", tools = root / "tools", settings = root / "settings.json";
    std::filesystem::create_directories(project);
    for (const auto* name : {"mcpp", "mcppls"}) {
        const auto bin = tools / name / "bin"; std::filesystem::create_directories(bin);
#if defined(_WIN32)
        const auto executable = std::string(name) + ".exe";
#else
        const auto executable = std::string(name);
#endif
        std::filesystem::copy_file(fixture, bin / executable);
        for (const auto& item : std::filesystem::directory_iterator(fixture.parent_path()))
            if (item.path().extension() == ".dll") std::filesystem::copy_file(item.path(), bin / item.path().filename());
    }
    const std::string source = "\xef\xbb\xbf" "export module sample;\r\nexport int value() { return 42; }\r\n";
    const std::string target = "export module target;\nexport int value();\n";
    std::vector<std::string> arguments {"meditor-gui", "--workspace", project.string(), "--open", "main.cppm", "--tool-root", tools.string(), "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    const auto key = [](std::string value) { return UiSelector::Key(std::move(value)); };
    for (const auto* language : {"en", "zh"}) for (const auto viewport : {Size{390, 844}, Size{1200, 800}}) {
        meditor::write_file_atomic(project / "main.cppm", source); meditor::write_file_atomic(project / "target.cppm", target);
        meditor::write_file_atomic(project / ".meditor-fixture-lsp-mode", "gui-nav");
        meditor::write_file_atomic(settings, meditor::Json({{"schemaVersion", 1}, {"revision", 1}, {"values", {{"theme", "dark"}, {"reducedMotion", true}, {"language", language}}}}).dump());
        Application app(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(app, {.viewport = viewport}); ui.Pump();
        auto painted = [&](std::string_view text) { return ui.CaptureSnapshot().ToString().find(text) != std::string::npos; };
        if (viewport.width < 600) ui.Find(key("explorer-toggle")).Tap();
        ui.Find(key("action-trust")).Tap(); ui.Find(key("code-editor")).Tap(); ui.PressKey(Key::F12);
        auto wait = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (ui.Find(key("language-cancel")).One().enabled && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100));
            }
            require(!ui.Find(key("language-cancel")).One().enabled && ui.Find(key("language-row-0")).One().enabled);
        };
        wait(); ui.Find(key("language-row-0")).Tap(); ui.Pump();
        require(ui.Find(key("code-editor")).One().focused && ui.ActiveTextInput().has_value());
        require(painted("export module target;") && ui.ActiveTextInput()->selection == TextSelection{33, 38});
        ui.PressKey(Key::Escape);
        if (viewport.width < 600) ui.Find(key("explorer-toggle")).Tap();
        ui.Find(key("main.cppm")).Tap();
        if (viewport.width < 600) ui.Find(key("explorer-toggle")).Tap();
        ui.Find(key("code-editor")).Tap(); ui.PressKey(Key::F12, {.shift = true}); wait();
        require(ui.Find(key("language-row-1")).One().enabled && !ui.Find(key("language-row-2")).One().enabled);
        ui.Find(key("language-row-0")).Tap(); ui.Pump();
        require(ui.Find(key("code-editor")).One().focused && painted("export module target;"));
        require(ui.Find(key("language-row-1")).One().enabled);
        ui.Find(key("language-row-1")).Tap(); ui.Pump();
        require(ui.Find(key("code-editor")).One().focused && painted("export module sample;"));
        ui.Find(key("language-row-0")).Tap(); ui.Pump();
        ui.Find(key("code-editor")).EnterText("renam");
        require(!ui.Find(key("language-row-0")).One().enabled && !ui.Find(key("language-row-1")).One().enabled);
        require(meditor::read_file(project / "main.cppm") == source && meditor::read_file(project / "target.cppm") == target);
        ui.PressKey(Key::Z, {.control = true}); ui.PressKey(Key::Escape);
        require(painted("export module target;") && ui.Find(key("code-editor")).One().focused);
    }
    meditor::write_file_atomic(project / "main.cppm", source); meditor::write_file_atomic(project / "target.cppm", target);
    {
        Application app(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(app, {.viewport = {1200, 800}}); ui.Pump();
        ui.Find(key("action-trust")).Tap(); ui.Find(key("code-editor")).Tap(); ui.PressKey(Key::F12);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (ui.Find(key("language-cancel")).One().enabled && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100));
        }
        require(ui.Find(key("language-row-0")).One().enabled);
        meditor::write_file_atomic(project / "target.cppm", "export module target;\nexport int chang();\n");
        ui.Find(key("language-row-0")).Tap();
        require(!ui.Find(key("language-row-0")).Exists());
        require(ui.CaptureSnapshot().ToString().find("export module sample;") != std::string::npos);
        const meditor::i18n::Localizer messages("zh");
        require(ui.Find(key("language-state")).One().text == messages.text("language.stale"));
    }
    std::println("Definition/reference panels navigate across files, restore focus and invalidate changed targets in en/zh phone/desktop layouts: passed");
}

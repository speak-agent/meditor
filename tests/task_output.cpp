// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.platform;
import meditor.test.fixture;
import meditor.ui.workbench;

namespace {
class InstalledResources final : public huxerui::PlatformResources {
public:
    huxerui::ResourceConfiguration Configuration() const override { return {}; }
    std::optional<huxerui::InputStream> OpenRead(std::string_view packagePath) override {
#if defined(__APPLE__)
        const auto root = meditor::executable_directory() / "HuxerUI";
#else
        const auto root = meditor::executable_directory() / "meditor.resources";
#endif
        auto result = huxerui::File(std::u8string_view((root / std::filesystem::path(packagePath)).u8string())).OpenRead();
        if (result.Succeeded()) return std::move(result).Value();
        if (result.Error().code == huxerui::IoErrorCode::NotFound) return std::nullopt;
        throw std::runtime_error("Could not open installed GUI test resources");
    }
};
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Task output contract failed at line " + std::to_string(at.line()));
}
}
int main(int argc, char** argv) {
    if (argc > 1) return meditor::testing::run_tool(argc, argv);
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-task-output-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const auto fixture = meditor::executable_directory() / std::filesystem::path(argv[0]).filename();
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
    const std::string source = "export module sample;\nexport int value() { return 42; }\n";
    meditor::write_file_atomic(project / "main.cppm", source);
    meditor::write_file_atomic(project / ".meditor-fixture-lsp-mode", "gui-symbols");
    meditor::write_file_atomic(project / ".meditor-fixture-task-mode", "stream");
    std::vector<std::string> arguments {"meditor", "--workspace", project.string(), "--open", "main.cppm", "--tool-root", tools.string(), "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    struct Case { std::string language; bool dark, reduced; Size viewport; };
    for (const auto& sample : {Case{"en", true, false, {1200, 800}}, Case{"zh", false, true, {390, 844}}, Case{"zh", true, false, {390, 844}}, Case{"en", false, true, {1200, 800}}}) {
        std::filesystem::remove(project / ".meditor-fixture-task-started"); std::filesystem::remove(project / ".meditor-fixture-task-continue");
        meditor::write_file_atomic(settings, meditor::Json({{"schemaVersion", 1}, {"revision", 1}, {"values", {{"theme", sample.dark ? "dark" : "light"}, {"reducedMotion", sample.reduced}, {"language", sample.language}}}}).dump());
        Application app(meditor::ui::workbench, {.show_debug_overlay = false});
        UiTest ui(app, {.viewport = sample.viewport, .resources = {.locale = Locale::FromLanguageTag(sample.language)}, .resource_provider = std::make_shared<InstalledResources>()}); ui.Pump();
        const auto key = [](std::string value) { return UiSelector::Key(std::move(value)); };
        auto wait = [&](auto predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (!predicate() && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(100)); }
            require(predicate());
        };
        if (sample.viewport.width < 600) ui.Find(key("explorer-toggle")).Tap();
        ui.Find(key("action-trust")).Tap(); ui.Find(key("action-outline")).Tap();
        wait([&] { return !ui.Find(key("language-cancel")).One().enabled; });
        require(ui.Find(key("language-row-0")).One().enabled);
        ui.Find(key("action-build")).Tap();
        require(ui.Find(key("code-editor")).One().focused);
        wait([&] { auto output = ui.Find(key("task-output")).One().text; return output && output->find("fixture: compiling") != output->npos; });
        require(!ui.Find(key("action-build")).One().enabled && ui.Find(key("action-cancel-task")).One().enabled);
        require(ui.Find(key("task-progress")).Exists() && ui.Find(key("output-panel")).One().in_viewport);
        require(!ui.Find(key("language-panel")).Exists());
        ui.Find(key("panel-language")).Tap(); require(ui.Find(key("language-row-0")).One().enabled && !ui.Find(key("output-panel")).Exists());
        ui.Find(key("panel-output")).Tap();
        ui.Find(key("code-editor")).Tap(); ui.PressKey(Key::End, {.control = true}); ui.Find(key("code-editor")).EnterText("// typing\n");
        ui.Pump(std::chrono::milliseconds(200)); auto first = ui.CaptureSnapshot();
        ui.Pump(std::chrono::milliseconds(60)); auto second = ui.CaptureSnapshot();
        if (sample.reduced) require(first == second); else require(first != second);
        require(ui.Find(key("code-editor")).One().focused && meditor::read_file(project / "main.cppm") == source);
        meditor::write_file_atomic(project / ".meditor-fixture-task-continue", "continue");
        wait([&] { return !ui.Find(key("action-cancel-task")).One().enabled; });
        require(!ui.Find(key("task-progress")).Exists() && ui.Find(key("task-output")).One().text->find("fixture: linked") != std::string::npos);
        const meditor::i18n::Localizer messages(sample.language);
        require(ui.Find(key("task-state")).One().text == messages.text("status.task.succeeded"));
        ui.PressKey(Key::Z, {.control = true}); ui.Find(key("output-close")).Tap();
        require(!ui.Find(key("output-panel")).Exists() && ui.Find(key("code-editor")).One().focused);
        ui.Find(key("output-toggle")).Tap(); require(ui.Find(key("output-panel")).Exists());
        std::filesystem::remove(project / ".meditor-fixture-task-started"); std::filesystem::remove(project / ".meditor-fixture-task-continue");
        ui.Find(key("action-build")).Tap(); wait([&] { return std::filesystem::exists(project / ".meditor-fixture-task-started"); });
        ui.PressKey(Key::Escape);
        require(!ui.Find(key("output-panel")).Exists() && ui.Find(key("code-editor")).One().focused);
        require(ui.Find(key("action-cancel-task")).One().enabled);
        ui.Find(key("output-toggle")).Tap(); require(ui.Find(key("task-progress")).Exists());
        ui.Find(key("action-cancel-task")).Tap(); wait([&] { return !ui.Find(key("action-cancel-task")).One().enabled; });
        require(ui.Find(key("task-state")).One().text == messages.text("status.task.cancelled"));
        require(!ui.Find(key("task-progress")).Exists() && meditor::read_file(project / "main.cppm") == source);

        std::filesystem::remove(project / ".meditor-fixture-task-started");
        meditor::write_file_atomic(project / ".meditor-fixture-task-mode", "stream-fail");
        ui.Find(key("action-build")).Tap(); wait([&] { return std::filesystem::exists(project / ".meditor-fixture-task-started"); });
        meditor::write_file_atomic(project / ".meditor-fixture-task-continue", "continue");
        wait([&] { return !ui.Find(key("action-cancel-task")).One().enabled; });
        require(ui.Find(key("task-state")).One().text == messages.text("status.task.failed"));
        require(ui.Find(key("task-output")).One().text->find("fixture: compilation failed") != std::string::npos);
        require(!ui.Find(key("task-progress")).Exists() && ui.Find(key("action-build")).One().enabled);
        require(meditor::read_file(project / "main.cppm") == source);

        meditor::write_file_atomic(project / ".meditor-fixture-task-mode", "silent");
        ui.Find(key("action-build")).Tap(); wait([&] { return !ui.Find(key("action-cancel-task")).One().enabled; });
        require(ui.Find(key("task-state")).One().text == messages.text("status.task.succeeded"));
        require(ui.Find(key("task-output")).One().text == messages.text("task.noOutput"));
        require(!ui.Find(key("task-progress")).Exists());
        ui.Find(key("output-close")).Tap(); require(ui.Find(key("output-toggle")).One().enabled);
        ui.Find(key("output-toggle")).Tap(); require(ui.Find(key("output-panel")).Exists());
        ui.Find(key("panel-language")).Tap(); require(ui.Find(key("panel-output")).One().enabled);
        ui.Find(key("panel-output")).Tap(); require(ui.Find(key("task-output")).One().text == messages.text("task.noOutput"));
        meditor::write_file_atomic(project / ".meditor-fixture-task-mode", "stream");
    }
    std::println("Live process output, real success/failure/cancellation, one bottom panel, retained language results and reduced-motion progress: passed");
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.platform;
import meditor.ui.workbench;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("Localized layout contract failed at line " + std::to_string(location.line()));
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto directory = std::filesystem::temp_directory_path() / ("meditor-localization-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {directory};
    const auto source = directory / "main.cppm", settings = directory / "settings.json";
    const std::string original = "export module demo;\n", suffix = "// 你好🙂\n";
    std::vector<std::string> arguments {"meditor", "--workspace", directory.string(), "--open", "main.cppm", "--settings-file", settings.string(), "--data-root", (directory / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    for (const auto viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}}) {
        meditor::write_file_atomic(source, original);
        meditor::write_file_atomic(settings, R"({"schemaVersion":1,"revision":1,"values":{"theme":"dark","reducedMotion":true,"language":"en"}})");
        Application application(meditor::ui::workbench, {.show_debug_overlay = false});
        UiTest ui(application, {.viewport = viewport, .resources = {.locale = Locale::FromLanguageTag("zh-CN")}});
        ui.Pump();
        require(ui.Find(UiSelector::Key("settings-entry")).One().text == "Settings");
        ui.Find(UiSelector::Key("explorer-toggle")).Tap();
        const auto editor = UiSelector::Key("code-editor");
        ui.Find(editor).Tap();
        ui.PressKey(Key::A, {.control = true});
        ui.Find(editor).EnterText(original + suffix);
        const auto caret = ui.ActiveTextInput()->selection;
        require(read(source) == original);

        ui.Find(UiSelector::Key("settings-entry")).Tap();
        auto panel = ui.Find(UiSelector::Key("settings-panel"));
        panel.ScrollUntil(UiSelector::Key("language-zh"), {.step = {0, 120}}).Tap();
        require(ui.Find(UiSelector::Key("settings-entry")).One().text == "设置");
        require(ui.Find(UiSelector::Key("current-language")).One().text == "当前语言：简体中文");
        meditor::Application preferences(directory, {}, settings, {.dataRoot = directory / "runtime"});
        require(preferences.execute({{"command", "settings.get"}})["data"]["values"]["language"] == "zh");
        for (const auto* key : {"window-title-bar", "status-bar", "settings-entry"}) {
            const auto node = ui.Find(UiSelector::Key(key)).One();
            require(node.in_viewport && node.bounds.x >= 0 && node.bounds.y >= 0);
            require(node.bounds.x + node.bounds.width <= viewport.width + 0.5F);
            require(node.bounds.y + node.bounds.height <= viewport.height + 0.5F);
        }
        ui.PressKey(Key::Escape);
        require(ui.Find(editor).Exists());
        for (int i = 0; i < 32 && !ui.ActiveTextInput(); ++i) ui.PressKey(Key::Tab);
        require(ui.ActiveTextInput().has_value() && ui.ActiveTextInput()->selection == caret);
        require(read(source) == original);
        ui.PressKey(Key::S, {.control = true});
        require(read(source) == original + suffix);
        ui.PressKey(Key::Z, {.control = true}); ui.PressKey(Key::S, {.control = true});
        require(read(source) == original);
        ui.PressKey(Key::Y, {.control = true}); ui.PressKey(Key::S, {.control = true});
        require(read(source) == original + suffix);

        // Auto follows the resource locale; explicit choices have priority.
        ui.Find(UiSelector::Key("settings-entry")).Tap();
        panel.ScrollUntil(UiSelector::Key("language-auto"), {.step = {0, 120}}).Tap();
        require(ui.Find(UiSelector::Key("settings-entry")).One().text == "设置");
        ui.UpdateResourceConfiguration({.locale = Locale::FromLanguageTag("en-GB")}); ui.Pump();
        require(ui.Find(UiSelector::Key("settings-entry")).One().text == "Settings");
        require(read(source) == original + suffix);
    }
    std::println("Localized workspace layouts preserve text, caret and history; automatic language follows resources: passed");
}

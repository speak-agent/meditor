// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.ui.workbench;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) { if (!value) throw std::runtime_error("Find UI contract failed at line " + std::to_string(at.line())); }
void write(const std::filesystem::path& path, std::string_view text) { std::ofstream output(path, std::ios::binary); output << text; }
std::string read(const std::filesystem::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}; }
}
int main() {
    using namespace huxerui; using namespace huxerui::testing;
    auto root = std::filesystem::temp_directory_path() / ("meditor-find-" + std::to_string(std::random_device{}())); std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const std::string original = "\xef\xbb\xbf" "export module demo;\r\n// needle 🙂 needle\r\nexport int needle = 42;\r\n";
    const std::string expected = "\xef\xbb\xbf" "export module demo;\r\n// 结果 🙂 结果\r\nexport int 结果 = 42;\r\n";
    std::vector<std::string> arguments {"meditor-gui", "--workspace", root.string(), "--open", "main.cppm", "--settings-file", (root / "settings.json").string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    for (auto viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}}) for (auto language : {"en", "zh"}) {
        write(root / "main.cppm", original);
        write(root / "settings.json", std::string(R"({"schemaVersion":1,"revision":1,"values":{"theme":")") + (viewport.width == 800 ? "light" : "dark") + R"(","reducedMotion":true,"language":")" + language + R"("}})");
        Application application(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(application, {.viewport = viewport}); ui.Pump();
        ui.Find(UiSelector::Key("explorer-toggle")).Tap(); ui.Find(UiSelector::Key("code-editor")).Tap();
        ui.PressKey(Key::F, {.control = true}); ui.Pump();
        auto panel = ui.Find(UiSelector::Key("find-panel")).One();
        require(panel.in_viewport && panel.bounds.x >= 0 && panel.bounds.x + panel.bounds.width <= viewport.width + 0.5F && panel.bounds.y >= 0 && panel.bounds.y + panel.bounds.height <= viewport.height + 0.5F);
        require(ui.Find(UiSelector::Key("find-query")).One().focused);
        ui.Find(UiSelector::Key("find-query")).EnterText("needle");
        require(ui.Find(UiSelector::Key("find-count")).One().text.value_or("").find('3') != std::string::npos);
        const auto paint = ui.CaptureSnapshot().ToString();
        require(paint.find(viewport.width == 800 ? "red=0.976471" : "red=0.227451") != paint.npos);
        ui.Find(UiSelector::Key("find-previous")).Tap(); require(ui.Find(UiSelector::Key("find-wrapped")).Exists());
        ui.Find(UiSelector::Key("find-replacement")).Tap();
        ui.Find(UiSelector::Key("find-replacement")).EnterText("结果");
        ui.Find(UiSelector::Key("find-replace-all")).Tap();
        require(ui.Find(UiSelector::Key("find-feedback")).One().text.value_or("").find('3') != std::string::npos);
        require(read(root / "main.cppm") == original);
        ui.PressKey(Key::Escape); ui.Pump();
        require(!ui.Find(UiSelector::Key("find-panel")).Exists() && ui.ActiveTextInput().has_value());
        ui.PressKey(Key::S, {.control = true}); require(read(root / "main.cppm") == expected);
        ui.PressKey(Key::Z, {.control = true}); ui.PressKey(Key::S, {.control = true}); require(read(root / "main.cppm") == original);
        ui.PressKey(Key::F, {.control = true}); ui.Pump();
        ui.Find(UiSelector::Key("find-query")).Tap(); ui.PressKey(Key::A, {.control = true}); ui.PressKey(Key::Backspace);
        require(!ui.Find(UiSelector::Key("find-next")).One().enabled && !ui.Find(UiSelector::Key("find-replace-all")).One().enabled);
        ui.Find(UiSelector::Key("find-query")).EnterText(std::string(4097, 'a'));
        require(ui.Find(UiSelector::Key("find-error")).Exists());
        const auto errorPanel = ui.Find(UiSelector::Key("find-panel")).One().bounds; require(errorPanel.y >= 0 && errorPanel.y + errorPanel.height <= viewport.height + 0.5F);
        ui.PressKey(Key::Escape); require(read(root / "main.cppm") == original);
    }
    write(root / "main.cppm", "needle needle");
    write(root / "settings.json", R"({"schemaVersion":1,"revision":1,"values":{"theme":"dark","reducedMotion":true,"language":"en"}})");
    {
        Application application(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(application, {.viewport = {1200, 800}}); ui.Pump();
        ui.Find(UiSelector::Key("code-editor")).Tap(); const auto beforeEditor = ui.Find(UiSelector::Key("code-editor")).One().bounds;
        ui.PressKey(Key::F, {.control = true}); require(ui.Find(UiSelector::Key("code-editor")).One().bounds == beforeEditor);
        ui.Find(UiSelector::Key("find-query")).EnterText("needle");
        ui.Find(UiSelector::Key("find-replacement")).Tap(); ui.Find(UiSelector::Key("find-replacement")).EnterText("prefix_needle");
        ui.Find(UiSelector::Key("find-replace")).Tap(); ui.Find(UiSelector::Key("find-replace")).Tap();
        ui.PressKey(Key::Escape); ui.PressKey(Key::S, {.control = true}); require(read(root / "main.cppm") == "prefix_needle prefix_needle");
    }
    write(root / "main.cppm", "needleabc");
    {
        Application application(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(application, {.viewport = {1200, 800}}); ui.Pump();
        ui.Find(UiSelector::Key("code-editor")).Tap(); ui.PressKey(Key::F, {.control = true});
        ui.Find(UiSelector::Key("find-query")).EnterText("needle");
        ui.Find(UiSelector::Key("find-replacement")).Tap(); ui.Find(UiSelector::Key("find-replacement")).EnterText("abcde🙂");
        ui.Find(UiSelector::Key("find-replace")).Tap(); ui.PressKey(Key::Escape);
        require(ui.ActiveTextInput().has_value() && ui.ActiveTextInput()->selection == TextSelection{7, 7});
        ui.PressKey(Key::S, {.control = true}); require(read(root / "main.cppm") == "abcde🙂abc");
    }
    write(root / "legacy.cppm", "needle\rdata"); arguments[4] = "legacy.cppm";
    pointers.clear(); for (auto& argument : arguments) pointers.push_back(argument.data()); meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    {
        Application application(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(application, {.viewport = {390, 844}}); ui.Pump();
        ui.Find(UiSelector::Key("explorer-toggle")).Tap(); ui.Find(UiSelector::Key("action-find")).Tap();
        ui.Find(UiSelector::Key("find-query")).EnterText("needle");
        require(!ui.Find(UiSelector::Key("find-replace")).One().enabled && !ui.Find(UiSelector::Key("find-replace-all")).One().enabled);
        ui.PressKey(Key::Escape); require(read(root / "legacy.cppm") == "needle\rdata");
    }
    std::println("Find dialog keyboard focus, Unicode replacement, highlights, wrap, atomic undo and localized layouts: passed");
}

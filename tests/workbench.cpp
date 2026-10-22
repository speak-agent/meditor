// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.ui.workbench;
import meditor.application;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("Workbench layout contract failed at line " + std::to_string(location.line()));
}
huxerui::View reduced_workbench() {
    auto theme = huxerui::FlatLightThemeSpec(); theme.motion.reduced_motion = true;
    return huxerui::FlatTheme(theme, meditor::ui::workbench());
}
void contained(const huxerui::testing::UiNodeInfo& node, huxerui::Size viewport) {
    require(node.in_viewport && node.enabled);
    require(node.bounds.x >= 0 && node.bounds.y >= 0);
    require(node.bounds.x + node.bounds.width <= viewport.width + 0.5F);
    require(node.bounds.y + node.bounds.height <= viewport.height + 0.5F);
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    auto directory = std::filesystem::temp_directory_path() / ("meditor-workbench-settings-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {directory};
    auto file = (directory / "settings.json").string();
    std::string executable = "meditor", option = "--settings-file";
    std::string dataOption = "--data-root", dataRoot = (directory / "runtime").string();
    char* arguments[] = {executable.data(), option.data(), file.data(), dataOption.data(), dataRoot.data()};
    meditor::ui::configure(5, arguments);
    Application application(meditor::ui::workbench, {.window = {.chrome_mode = WindowChromeMode::Custom, .caption_labels = {.minimize = "Minimize", .toggle_maximize = "Maximize", .close = "Close"}}, .show_debug_overlay = false});
    for (Size viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}}) {
        UiTest ui(application, {.viewport = viewport});
        contained(ui.Find(UiSelector::Text("Open workspace")).One(), viewport);
        auto title = ui.Find(UiSelector::Key("window-title-bar")).One();
        auto status = ui.Find(UiSelector::Key("status-bar")).One();
        contained(title, viewport); contained(status, viewport);
        contained(ui.Find(UiSelector::Key("settings-entry")).One(), viewport);
        require(title.bounds.y + title.bounds.height <= status.bounds.y);
        contained(ui.Find(UiSelector::Text("Light")).One(), viewport);
        auto dark = ui.CaptureSnapshot();
        ui.Find(UiSelector::Text("Light")).Tap();
        contained(ui.Find(UiSelector::Text("Dark")).One(), viewport);
        require(ui.CaptureSnapshot() != dark);
        ui.Pump(std::chrono::milliseconds(60)); auto middle = ui.CaptureSnapshot();
        contained(ui.Find(UiSelector::Text("Dark")).One(), viewport);
        ui.Pump(std::chrono::milliseconds(60)); auto end = ui.CaptureSnapshot();
        require(middle != end);
        ui.PumpAndSettle({.timeout = std::chrono::milliseconds(500)});
        auto final = ui.CaptureSnapshot();
        ui.Pump(std::chrono::milliseconds(100)); require(ui.CaptureSnapshot() == final);
        ui.Find(UiSelector::Text("Dark")).Tap();
        contained(ui.Find(UiSelector::Text("Light")).One(), viewport);
        ui.SetWindowMetrics({.viewport = {844, 390}}); ui.Pump();
        contained(ui.Find(UiSelector::Text("Open workspace")).One(), {844, 390});
        contained(ui.Find(UiSelector::Key("status-bar")).One(), {844, 390});
        ui.Find(UiSelector::Key("settings-entry")).Tap();
        contained(ui.Find(UiSelector::Key("motion-setting")).One(), {844, 390});
        ui.Find(UiSelector::Key("motion-setting")).Tap();
        meditor::Application settingsReader(directory, {}, directory / "settings.json");
        require(settingsReader.execute({{"command", "settings.get"}})["data"]["values"]["reducedMotion"].get<bool>());
        ui.Find(UiSelector::Text("Light")).Tap();
        auto reducedSnapshot = ui.CaptureSnapshot(); ui.Pump(std::chrono::milliseconds(60));
        require(ui.CaptureSnapshot() == reducedSnapshot);
        ui.Find(UiSelector::Text("Dark")).Tap();
        ui.Find(UiSelector::Key("motion-setting")).Tap();
        ui.Find(UiSelector::Key("settings-panel")).ScrollUntil(UiSelector::Text("Back"), {.step = {0, 120}}).Tap();
        contained(ui.Find(UiSelector::Text("Open workspace")).One(), {844, 390});
    }
    Application reducedApplication(reduced_workbench, {.show_debug_overlay = false});
    UiTest reduced(reducedApplication, {.viewport = {390, 844}});
    reduced.Find(UiSelector::Text("Light")).Tap(); auto settled = reduced.CaptureSnapshot();
    reduced.Pump(std::chrono::milliseconds(60)); require(reduced.CaptureSnapshot() == settled);
    std::println("Windowless workbench layout, theme animation, reduced motion and resize: passed");
}

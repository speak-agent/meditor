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
    if (!value) throw std::runtime_error("About view contract failed at line " + std::to_string(location.line()));
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto directory = std::filesystem::temp_directory_path() / ("meditor-about-ui-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {directory};
    const auto settings = directory / "settings.json";
    std::vector<std::string> arguments {"meditor-gui", "--settings-file", settings.string(), "--data-root", (directory / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    for (const auto* language : {"en", "zh"})
    for (const auto viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}}) {
        const meditor::Json stored {{"schemaVersion", 1}, {"revision", 0}, {"values", {{"theme", "dark"}, {"reducedMotion", true}, {"language", language}}}};
        meditor::write_file_atomic(settings, stored.dump());
        const auto checkpoint = meditor::read_file(settings);
        meditor::Application core(directory, {}, settings, {.dataRoot = directory / "runtime"});
        const auto expected = core.execute({{"command", "about"}}).at("data");
        Application application(meditor::ui::workbench, {.show_debug_overlay = false});
        UiTest ui(application, {.viewport = viewport}); ui.Pump();
        const auto entry = ui.Find(UiSelector::Key("about-entry")).One();
        require(entry.in_viewport && entry.bounds.x >= 0 && entry.bounds.x + entry.bounds.width <= viewport.width + 0.5F);
        require(entry.text == (std::string_view(language) == "zh" ? "关于" : "About"));
        ui.Find(UiSelector::Key("about-entry")).Tap();
        const auto content = ui.Find(UiSelector::Key("about-content")).One().bounds;
        require(content.width <= 800.5F && content.width <= viewport.width + 0.5F && content.x >= 0);
        require(std::abs(content.x * 2 + content.width - viewport.width) <= 1.0F);
        require(ui.Find(UiSelector::Key("about-version")).One().text == "meditor 0.0.1");
        ui.Find(UiSelector::Key("about-panel")).ScrollUntil(UiSelector::Key("license-meditor")).Tap();
        require(ui.Find(UiSelector::Key("license-text")).One().text == expected["product"]["licenseText"].get<std::string>());
        ui.PressKey(Key::Escape); require(ui.Find(UiSelector::Key("about-panel")).Exists());
        ui.Find(UiSelector::Key("about-panel")).ScrollUntil(UiSelector::Key("license-HuxerUI"), {.step = {0, 120}}).Tap();
        require(ui.Find(UiSelector::Key("license-text")).One().text == expected["libraries"][0]["licenseText"].get<std::string>());
        ui.PressKey(Key::Escape);
        ui.Find(UiSelector::Key("about-panel")).ScrollUntil(UiSelector::Key("about-back"), {.step = {0, 120}}).Tap();
        require(ui.Find(UiSelector::Key("workspace-open")).One().in_viewport);
        require(meditor::read_file(settings) == checkpoint);
    }
    std::println("Offline About and project/dependency license views are reachable in en/zh at three sizes: passed");
}

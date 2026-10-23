// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.platform;
import meditor.ui.workbench;
import meditor.test.fixture;

using namespace huxerui;
using namespace huxerui::testing;
namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Preparation contract failed at line " + std::to_string(location.line())); }
}
int main(int argc, char** argv) {
    if (argc > 1) return meditor::testing::run_tool(argc, argv);
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    const auto fixture = meditor::executable_directory() / std::filesystem::path(argv[0]).filename();
    require(std::filesystem::is_regular_file(fixture));
    for (const auto& stale : std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
        if (meditor::path_to_utf8(stale.path().filename()).starts_with("meditor-preparation-"))
            { std::error_code ignored; std::filesystem::remove_all(stale.path(), ignored); } // Terminated runs cannot unwind.
    const auto root = std::filesystem::temp_directory_path() / ("meditor-preparation-" + std::to_string(std::random_device{}()));
    // An EMPTY tool root: doctor reports the tools missing and the banner guides the first run.
    const auto tools = root / "empty-tools";
    for (const auto* name : {"mcpp", "mcppls"}) std::filesystem::create_directories(tools / name / "bin");
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    const auto settings = root / "settings.json";
    std::vector<std::string> arguments {"meditor", "--workspace", project.string(), "--tool-root", tools.string(), "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    Application app(meditor::ui::workbench, {.show_debug_overlay = false});
    UiTest ui(app, {.viewport = {1200, 800}}); ui.Pump();
    const auto key = [](std::string value) { return UiSelector::Key(std::move(value)); };
    // The banner reports the missing tools through doctor's machine-readable state.
    // Pump without querying between pumps: mid-sequence Find() disturbs the harness frame
    // lifecycle here (banner arrives in a late lifecycle frame).
    for (int i = 0; i < 30; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); ui.Pump(std::chrono::milliseconds(100)); }
    require(ui.Find(key("preparation-missing-active")).Exists());
    require(ui.Find(key("action-prepare")).Exists());
    require(ui.Find(key("preparation-missing")).Exists());
    require(ui.Find(key("action-prepare")).Exists());

    // Trust first: environment.prepare executes tools and therefore requires an explicit trust.
    ui.Find(key("action-trust")).Tap();
    for (int i = 0; i < 10; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); ui.Pump(std::chrono::milliseconds(100)); }
    // Provision the fixture tools mid-run; the retry path (doctor after a task) flips to ready.
    for (const auto* name : {"mcpp", "mcppls"})
        std::filesystem::copy_file(fixture, tools / name / "bin" / (name + suffix));
    ui.Find(key("action-prepare")).Tap(); // environment.prepare task; fixture mcpp exits promptly
    for (int i = 0; i < 60; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); ui.Pump(std::chrono::milliseconds(100)); }
    require(ui.Find(key("preparation-ready")).Exists());
    require(!ui.Find(key("action-prepare")).Exists());

    std::println("First-run preparation guidance: banner on missing tools, prepare task and readiness transition: passed");
}

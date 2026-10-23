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
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Semantic token contract failed at line " + std::to_string(location.line())); }
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
        if (meditor::path_to_utf8(stale.path().filename()).starts_with("meditor-semantic-"))
            { std::error_code ignored; std::filesystem::remove_all(stale.path(), ignored); } // Terminated runs cannot unwind.
    const auto root = std::filesystem::temp_directory_path() / ("meditor-semantic-" + std::to_string(std::random_device{}()));
    const auto tools = root / "tools";
    for (const auto* name : {"mcpp", "mcppls"}) {
        const auto bin = tools / name / "bin"; std::filesystem::create_directories(bin);
        std::filesystem::copy_file(fixture, bin / (name + suffix));
    }
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    { std::ofstream out(project / "main.cppm", std::ios::binary); out << "export module sample;\nexport int value() { return 42; }\n"; }
    { std::ofstream out(project / ".meditor-fixture-lsp-mode", std::ios::binary); out << "semantic"; } // no fault branch: default replies cover semanticTokens/range
    const auto settings = root / "settings.json";
    std::vector<std::string> arguments {"meditor", "--workspace", project.string(), "--open", "main.cppm", "--tool-root", tools.string(), "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    Application app(meditor::ui::workbench, {.show_debug_overlay = false});
    UiTest ui(app, {.viewport = {1200, 800}}); ui.Pump();
    const auto key = [](std::string value) { return UiSelector::Key(std::move(value)); };
    auto wait = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate() && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); ui.Pump(std::chrono::milliseconds(60)); }
        require(predicate());
    };

    ui.Find(key("action-trust")).Tap();
    wait([&] { return ui.Find(key("code-editor")).Exists(); });
    // The initial view wants tokens: the fixture overlay lands after the debounced query.
    wait([&] { return ui.Find(key("semantic-tokens-active")).Exists(); });

    // Any edit invalidates the overlay; the debounced re-query serves it again.
    ui.Find(key("code-editor")).Tap();
    require(ui.ActiveTextInput().has_value());
    auto session = ui.ActiveTextInput();
    { TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = "x";
      require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok); }
    // The edit clears the overlay synchronously; the debounced re-query serves it again.
    wait([&] { return !ui.Find(key("semantic-tokens-active")).Exists(); });
    wait([&] { return ui.Find(key("semantic-tokens-active")).Exists(); });

    std::println("Fixture-backed semantic tokens: initial overlay, edit invalidation and re-query: passed");
}

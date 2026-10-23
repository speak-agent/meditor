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
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Completion contract failed at line " + std::to_string(location.line())); }
using meditor::Json;
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
        if (meditor::path_to_utf8(stale.path().filename()).starts_with("meditor-completion-"))
            { std::error_code ignored; std::filesystem::remove_all(stale.path(), ignored); } // Terminated runs cannot unwind.
    const auto root = std::filesystem::temp_directory_path() / ("meditor-completion-" + std::to_string(std::random_device{}()));
    const auto tools = root / "tools";
    for (const auto* name : {"mcpp", "mcppls"}) {
        const auto bin = tools / name / "bin"; std::filesystem::create_directories(bin);
        std::filesystem::copy_file(fixture, bin / (name + suffix));
    }
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    const std::string original = "export module sample;\nexport int value() { return 42; }\n";
    { std::ofstream out(project / "main.cppm", std::ios::binary); out << original; }
    { std::ofstream out(project / ".meditor-fixture-lsp-mode", std::ios::binary); out << "completion"; }
    const auto settings = root / "settings.json";
    auto read = std::string();
    { std::ifstream in(project / "main.cppm", std::ios::binary); std::ostringstream buffer; buffer << in.rdbuf(); read = buffer.str(); }
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
    auto currentText = [&] { std::ifstream in(project / "main.cppm", std::ios::binary); std::ostringstream buffer; buffer << in.rdbuf(); return buffer.str(); };

    ui.Find(key("action-trust")).Tap();
    wait([&] { return ui.Find(key("code-editor")).Exists(); });
    ui.Find(key("code-editor")).Tap();
    require(ui.ActiveTextInput().has_value());
    auto session = ui.ActiveTextInput();
    auto commit = [&](const std::string& text) {
        TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = text;
        require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok);
    };

    // Typing an identifier surfaces the fixture-backed completion list.
    commit("mod");
    wait([&] { return ui.Find(key("completion-active")).Exists(); });
    // Escape dismisses without changing the buffer.
    ui.PressKey(Key::Escape);
    wait([&] { return !ui.Find(key("completion-active")).Exists(); });
    // Explicit trigger works with the same prefix; Enter accepts the fixture item.
    ui.PressKey(Key::Space, {.control = true});
    wait([&] { return ui.Find(key("completion-active")).Exists(); });
    ui.PressKey(Key::Enter);
    wait([&] { return !ui.Find(key("completion-active")).Exists(); });
    ui.PressKey(Key::S, {.control = true}); // Save: dirty accept becomes file content.
    wait([&] { return currentText() == original + "module_value"; });

    std::println("Fixture-backed completion: typing surfaces, escape dismisses, explicit trigger and enter accept, save persists: passed");
}

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
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Hover/signature contract failed at line " + std::to_string(location.line())); }
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
        if (meditor::path_to_utf8(stale.path().filename()).starts_with("meditor-hover-signature-"))
            { std::error_code ignored; std::filesystem::remove_all(stale.path(), ignored); } // Terminated runs cannot unwind.
    const auto root = std::filesystem::temp_directory_path() / ("meditor-hover-signature-" + std::to_string(std::random_device{}()));
    const auto tools = root / "tools";
    for (const auto* name : {"mcpp", "mcppls"}) {
        const auto bin = tools / name / "bin"; std::filesystem::create_directories(bin);
        std::filesystem::copy_file(fixture, bin / (name + suffix));
    }
    const auto project = root / "project";
    std::filesystem::create_directories(project);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    { std::ofstream out(project / "main.cppm", std::ios::binary); out << "export module sample;\nexport int value() { return 42; }\n"; }
    { std::ofstream out(project / ".meditor-fixture-lsp-mode", std::ios::binary); out << "hover"; } // no fault branch: default replies cover hover and signatureHelp
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
    ui.Find(key("code-editor")).Tap();
    require(ui.ActiveTextInput().has_value());
    auto session = ui.ActiveTextInput();
    auto commit = [&](const std::string& text) {
        TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = text;
        require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok);
    };

    // Pointer rest over code surfaces the fixture hover value.
    const auto editorBounds = ui.Find(key("code-editor")).One().visible_bounds;
    PointerEvent move; move.type = PointerEventType::Move; move.pointer_id = 1;
    move.position = {editorBounds.x + editorBounds.width * 0.35F, editorBounds.y + editorBounds.height * 0.3F};
    move.device_kind = PointerDeviceKind::Mouse;
    ui.SendPointer(move);
    wait([&] { return ui.Find(key("hover-active")).Exists(); });
    ui.PressKey(Key::Escape);
    wait([&] { return !ui.Find(key("hover-active")).Exists(); });

    // A '(' commit surfaces the fixture signature; a ')' commit dismisses it.
    commit("fixture_fn(");
    wait([&] { return ui.Find(key("signature-active")).Exists(); });
    commit(")");
    wait([&] { return !ui.Find(key("signature-active")).Exists(); });

    // A ',' keeps signature help alive and Escape dismisses it.
    commit("g(1,");
    wait([&] { return ui.Find(key("signature-active")).Exists(); });
    ui.PressKey(Key::Escape);
    wait([&] { return !ui.Find(key("signature-active")).Exists(); });

    std::println("Fixture-backed hover and signature help: rest, escape, trigger, close-paren and comma flows: passed");
}

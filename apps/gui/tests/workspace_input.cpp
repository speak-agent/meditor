// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.ui.workbench;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("Workspace interaction contract failed at line " + std::to_string(location.line()));
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto directory = std::filesystem::temp_directory_path() / ("meditor-workspace-input-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {directory};
    const auto source = directory / "main.cppm";
    const std::string original = "export module demo;\nexport int value() { return 42; }\n";
    std::vector<std::string> arguments {"meditor-gui", "--workspace", directory.string(), "--open", "main.cppm", "--data-root", (directory / "runtime").string()};
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    for (const auto viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}})
    for (const auto deviceKind : {PointerDeviceKind::Touch, PointerDeviceKind::Mouse}) {
        { std::ofstream output(source, std::ios::binary); output << original; }
        Application application(meditor::ui::workbench, {.show_debug_overlay = false});
        UiTest ui(application, {.viewport = viewport});
        ui.Pump(); // Commit the queued startup lifecycle before interacting.
        ui.Find(UiSelector::Key("explorer-toggle")).Tap();
        const auto editor = UiSelector::Key("code-editor");
        require(ui.Find(editor).One().in_viewport);
        ui.Find(editor).Tap({.device_kind = deviceKind});
        require(ui.ActiveTextInput().has_value());
        ui.PressKey(Key::A, {.control = true});
        auto commit = [&](const std::string& text) {
            auto session = ui.ActiveTextInput(); require(session.has_value());
            TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = text;
            require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok);
            ui.Pump();
            require(ui.ActiveTextInput().has_value());
            require(ui.ActiveTextInput()->session_id == session->session_id);
        };
        commit("export module 世界; // 🙂\n");
        require(read(source) == original);
        ui.PressKey(Key::S, {.control = true});
        require(read(source) == "export module 世界; // 🙂\n");
        ui.PressKey(Key::Z, {.control = true});
        ui.PressKey(Key::S, {.control = true});
        require(read(source) == original);
        ui.PressKey(Key::Y, {.control = true});
        ui.PressKey(Key::S, {.control = true});
        require(read(source) == "export module 世界; // 🙂\n");

        // Replacing the editor by settings must end its native input session.
        const auto previousSession = ui.ActiveTextInput()->session_id;
        ui.Find(UiSelector::Key("settings-entry")).Tap();
        require(!ui.Find(editor).Exists());
        require(!ui.ActiveTextInput().has_value());
        ui.PressKey(Key::Escape);
        ui.Find(editor).Tap({.device_kind = deviceKind});
        require(ui.ActiveTextInput().has_value());
        require(ui.ActiveTextInput()->session_id != previousSession);
        TextInputCommand stale; stale.kind = TextInputCommandKind::CommitText; stale.text = "stale";
        require(ui.SendTextInput({previousSession, {stale}}).result_code == TextInputResultCode::SessionMismatch);
        ui.PressKey(Key::A, {.control = true});
        commit(original);
        ui.Find(UiSelector::Text("Save")).Tap();
        require(read(source) == original);
    }
    std::println("Headless workspace pointer focus, UTF-8 input, save, history and settings return: passed");
}

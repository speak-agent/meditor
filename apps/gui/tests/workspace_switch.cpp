// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.ui.workbench;
namespace {
std::filesystem::path destination;
int selections {};
bool cancel {};
bool pickerFails {};
huxerui::Task<std::optional<meditor::ui::WorkspaceSelection>> select_workspace() {
    ++selections;
    co_await huxerui::Delay(std::chrono::milliseconds(500));
    if (pickerFails) throw std::runtime_error("Synthetic picker failure");
    if (cancel) co_return std::nullopt;
    co_return meditor::ui::WorkspaceSelection{destination, {}};
}
huxerui::View root() { return meditor::ui::workbench({select_workspace}); }
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Workspace picker contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto directory = std::filesystem::temp_directory_path() / ("meditor-picker-" + std::to_string(std::random_device{}()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {directory};
    auto first = directory / "first"; destination = directory / std::filesystem::u8path("第二 workspace");
    std::filesystem::create_directories(first); std::filesystem::create_directories(destination);
    { std::ofstream file(first / "main.cppm"); file << "original"; }
    { std::ofstream file(destination / std::filesystem::u8path("模块.cppm")); file << "second"; }
    std::vector<std::string> arguments {"meditor-gui", "--workspace", first.string(), "--open", "main.cppm", "--data-root", (directory / "runtime").string()};
    std::vector<char*> pointers; for (auto& arg : arguments) pointers.push_back(arg.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    Application application(root, {.show_debug_overlay = false});
    UiTest ui(application, {.viewport = {1200, 800}}); ui.Pump();
    const auto change = UiSelector::Key("workspace-change"), editor = UiSelector::Key("code-editor");
    auto finish = [&] { ui.Pump(std::chrono::milliseconds(600)); ui.Pump(); };
    ui.Find(change).Tap(); ui.Pump(); require(selections == 1 && !ui.Find(change).One().enabled);
    // A pending platform picker does not freeze editing; its result must recheck state.
    ui.Find(editor).Tap(); ui.PressKey(Key::A, {.control = true});
    auto session = ui.ActiveTextInput(); require(session.has_value());
    TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = "edited";
    require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok); ui.Pump();
    finish();
    require(ui.Find(UiSelector::Text("Save modified documents before continuing")).Exists());
    require(ui.Find(editor).Exists() && !ui.Find(UiSelector::Key("模块.cppm")).Exists());
    ui.Find(change).Tap(); ui.Pump(); require(selections == 1); // Dirty preflight rejects another picker.
    ui.Find(UiSelector::Key("action-save")).Tap();
    { std::ifstream file(first / "main.cppm"); std::string saved; file >> saved; require(saved == "edited"); }
    cancel = true; ui.Find(change).Tap(); ui.Pump(); finish();
    require(selections == 2 && ui.Find(editor).Exists());
    cancel = false; pickerFails = true; ui.Find(change).Tap(); ui.Pump(); finish();
    require(selections == 3 && ui.Find(change).One().enabled && ui.Find(editor).Exists());
    pickerFails = false; auto valid = destination; destination = directory / "missing";
    ui.Find(change).Tap(); ui.Pump(); finish(); require(selections == 4 && ui.Find(editor).Exists());
    destination = valid; ui.Find(change).Tap(); ui.Pump(); finish();
    require(selections == 5 && !ui.Find(editor).Exists() && ui.Find(UiSelector::Key("模块.cppm")).Exists());
    ui.Find(UiSelector::Key("模块.cppm")).Tap(); require(ui.Find(editor).Exists());
    require(ui.Find(UiSelector::Text("模块.cppm")).Exists());
    std::println("Delayed workspace picker preserves concurrent edits, cancellation and invalid targets before a successful switch: passed");
}

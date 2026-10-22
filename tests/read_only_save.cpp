// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.platform;
import meditor.ui.workbench;
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("GUI save failure contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-save-ui-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code ec; std::filesystem::permissions(path / "main.cppm", std::filesystem::perms::owner_all, ec);
        std::filesystem::remove_all(path, ec);
    } } cleanup {root};
    auto file = root / "main.cppm", settings = root / "settings.json";
    for (const auto language : {"en", "zh"}) {
        meditor::Application preferences(root, {}, settings, {.dataRoot = root / "runtime"});
        auto state = preferences.execute({{"command", "settings.get"}})["data"];
        require(preferences.execute({{"command", "settings.update"}, {"params", {{"revision", state["revision"]}, {"changes", {{"language", language}}}}}})["ok"].get<bool>());
        meditor::i18n::Localizer messages(language);
        for (const auto viewport : {Size{390, 844}, Size{1200, 800}}) {
            meditor::write_file_atomic(file, "original");
            std::vector<std::string> arguments {"meditor", "--workspace", root.string(), "--open", "main.cppm", "--settings-file", settings.string(), "--data-root", (root / "runtime").string()};
            std::vector<char*> pointers; for (auto& arg : arguments) pointers.push_back(arg.data());
            meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
            Application application(meditor::ui::workbench, {.show_debug_overlay = false});
            UiTest ui(application, {.viewport = viewport}); ui.Pump();
            ui.Find(UiSelector::Key("explorer-toggle")).Tap();
            ui.Find(UiSelector::Key("code-editor")).Tap();
            ui.PressKey(Key::A, {.control = true});
            auto session = ui.ActiveTextInput(); require(session.has_value());
            TextInputCommand command; command.kind = TextInputCommandKind::CommitText; command.text = "pending";
            require(ui.SendTextInput({session->session_id, {command}}).result_code == TextInputResultCode::Ok); ui.Pump();
            std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);
            const auto stamp = std::filesystem::last_write_time(file);
            ui.PressKey(Key::S, {.control = true});
            const auto feedback = ui.Find(UiSelector::Text(messages.text("error.file-read-only"))).One();
            require(feedback.in_viewport && feedback.bounds.x >= 0 && feedback.bounds.y >= 0);
            require(feedback.bounds.x + feedback.bounds.width <= viewport.width + 0.5F);
            require(feedback.bounds.y + feedback.bounds.height <= viewport.height + 0.5F);
            require(meditor::read_file(file) == "original" && std::filesystem::last_write_time(file) == stamp);
            ui.PressKey(Key::Z, {.control = true}); ui.PressKey(Key::Y, {.control = true});
            std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            ui.PressKey(Key::S, {.control = true}); require(meditor::read_file(file) == "pending");
            require(ui.Find(UiSelector::Text(messages.text("status.saved"))).Exists());
        }
    }
    std::println("Read-only GUI save feedback and recoverable history preserve bytes across two languages and sizes: passed");
}

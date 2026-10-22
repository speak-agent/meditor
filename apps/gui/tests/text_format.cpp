// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.document;
import meditor.ui.editor;
import meditor.ui.workbench;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Editor format contract failed at line " + std::to_string(at.line()));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& text) { std::ofstream file(path, std::ios::binary); file << text; }
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    auto root = std::filesystem::temp_directory_path() / ("meditor-format-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    for (const std::string raw : {std::string("\xef\xbb\xbf") + "a\r\nb\r\n", std::string("a\r\nb\nc\r\n"), std::string("a\nb\n")}) {
        write(root / "format.cppm", raw);
        auto core = std::make_shared<meditor::Application>(root);
        const auto id = core->execute({{"command", "document.open"}, {"params", {{"path", "format.cppm"}}}})["data"]["document"].get<std::string>();
        meditor::ui::EditorState editor(core, id, [] {});
        auto client = editor.text_input_client(); client->BeginTextInput(7);
        auto text = [&] { return core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"]["text"].get<std::string>(); };
        auto save = [&] {
            auto data = core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"];
            require(core->execute({{"command", "document.save"}, {"params", {{"document", id}, {"revision", data["revision"]}}}})["ok"]);
        };
        auto select = [&](TextOffset start, TextOffset end) { TextInputCommand command; command.kind = TextInputCommandKind::SetSelection; command.selection_after = TextSelection{start, end}; require(client->ApplyTextInput({7, {command}}).result_code == TextInputResultCode::Ok); };
        TextInputCommand first; first.kind = TextInputCommandKind::CommitText; first.target = TextRange{0, 0}; first.text = "X";
        TextInputCommand last = first; last.target = TextRange{static_cast<TextOffset>(meditor::TextPresentation(raw).text().size() + 1), static_cast<TextOffset>(meditor::TextPresentation(raw).text().size() + 1)}; last.text = "Y";
        require(client->ApplyTextInput({7, {first, last}}).result_code == TextInputResultCode::Ok);
        const auto prefix = raw.starts_with("\xef\xbb\xbf") ? 3U : 0U;
        const auto edited = raw.substr(0, prefix) + "X" + raw.substr(prefix) + "Y";
        require(text() == edited); save(); require(read(root / "format.cppm") == edited);
        editor.undo(); require(text() == raw); save(); require(read(root / "format.cppm") == raw);
        const auto beforeRejected = client->State();
        TextInputCommand invalid; invalid.kind = TextInputCommandKind::SetSelection; invalid.selection_after = TextSelection{999, 999};
        require(client->ApplyTextInput({7, {first, invalid}}).result_code == TextInputResultCode::Rejected);
        require(client->State() == beforeRejected && text() == raw);
        // A native batch includes UTF-16 positions before CRLF normalization.
        auto* selection = dynamic_cast<TextSelectionClient*>(client.get()); require(selection);
        require(selection->PerformTextEditingAction(TextEditingAction::SelectAll, nullptr));
        TextInputCommand commit; commit.kind = TextInputCommandKind::CommitText; commit.text = "你好\r\n🙂"; commit.selection_after = TextSelection{6, 6};
        require(client->ApplyTextInput({7, {commit}}).result_code == TextInputResultCode::Ok);
        require(client->State().selection == TextSelection{5, 5});
        const auto ending = meditor::inspect_text_format(raw).lineEnding;
        const auto expected = raw.substr(0, prefix) + meditor::normalize_line_endings("你好\n🙂", ending);
        require(text() == expected); save(); require(read(root / "format.cppm") == expected);
        require(selection->PerformTextEditingAction(TextEditingAction::SelectAll, nullptr));
        commit.text.clear(); commit.selection_after.reset(); require(client->ApplyTextInput({7, {commit}}).result_code == TextInputResultCode::Ok);
        require(text() == raw.substr(0, prefix));
        commit.text = "\n"; require(client->ApplyTextInput({7, {commit}}).result_code == TextInputResultCode::Ok);
        require(text() == raw.substr(0, prefix) + (ending == meditor::LineEnding::CrLf ? "\r\n" : "\n"));
        select(0, 1);
        TextInputCommand composition; composition.kind = TextInputCommandKind::UpdateComposition; composition.text = "hello\r\n";
        const auto original = text();
        require(client->ApplyTextInput({7, {composition}}).result_code == TextInputResultCode::Ok);
        TextInputCommand cancel; cancel.kind = TextInputCommandKind::CancelComposition;
        require(client->ApplyTextInput({7, {cancel}}).result_code == TextInputResultCode::Ok && text() == original);
    }
    // DeleteSurrounding preserves the selected text's original mixed separators.
    write(root / "surrounding.cppm", "aa\r\nbb\ncc");
    {
        auto core = std::make_shared<meditor::Application>(root);
        auto id = core->execute({{"command", "document.open"}, {"params", {{"path", "surrounding.cppm"}}}})["data"]["document"].get<std::string>();
        meditor::ui::EditorState state(core, id, [] {}); auto client = state.text_input_client(); client->BeginTextInput(10);
        TextInputCommand select; select.kind = TextInputCommandKind::SetSelection; select.selection_after = TextSelection{1, 7};
        TextInputCommand erase; erase.kind = TextInputCommandKind::DeleteSurrounding; erase.delete_before = 1; erase.delete_after = 1;
        require(client->ApplyTextInput({10, {select, erase}}).result_code == TextInputResultCode::Ok);
        require(core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"]["text"] == "a\r\nbb\nc");
    }
    // Composition cancellation must restore absence of a BOM, not retain a newly pasted one.
    write(root / "bom.cppm", "");
    auto core = std::make_shared<meditor::Application>(root);
    auto id = core->execute({{"command", "document.open"}, {"params", {{"path", "bom.cppm"}}}})["data"]["document"].get<std::string>();
    meditor::ui::EditorState state(core, id, [] {}); auto client = state.text_input_client(); client->BeginTextInput(8);
    TextInputCommand update; update.kind = TextInputCommandKind::UpdateComposition; update.text = "\xef\xbb\xbf" "你好\r\n";
    require(client->ApplyTextInput({8, {update}}).result_code == TextInputResultCode::Ok);
    TextInputCommand cancel; cancel.kind = TextInputCommandKind::CancelComposition;
    require(client->ApplyTextInput({8, {cancel}}).result_code == TextInputResultCode::Ok);
    require(core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"]["text"] == "");
    // Native coordinates can temporarily address either unit of an inserted CRLF.
    TextInputCommand inserted; inserted.kind = TextInputCommandKind::CommitText; inserted.text = "a\r\nb";
    TextInputCommand position; position.kind = TextInputCommandKind::SetSelection; position.selection_after = TextSelection{3, 3};
    TextInputCommand erase; erase.kind = TextInputCommandKind::DeleteSurrounding; erase.delete_before = 1;
    require(client->ApplyTextInput({8, {inserted, position, erase}}).result_code == TextInputResultCode::Ok);
    require(core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"]["text"] == "a\nb");
    require(!client->Configuration().read_only);
    write(root / "legacy.cppm", "a\rb");
    auto legacyId = core->execute({{"command", "document.open"}, {"params", {{"path", "legacy.cppm"}}}})["data"]["document"].get<std::string>();
    meditor::ui::EditorState legacy(core, legacyId, [] {}); auto legacyClient = legacy.text_input_client(); legacyClient->BeginTextInput(9);
    require(legacyClient->Configuration().read_only);
    update.kind = TextInputCommandKind::CommitText; update.text = "changed";
    require(legacyClient->ApplyTextInput({9, {update}}).result_code == TextInputResultCode::Rejected);
    require(read(root / "legacy.cppm") == "a\rb");
    // The real workbench must retain BOM/CRLF through select-all, save and undo.
    const std::string original = "\xef\xbb\xbf" "export module demo;\r\n// 你好🙂\r\n";
    std::vector<std::string> arguments {"meditor-gui", "--workspace", root.string(), "--open", "main.cppm", "--data-root", (root / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    for (auto viewport : {Size{390, 844}, Size{800, 600}, Size{1200, 800}}) {
        write(root / "main.cppm", original);
        huxerui::Application application(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest ui(application, {.viewport = viewport}); ui.Pump();
        ui.Find(UiSelector::Key("explorer-toggle")).Tap(); ui.Find(UiSelector::Key("code-editor")).Tap();
        ui.PressKey(Key::A, {.control = true});
        require(ui.ActiveTextInput()->selection.Range() == TextRange{0, 28});
        ui.Find(UiSelector::Key("code-editor")).EnterText("你好\n"); ui.PressKey(Key::S, {.control = true});
        require(read(root / "main.cppm") == "\xef\xbb\xbf" "你好\r\n");
        ui.PressKey(Key::Z, {.control = true}); ui.PressKey(Key::S, {.control = true}); require(read(root / "main.cppm") == original);
        if (viewport.width == 1200) {
            const auto label = ui.Find(UiSelector::Key("document-language")).One().text.value_or("");
            require(label.find("UTF-8 BOM") != label.npos && label.find("CRLF") != label.npos);
        }
    }
    arguments[4] = "legacy.cppm"; pointers.clear(); for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    huxerui::Application readonly(meditor::ui::workbench, {.show_debug_overlay = false}); UiTest readonlyUi(readonly, {.viewport = {390, 844}}); readonlyUi.Pump();
    readonlyUi.Find(UiSelector::Key("explorer-toggle")).Tap();
    require(readonlyUi.Find(UiSelector::Key("format-read-only")).One().in_viewport);
    require(read(root / "legacy.cppm") == "a\rb");
    std::println("GUI BOM/CRLF preservation, mixed batches, composition rollback and read-only legacy data: passed");
}

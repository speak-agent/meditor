// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import meditor.application;
import meditor.ui.editor;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Editor input contract failed at line " + std::to_string(location.line())); }
struct TestClipboard : huxerui::PlatformClipboard {
    std::optional<std::string> text;
    std::optional<std::string> ReadText() override { return text; }
    bool WriteText(std::string_view value) override { text = value; return true; }
};
}
int main() {
    using namespace huxerui;
    auto root = std::filesystem::temp_directory_path() / ("meditor-editor-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    try {
        auto application = std::make_shared<meditor::Application>(root);
        auto opened = application->execute({{"command", "document.open"}, {"params", {{"path", "input.cppm"}, {"create", true}}}});
        require(opened.at("ok"));
        auto document = opened.at("data").at("document").get<std::string>();
        meditor::ui::EditorState editor(application, document, [] {});
        auto client = editor.text_input_client(); client->BeginTextInput(7);
        auto read = [&] { return application->execute({{"command", "document.read"}, {"params", {{"document", document}}}}).at("data").at("text").get<std::string>(); };
        auto apply = [&](TextInputCommand command) { return client->ApplyTextInput({7, {command}}).result_code; };
        require(client->ApplyTextInput({7, {}}).result_code == TextInputResultCode::Rejected);
        TextInputCommand update; update.kind = TextInputCommandKind::UpdateComposition; update.text = "ni";
        require(apply(update) == TextInputResultCode::Ok && read() == "ni");
        update.text = "你好🙂";
        require(apply(update) == TextInputResultCode::Ok && read() == "你好🙂");
        TextInputCommand finish; finish.kind = TextInputCommandKind::FinishComposition;
        require(apply(finish) == TextInputResultCode::Ok);
        editor.undo(); require(read().empty());
        editor.redo(); require(read() == "你好🙂");
        auto before = client->State();
        TextInputCommand valid; valid.kind = TextInputCommandKind::CommitText; valid.text = "x";
        TextInputCommand invalid; invalid.selection_after = TextSelection{999, 999};
        require(client->ApplyTextInput({7, {valid, invalid}}).result_code == TextInputResultCode::Rejected);
        require(client->State() == before && read() == "你好🙂");
        require(client->ApplyTextInput({99, {valid}}).result_code == TextInputResultCode::SessionMismatch);
        auto* selection = dynamic_cast<TextSelectionClient*>(client.get()); require(selection);
        TestClipboard clipboard;
        require(selection->PerformTextEditingAction(TextEditingAction::SelectAll, nullptr));
        require(selection->PerformTextEditingAction(TextEditingAction::Copy, &clipboard) && clipboard.text == "你好🙂");
        require(selection->PerformTextEditingAction(TextEditingAction::Cut, &clipboard) && read().empty());
        require(selection->PerformTextEditingAction(TextEditingAction::Paste, &clipboard) && read() == "你好🙂");
        auto context = client->QueryTextInputContext(7, 2, 2);
        require(context.result_code == TextInputResultCode::Ok && context.text == "🙂");
        require(client->QueryTextInputContext(7, 0, -1).result_code != TextInputResultCode::Ok);
        client->EndTextInput(7, TextInputEndReason::FocusLost);
        require(client->State().session_id == 0);
        // A find dialog ends the editor session; later typing must have its own undo entry.
        const auto beforeFocusChange = read();
        client->BeginTextInput(8); update.text = "候选";
        require(client->ApplyTextInput({8, {update}}).result_code == TextInputResultCode::Ok);
        client->EndTextInput(8, TextInputEndReason::FocusLost); client->BeginTextInput(9);
        require(client->ApplyTextInput({9, {valid}}).result_code == TextInputResultCode::Ok);
        editor.undo(); require(read() == beforeFocusChange + "候选");
        const auto matchRevision = editor.revision();
        require(editor.set_search_results({{0, 2}}, matchRevision));
        editor.insert("x");
        require(!editor.set_search_results({{0, 2}}, matchRevision));
        require(!editor.select_match({0, 2}, matchRevision));
        require(!editor.set_search_results({{0, 3}}, editor.revision()));
        require(editor.select_match({0, 2}, editor.revision()));
        const auto oldSize = read().size();
        require(application->execute({{"command", "document.edit"}, {"params", {{"document", document}, {"revision", editor.revision()}, {"edits", meditor::Json::array({{{"start", 0}, {"end", oldSize}, {"text", "a🙂b"}}})}}}})["ok"]);
        editor.refresh(); require(editor.selection() == TextSelection{0, 1});
        require(editor.select_byte_range(5, 5, editor.revision()) && editor.selection() == TextSelection{3, 3});
        require(!editor.select_byte_range(2, 2, editor.revision()));
        application.reset();
    } catch (...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
    std::println("Editor IME grouping, atomic batches and clipboard: passed");
}

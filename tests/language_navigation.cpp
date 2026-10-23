// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.document;
import meditor.ui.editor;

namespace {
std::shared_ptr<meditor::ui::EditorState> mountedEditor;
huxerui::View editor_root() { return meditor::ui::code_editor(mountedEditor); }
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Language navigation contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace meditor;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-navigation-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const std::string raw = "\xef\xbb\xbf" "你好🙂\r\nexport int value;\r\n";
    { std::ofstream file(root / "main.cppm", std::ios::binary); file << raw; }
    auto app = std::make_shared<meditor::Application>(root);
    const auto id = app->execute({{"command", "document.open"}, {"params", {{"path", "main.cppm"}}}})["data"]["document"].get<std::string>();
    ui::EditorState editor(app, id, [] {});
    const auto revision = editor.revision();
    require(editor.select_lsp_range({0, 0}, {0, 1}, revision));
    require(editor.selection() == huxerui::TextSelection{0, 0});
    require(editor.select_lsp_range({0, 3}, {0, 5}, revision));
    require(editor.selection() == huxerui::TextSelection{2, 4});
    require(editor.lsp_position().line == 0 && editor.lsp_position().character == 5);
    require(editor.select_lsp_range({1, 11}, {1, 16}, revision));
    require(editor.selection() == huxerui::TextSelection{16, 21});
    const auto selected = editor.selection();
    const auto reject = [&](Position start, Position end, std::uint64_t version) {
        require(!editor.select_lsp_range(start, end, version));
        require(editor.selection() == selected && editor.revision() == revision);
    };
    reject({0, 4}, {0, 5}, revision); // Inside the emoji's UTF-16 surrogate pair.
    reject({0, 6}, {1, 0}, revision); // Between CR and LF.
    reject({1, 16}, {1, 11}, revision);
    reject({0, 1}, {0, 0}, revision); // BOM aliases must not turn a reversed range into an empty one.
    reject({99, 0}, {99, 0}, revision);
    reject({1, 0}, {1, 0}, revision + 1);
    // A core edit can advance the document while the editor still caches an older revision.
    require(app->execute({{"command", "document.edit"}, {"params", {{"document", id}, {"revision", revision}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "X"}}})}}}})["ok"]);
    require(!editor.select_lsp_range({1, 0}, {1, 6}, revision));
    require(editor.revision() == revision + 1);
    require(editor.select_lsp_range({1, 0}, {1, 6}, revision + 1));
    require(editor.selection() == huxerui::TextSelection{7, 13});
    require(!editor.request_focus());
    mountedEditor = std::make_shared<ui::EditorState>(editor);
    editor.focus_when_mounted();
    {
        huxerui::Application gui(editor_root, {.show_debug_overlay = false});
        huxerui::testing::UiTest test(gui, {.viewport = {500, 400}}); test.Pump();
        require(test.Find(huxerui::testing::UiSelector::Key("code-editor")).One().focused);
        require(editor.request_focus()); test.Pump();
        require(test.Find(huxerui::testing::UiSelector::Key("code-editor")).One().focused);
    }
    require(!editor.request_focus()); mountedEditor.reset();
    std::ifstream input(root / "main.cppm", std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()) == raw);
    std::println("Revision-bound LSP navigation: BOM, CRLF, Unicode, invalid ranges and unsaved core edits passed");
}

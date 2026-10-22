// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.ui.editor;
import std;
import huxerui;
import meditor.application;
import meditor.document;

export namespace meditor::ui {
class EditorState {
public:
    struct Impl;
private:
    std::shared_ptr<Impl> impl_;
    friend huxerui::View code_editor(std::shared_ptr<EditorState> state, bool dark);
public:
    EditorState(std::shared_ptr<Application> application, std::string document, std::function<void()> changed);
    void refresh();
    void undo();
    void redo();
    void insert(std::string text);
    std::uint64_t revision() const;
    std::pair<std::size_t, std::size_t> selection_bytes() const;
    bool select_byte_range(std::size_t start, std::size_t end, std::uint64_t revision);
    bool select_lsp_range(Position start, Position end, std::uint64_t revision);
    bool request_focus();
    void focus_when_mounted();
    Position lsp_position() const;
    bool select_match(huxerui::TextSelection selection, std::uint64_t revision);
    bool set_search_results(std::vector<huxerui::TextRange> matches, std::uint64_t revision);
    void clear_search_results();
    std::string document_id() const;
    std::string document_path() const;
    bool highlighting_limited() const;
    std::string language_id() const;
    TextFormat text_format() const;
    huxerui::TextSelection selection() const;
    std::shared_ptr<huxerui::TextInputClient> text_input_client() const;
};
huxerui::View code_editor(std::shared_ptr<EditorState> state, bool dark = true);
}

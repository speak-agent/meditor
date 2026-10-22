// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.ui.editor;
import std;
import huxerui;
import meditor.application;

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
    std::string document_id() const;
    bool highlighting_limited() const;
    huxerui::TextSelection selection() const;
    std::shared_ptr<huxerui::TextInputClient> text_input_client() const;
};
huxerui::View code_editor(std::shared_ptr<EditorState> state, bool dark = true);
}

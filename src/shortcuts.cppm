// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.shortcuts;
import std;
import huxerui;
export namespace meditor::shortcuts {
enum class Context { Workbench, Editor };
enum class Command { Save, Find, Outline, Problems, Definition, References, Dismiss, SelectAll, Undo, Redo };
struct Binding {
    Command command;
    Context context;
    huxerui::Key key;
    bool primary {}, shift {};
    std::string_view commandId, labelId;
};
std::span<const Binding> defaults();
std::optional<Command> resolve(const huxerui::KeyEvent& event, Context context);
std::string label(Command command, bool apple);
}

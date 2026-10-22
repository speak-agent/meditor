// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.shortcuts;
import std;
import huxerui;
namespace meditor::shortcuts {
namespace {
const std::array BINDINGS {
    Binding{Command::Save, Context::Workbench, huxerui::Key::S, true, false, "document.save", "action.save"},
    Binding{Command::Find, Context::Workbench, huxerui::Key::F, true, false, "document.find", "action.find"},
    Binding{Command::Outline, Context::Workbench, huxerui::Key::O, true, true, "language.symbols", "language.outline"},
    Binding{Command::Problems, Context::Workbench, huxerui::Key::M, true, true, "language.diagnostics", "language.problems"},
    Binding{Command::Definition, Context::Workbench, huxerui::Key::F12, false, false, "language.definition", "language.definition"},
    Binding{Command::References, Context::Workbench, huxerui::Key::F12, false, true, "language.references", "language.references"},
    Binding{Command::Dismiss, Context::Workbench, huxerui::Key::Escape, false, false, "ui.dismiss", "language.close"},
    Binding{Command::SelectAll, Context::Editor, huxerui::Key::A, true, false, "editor.selectAll", "action.selectAll"},
    Binding{Command::Undo, Context::Editor, huxerui::Key::Z, true, false, "document.undo", "action.undo"},
    Binding{Command::Redo, Context::Editor, huxerui::Key::Z, true, true, "document.redo", "action.redo"},
    Binding{Command::Redo, Context::Editor, huxerui::Key::Y, true, false, "document.redo", "action.redo"},
};
}
std::span<const Binding> defaults() { return BINDINGS; }
std::optional<Command> resolve(const huxerui::KeyEvent& event, Context context) {
    if (event.type != huxerui::KeyEventType::Down || event.modifiers.alt || (event.repeat && context == Context::Workbench)) return std::nullopt;
    const bool primary = event.modifiers.control || event.modifiers.meta;
    for (const auto& binding : BINDINGS)
        if (binding.context == context && binding.key == event.key && binding.primary == primary && binding.shift == event.modifiers.shift) return binding.command;
    return std::nullopt;
}
std::string label(Command command, bool apple) {
    for (const auto& binding : BINDINGS) if (binding.command == command) {
        std::string result = binding.primary ? (apple ? "Cmd+" : "Ctrl+") : "";
        if (binding.shift) result += "Shift+";
        switch (binding.key) {
            case huxerui::Key::F12: return result + "F12";
            case huxerui::Key::Escape: return result + "Esc";
            case huxerui::Key::A: return result + "A";
            case huxerui::Key::S: return result + "S";
            case huxerui::Key::F: return result + "F";
            case huxerui::Key::O: return result + "O";
            case huxerui::Key::M: return result + "M";
            case huxerui::Key::Z: return result + "Z";
            default: return result;
        }
    }
    return {};
}
}

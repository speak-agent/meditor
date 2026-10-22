// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import meditor.shortcuts;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("Shortcut context contract failed"); }
}
int main() {
    using namespace huxerui;
    using namespace meditor::shortcuts;
    require(resolve({KeyEventType::Down, Key::S, {}, {.control = true}}, Context::Workbench) == Command::Save);
    require(resolve({KeyEventType::Down, Key::S, {}, {.meta = true}}, Context::Workbench) == Command::Save);
    require(!resolve({KeyEventType::Down, Key::S, {}, {.shift = true, .control = true}}, Context::Workbench));
    require(!resolve({KeyEventType::Down, Key::S, {}, {.control = true, .alt = true}}, Context::Workbench));
    require(!resolve({KeyEventType::Up, Key::S, {}, {.control = true}}, Context::Workbench));
    require(!resolve({KeyEventType::Down, Key::S, {}, {.control = true}, true}, Context::Workbench));
    require(!resolve({KeyEventType::Down, Key::Z, {}, {.control = true}}, Context::Workbench));
    require(resolve({KeyEventType::Down, Key::Z, {}, {.control = true}, true}, Context::Editor) == Command::Undo);
    require(resolve({KeyEventType::Down, Key::Z, {}, {.shift = true, .meta = true}}, Context::Editor) == Command::Redo);
    require(resolve({KeyEventType::Down, Key::F12, {}, {.shift = true}}, Context::Workbench) == Command::References);
    require(!resolve({KeyEventType::Down, Key::F12, {}, {.control = true}}, Context::Workbench));
    require(!resolve({KeyEventType::Down, Key::Escape, {}, {.shift = true}}, Context::Workbench));
    require(label(Command::Save, true) == "Cmd+S" && label(Command::Save, false) == "Ctrl+S");
    require(label(Command::Redo, true) == "Cmd+Shift+Z");
    auto bindings = defaults();
    for (std::size_t i = 0; i < bindings.size(); ++i) for (std::size_t j = i + 1; j < bindings.size(); ++j) {
        const auto& a = bindings[i]; const auto& b = bindings[j];
        require(a.context != b.context || a.key != b.key || a.primary != b.primary || a.shift != b.shift);
    }
    std::println("Shortcut contexts: primary key, AltGr exclusion, exact modifiers, repeat policy and collision checks passed");
}

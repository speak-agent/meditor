// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.ui.theme;
import huxerui;

export namespace meditor::ui {
struct Palette {
    huxerui::Color canvas, surface, raised, ink, muted, accent, onAccent, selected, border, disabledInk;
};
Palette palette(bool dark);
huxerui::ThemeDefinition editor_theme(bool dark, bool compact, bool reducedMotion);
}

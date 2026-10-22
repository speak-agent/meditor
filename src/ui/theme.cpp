// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.ui.theme;
import std;
import huxerui;

namespace meditor::ui {
using namespace huxerui;
Palette palette(bool dark) {
    if (dark) return {
        Color::Rgb(15, 20, 29), Color::Rgb(23, 30, 42), Color::Rgb(32, 42, 57),
        Color::Rgb(222, 230, 243), Color::Rgb(151, 166, 188), Color::Rgb(116, 171, 255),
        Color::Rgb(14, 26, 44), Color::Rgb(38, 57, 83), Color::Rgb(56, 72, 96), Color::Rgb(105, 121, 145),
    };
    return {
        Color::Rgb(250, 252, 255), Color::Rgb(242, 246, 251), Color::Rgb(230, 237, 246),
        Color::Rgb(31, 45, 65), Color::Rgb(82, 101, 126), Color::Rgb(42, 103, 204),
        Color::White(), Color::Rgb(219, 232, 251), Color::Rgb(202, 214, 231), Color::Rgb(124, 140, 161),
    };
}
ThemeDefinition editor_theme(bool dark, bool compact, bool reducedMotion) {
    const auto colors = palette(dark);
    auto spec = dark ? FlatDarkThemeSpec() : FlatLightThemeSpec();
    spec.colors.primary = colors.accent; spec.colors.on_primary = colors.onAccent;
    spec.colors.on_surface = colors.ink; spec.motion.reduced_motion = reducedMotion;
    spec.interactions.focus_ring = FocusRing{colors.accent, 2, 2};
    auto definition = FlatThemeDefinition(spec);
    auto hover = colors.accent; hover.alpha = dark ? 0.12F : 0.08F;
    auto press = colors.accent; press.alpha = dark ? 0.22F : 0.16F;
    const AnimationSpec enter = reducedMotion ? AnimationSpec{SnapSpec{}} : AnimationSpec{TweenSpec{0.08, Easing::EaseOut}};
    const AnimationSpec exit = reducedMotion ? AnimationSpec{SnapSpec{}} : AnimationSpec{TweenSpec{0.12, Easing::EaseOut}};
    definition.Set(ButtonStyle{
        .background = colors.raised,
        .label_style = TextStyle{Font::System(14), colors.ink},
        .disabled_background = colors.surface,
        .disabled_label = colors.disabledInk,
        .padding = EdgeInsets::Symmetric(10, 6),
        .minimum_height = compact ? 44.0F : 36.0F,
        .corner_radii = CornerRadii{6},
        .indication = Indication{
            .hover = IndicationLayer{.fill = hover, .enter = enter, .exit = exit},
            .press = IndicationLayer{.fill = press, .enter = enter, .exit = exit},
        },
    });
    definition.Set(TabsStyle{
        .background = colors.surface,
        .label_style = TextStyle{Font::System(14), colors.ink},
        .selected_label = colors.accent,
        .disabled_label = colors.disabledInk,
        .indicator = colors.accent,
        .divider_color = colors.border,
        .divider_height = 1,
        .minimum_height = compact ? 44.0F : 40.0F,
        .indication = Indication{
            .hover = IndicationLayer{.fill = hover, .enter = enter, .exit = exit},
            .press = IndicationLayer{.fill = press, .enter = enter, .exit = exit},
        },
        .indicator_animation_duration = reducedMotion ? 0 : 0.12,
    });
    return definition;
}
}

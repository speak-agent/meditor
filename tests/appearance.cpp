// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.ui.theme;

namespace {
bool gDark {}, gCompact {}, gReduced {};
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Appearance contract failed at line " + std::to_string(at.line()));
}
double luminance(huxerui::Color color) {
    auto linear = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
    return .2126 * linear(color.red) + .7152 * linear(color.green) + .0722 * linear(color.blue);
}
double contrast(huxerui::Color a, huxerui::Color b) {
    auto x = luminance(a), y = luminance(b); return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
huxerui::View probe() {
    using namespace huxerui;
    return Scope([]() -> View {
        auto selected = UseState(std::size_t{0});
        return Theme{meditor::ui::editor_theme(gDark, gCompact, gReduced), Column {
            Button("Action").Key("probe-action"),
            Tabs({"One", "Two"}, selected).Key("probe-tabs").On<TabsEvents::Changed>([selected](std::size_t index) mutable { selected = index; }),
        }.With(Padding(20), Spacing(16), Background(meditor::ui::palette(gDark).canvas), CrossAlign(CrossAxisAlignment::Start))};
    });
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    for (bool dark : {false, true}) for (bool compact : {false, true}) for (bool reduced : {false, true}) {
        gDark = dark; gCompact = compact; gReduced = reduced;
        const auto colors = meditor::ui::palette(dark);
        require(contrast(colors.ink, colors.canvas) >= 7 && contrast(colors.ink, colors.raised) >= 4.5);
        require(contrast(colors.muted, colors.surface) >= 4.5 && contrast(colors.ink, colors.selected) >= 4.5);
        require(contrast(colors.accent, colors.onAccent) >= 4.5 && contrast(colors.accent, colors.raised) >= 3);
        Application app(probe, {.show_debug_overlay = false}); UiTest ui(app, {.viewport = {600, 400}}); ui.Pump(std::chrono::milliseconds(200));
        const auto button = ui.Find(UiSelector::Key("probe-action")).One();
        require(button.bounds.height >= (compact ? 44 : 36));
        for (const auto& tab : ui.FindSemantics(UiSemanticSelector::Role(SemanticRole::Tab)).All()) require(tab.bounds.height >= (compact ? 44 : 40));
        auto before = ui.CaptureSnapshot();
        ui.SendPointer({PointerEventType::Move, 1, {button.bounds.x + 8, button.bounds.y + 8}}); ui.Pump();
        auto entered = ui.CaptureSnapshot(); ui.Pump(std::chrono::milliseconds(40)); auto middle = ui.CaptureSnapshot();
        ui.Pump(std::chrono::milliseconds(200)); auto end = ui.CaptureSnapshot();
        require(end != before);
        if (reduced) require(entered == middle && middle == end); else require(middle != end);
        ui.PressKey(Key::Tab); ui.PressKey(Key::Tab); ui.PressKey(Key::ArrowRight); ui.Pump(std::chrono::milliseconds(200));
        require(ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Tab), UiSemanticSelector::Selected(true))).One().label == "Two");
    }
    std::println("Light/dark contrast, touch targets, hover motion, reduced motion and native tab keyboard semantics: passed");
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.ui.editor;
import std;
import huxerui;
import meditor.application;

namespace meditor::ui {
using namespace huxerui;
namespace {
std::size_t byte_at(const std::string& text, TextOffset offset) {
    if (offset < 0) throw std::runtime_error("Invalid text offset");
    TextOffset unit {};
    for (std::size_t i = 0;;) {
        if (unit == offset) return i;
        if (i >= text.size()) break;
        auto ch = static_cast<unsigned char>(text[i]);
        auto count = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
        unit += count == 4 ? 2 : 1; i += count;
    }
    throw std::runtime_error("Invalid UTF-16 boundary");
}
TextOffset units(std::string_view text) { TextOffset count {}; for (unsigned char ch : text) if ((ch & 0xc0) != 0x80) count += ch >= 0xf0 ? 2 : 1; return count; }
struct Line { std::size_t byte {}; TextOffset unit {}; std::string text; };
}
struct EditorState::Impl : TextInputClient, TextSelectionClient {
    std::shared_ptr<Application> application;
    std::string document;
    std::function<void()> changed;
    TextEditingValue value;
    std::optional<TextEditingValue> baseline;
    std::uint64_t documentRevision {};
    std::uint64_t compositionSequence {};
    std::string compositionGroup;
    std::string compositionPrefix {"ime-" + std::to_string(std::random_device{}()) + "-"};
    TextInputState input;
    std::vector<Line> lines;
    std::vector<std::vector<std::pair<TextOffset, float>>> positions;
    Size viewport;
    float lineHeight {24}, ascent {18};
    std::size_t topLine {};
    bool dark {true};
    bool revealCaret {true};
    void rebuild_() {
        auto oldLines = std::move(lines);
        auto oldPositions = std::move(positions);
        lines.clear(); std::size_t byte {}; TextOffset unit {};
        while (true) {
            auto end = value.text.find('\n', byte); auto length = end == std::string::npos ? value.text.size() - byte : end - byte;
            auto content = value.text.substr(byte, length); if (!content.empty() && content.back() == '\r') content.pop_back();
            lines.push_back({byte, unit, content});
            if (end == std::string::npos) break;
            unit += units(std::string_view(value.text).substr(byte, end - byte + 1)); byte = end + 1;
        }
        positions.resize(lines.size());
        for (std::size_t i = 0; i < lines.size() && i < oldLines.size(); ++i)
            if (lines[i].text == oldLines[i].text) positions[i] = std::move(oldPositions[i]);
    }
    void refresh_() {
        auto response = application->execute({{"command", "document.read"}, {"params", {{"document", document}}}});
        if (!response.value("ok", false)) return;
        auto data = response.at("data"); auto revision = data.at("revision").get<std::uint64_t>();
        if (revision == documentRevision) return;
        value.text = data.at("text").get<std::string>(); documentRevision = revision;
        auto length = units(value.text); value.selection.anchor = std::min(value.selection.anchor, length); value.selection.active = std::min(value.selection.active, length);
        ++input.revision; ++input.content_revision; rebuild_();
    }
    bool publish_(const TextEditingValue& next, const std::string& historyGroup = {}) {
        if (next.text != value.text) {
            std::size_t prefix {}, suffix {};
            while (prefix < value.text.size() && prefix < next.text.size() && value.text[prefix] == next.text[prefix]) ++prefix;
            while (prefix && prefix < value.text.size() && (static_cast<unsigned char>(value.text[prefix]) & 0xc0) == 0x80) --prefix;
            while (suffix < value.text.size() - prefix && suffix < next.text.size() - prefix && value.text[value.text.size() - suffix - 1] == next.text[next.text.size() - suffix - 1]) ++suffix;
            while (suffix && value.text.size() > suffix && (static_cast<unsigned char>(value.text[value.text.size() - suffix]) & 0xc0) == 0x80) --suffix;
            auto reply = application->execute({{"command", "document.edit"}, {"params", {{"document", document}, {"revision", documentRevision}, {"historyGroup", historyGroup}, {"edits", Json::array({{{"start", prefix}, {"end", value.text.size() - suffix}, {"text", next.text.substr(prefix, next.text.size() - prefix - suffix)}}})}}}});
            if (!reply.value("ok", false)) { refresh_(); return false; }
            documentRevision = reply["data"]["revision"]; ++input.content_revision;
        }
        revealCaret = revealCaret || next.text != value.text || next.selection != value.selection;
        value = next; ++input.revision; rebuild_();
        if (changed) changed();
        return true;
    }
    void replace_(TextEditingValue& next, TextRange range, const std::string& text) {
        auto start = byte_at(next.text, range.start), end = byte_at(next.text, range.end);
        if (start > end) throw std::runtime_error("Invalid range");
        next.text.replace(start, end - start, text); auto caret = range.start + units(text); next.selection = {caret, caret};
    }
    TextInputConfiguration Configuration() const override { TextInputConfiguration config; config.multiline = true; return config; }
    TextInputState State() const override { auto result = input; result.selection = value.selection; result.composition = value.composition; return result; }
    TextInputState BeginTextInput(TextInputSessionId session) override { input.session_id = session; ++input.revision; return State(); }
    TextInputApplyResult ApplyTextInput(const TextInputCommandBatch& batch) override {
        if (batch.session_id != input.session_id) return {TextInputResultCode::SessionMismatch};
        if (batch.commands.empty()) return {TextInputResultCode::Rejected};
        auto next = value; auto original = baseline;
        try {
            for (const auto& command : batch.commands) {
                auto range = command.target.value_or(next.composition.value_or(next.selection.Range()));
                if (command.coordinate_space == TextInputCoordinateSpace::Composition && command.target) {
                    if (!next.composition) throw std::runtime_error("Missing composition");
                    range.start += next.composition->start; range.end += next.composition->start;
                    if (range.end > next.composition->end) throw std::runtime_error("Composition range exceeded");
                }
                switch (command.kind) {
                case TextInputCommandKind::SetSelection:
                    if (!command.selection_after) throw std::runtime_error("Missing selection"); next.selection = *command.selection_after; break;
                case TextInputCommandKind::BeginComposition:
                    if (next.composition || !command.target) throw std::runtime_error("Invalid composition start");
                    original = next; next.composition = range; break;
                case TextInputCommandKind::UpdateComposition: {
                    if (!original) original = next;
                    auto old = next.composition; replace_(next, range, command.text);
                    next.composition = old ? TextRange{old->start, old->end + units(command.text) - range.Length()} : TextRange{range.start, range.start + units(command.text)};
                    break;
                }
                case TextInputCommandKind::CommitText:
                    replace_(next, range, command.text); next.composition.reset(); original.reset(); break;
                case TextInputCommandKind::FinishComposition: next.composition.reset(); original.reset(); break;
                case TextInputCommandKind::CancelComposition:
                    if (original) next = *original; next.composition.reset(); original.reset(); break;
                case TextInputCommandKind::DeleteSurrounding: {
                    if (command.delete_before < 0 || command.delete_after < 0) throw std::runtime_error("Invalid delete");
                    auto selected = next.selection.Range(); auto a = selected.start, b = selected.end;
                    if (command.delete_unit == TextInputUnit::UnicodeCodePoint) {
                        auto left = byte_at(next.text, a), right = byte_at(next.text, b);
                        for (TextOffset i = 0; i < command.delete_before && left; ++i) { --left; while (left && (static_cast<unsigned char>(next.text[left]) & 0xc0) == 0x80) --left; }
                        for (TextOffset i = 0; i < command.delete_after && right < next.text.size(); ++i) { ++right; while (right < next.text.size() && (static_cast<unsigned char>(next.text[right]) & 0xc0) == 0x80) ++right; }
                        a = units(std::string_view(next.text).substr(0, left)); b = units(std::string_view(next.text).substr(0, right));
                    } else { a = std::max<TextOffset>(0, a - command.delete_before); b = std::min(units(next.text), b + command.delete_after); }
                    auto selectedText = AttributedText(next.text).TextInRange(selected);
                    replace_(next, {a, b}, selectedText); next.selection = {a, a + selected.Length()}; next.composition.reset(); original.reset(); break;
                }
                }
                if (command.selection_after) next.selection = *command.selection_after;
                byte_at(next.text, next.selection.anchor); byte_at(next.text, next.selection.active);
                if (next.composition) { byte_at(next.text, next.composition->start); byte_at(next.text, next.composition->end); }
            }
            if ((original || baseline) && compositionGroup.empty()) compositionGroup = compositionPrefix + std::to_string(++compositionSequence);
            if (!publish_(next, compositionGroup)) return {TextInputResultCode::Rejected};
            baseline = original;
            if (!next.composition) compositionGroup.clear();
            return {TextInputResultCode::Ok};
        } catch (...) { return {TextInputResultCode::Rejected}; }
    }
    TextInputContext QueryTextInputContext(TextInputSessionId session, TextOffset start, TextOffset length) const override {
        if (session != input.session_id) return {.result_code = TextInputResultCode::SessionMismatch, .session_id = session};
        try {
            if (start < 0 || length < 0) return {.session_id = session};
            auto total = units(value.text); if (start > total) return {.session_id = session};
            auto end = start + std::min(total - start, length);
            auto a = byte_at(value.text, start), b = byte_at(value.text, end);
            return {TextInputResultCode::Ok, session, start, total, value.text.substr(a, b - a), value.selection, value.composition};
        } catch (...) { return {.session_id = session}; }
    }
    std::pair<std::size_t, float> location_(TextOffset unit) const {
        std::size_t row {};
        while (row + 1 < lines.size() && lines[row + 1].unit <= unit) ++row;
        float x {};
        for (auto [offset, position] : positions[row]) { if (offset > unit - lines[row].unit) break; x = position; }
        return {row, x};
    }
    TextInputGeometry QueryTextInputGeometry(TextInputSessionId session, TextRange range) const override {
        if (session != input.session_id) return {.result_code = TextInputResultCode::SessionMismatch, .session_id = session};
        auto [row, x] = location_(range.end);
        TextInputGeometry result {TextInputResultCode::Ok, session, {64 + x, 10 + (static_cast<float>(row) - topLine) * lineHeight, 1.5F, lineHeight}, {}};
        if (range.start != range.end) {
            auto [first, firstX] = location_(range.start);
            for (auto line = first; line <= row; ++line) result.range_rects.push_back({64 + (line == first ? firstX : 0), 10 + (static_cast<float>(line) - topLine) * lineHeight, std::max(1.0F, (line == row ? x : viewport.width - 70) - (line == first ? firstX : 0)), lineHeight});
        }
        return result;
    }
    TextInputPositionResult QueryTextInputPosition(TextInputSessionId session, Point point) const override {
        if (session != input.session_id) return {.result_code = TextInputResultCode::SessionMismatch, .session_id = session};
        auto row = std::min(lines.size() - 1, topLine + static_cast<std::size_t>(std::max(0.0F, std::floor((point.y - 10) / lineHeight))));
        TextOffset column {}; float best = std::numeric_limits<float>::infinity();
        for (auto [offset, x] : positions[row]) { auto distance = std::abs(x - (point.x - 64)); if (distance < best) { best = distance; column = offset; } }
        return {TextInputResultCode::Ok, session, {lines[row].unit + column}};
    }
    TextInputKeyResult HandleTextKey(const KeyEvent& event) override {
        if (event.type != KeyEventType::Down) return TextInputKeyResult::Unhandled;
        bool control = event.modifiers.control || event.modifiers.meta;
        if (value.composition) return TextInputKeyResult::Unhandled;
        if (control && event.key == Key::A) { auto next = value; next.selection = {0, units(next.text)}; publish_(next); return TextInputKeyResult::Handled; }
        if (control && (event.key == Key::Z || event.key == Key::Y)) {
            application->execute({{"command", event.key == Key::Y || event.modifiers.shift ? "document.redo" : "document.undo"}, {"params", {{"document", document}, {"revision", documentRevision}}}}); refresh_(); if (changed) changed(); return TextInputKeyResult::Handled;
        }
        auto next = value; auto selection = next.selection.Range(); auto caret = next.selection.active;
        auto move = [&](TextOffset target) { if (!event.modifiers.shift) next.selection.anchor = target; next.selection.active = target; };
        auto previous = [&](TextOffset offset) { if (!offset) return offset; auto byte = byte_at(next.text, offset); --byte; while (byte && (static_cast<unsigned char>(next.text[byte]) & 0xc0) == 0x80) --byte; return units(std::string_view(next.text).substr(0, byte)); };
        auto following = [&](TextOffset offset) { auto byte = byte_at(next.text, offset); if (byte == next.text.size()) return offset; ++byte; while (byte < next.text.size() && (static_cast<unsigned char>(next.text[byte]) & 0xc0) == 0x80) ++byte; return units(std::string_view(next.text).substr(0, byte)); };
        if (event.key == Key::ArrowLeft) move(previous(caret));
        else if (event.key == Key::ArrowRight) move(following(caret));
        else if (event.key == Key::Home) { auto [row, x] = location_(caret); move(lines[row].unit); }
        else if (event.key == Key::End) { auto [row, x] = location_(caret); move(lines[row].unit + units(lines[row].text)); }
        else if (event.key == Key::ArrowUp || event.key == Key::ArrowDown) { auto [row, x] = location_(caret); auto target = event.key == Key::ArrowUp ? (row ? row - 1 : 0) : std::min(row + 1, lines.size() - 1); auto hit = QueryTextInputPosition(input.session_id, {64 + x, 10 + (static_cast<float>(target) - topLine) * lineHeight + 1}); move(hit.position.offset); }
        else if (event.key == Key::Backspace) replace_(next, selection.IsCollapsed() ? TextRange{previous(caret), caret} : selection, "");
        else if (event.key == Key::Delete) replace_(next, selection.IsCollapsed() ? TextRange{caret, following(caret)} : selection, "");
        else if (event.key == Key::Enter) replace_(next, selection, "\n");
        else if (event.key == Key::Tab) replace_(next, selection, "    ");
        else return TextInputKeyResult::Unhandled;
        publish_(next); return TextInputKeyResult::Handled;
    }
    bool CanPerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) const override {
        if (value.composition) return false;
        if (action == TextEditingAction::SelectAll) return !value.text.empty();
        return clipboard && (action == TextEditingAction::Paste || !value.selection.IsCollapsed());
    }
    bool PerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) override {
        if (!CanPerformTextEditingAction(action, clipboard)) return false;
        auto next = value;
        if (action == TextEditingAction::SelectAll) next.selection = {0, units(next.text)};
        else if (action == TextEditingAction::Paste) {
            auto text = clipboard->ReadText(); if (!text) return false;
            replace_(next, next.selection.Range(), *text);
        } else {
            auto range = next.selection.Range();
            auto text = next.text.substr(byte_at(next.text, range.start), byte_at(next.text, range.end) - byte_at(next.text, range.start));
            if (!clipboard->WriteText(text)) return false;
            if (action == TextEditingAction::Copy) return true;
            replace_(next, range, "");
        }
        return publish_(next);
    }
    bool ClearSelection() override {
        if (value.selection.IsCollapsed()) return false;
        auto next = value; next.selection.anchor = next.selection.active; publish_(next); return true;
    }
    bool SelectWord(Point point) override {
        auto hit = QueryTextInputPosition(input.session_id, point);
        auto byte = byte_at(value.text, hit.position.offset); auto first = byte, last = byte;
        auto word = [](unsigned char ch) { return std::isalnum(ch) || ch == '_' || ch >= 0x80; };
        while (first && word(static_cast<unsigned char>(value.text[first - 1]))) --first;
        while (last < value.text.size() && word(static_cast<unsigned char>(value.text[last]))) ++last;
        auto next = value; next.selection = {units(std::string_view(value.text).substr(0, first)), units(std::string_view(value.text).substr(0, last))}; publish_(next); return true;
    }
    bool ExtendSelection(Point point, bool startHandle) override {
        auto hit = QueryTextInputPosition(input.session_id, point);
        auto next = value; auto range = next.selection.Range();
        next.selection = startHandle ? TextSelection{range.end, std::min(hit.position.offset, range.end)} : TextSelection{range.start, std::max(hit.position.offset, range.start)};
        publish_(next); return true;
    }
    std::optional<TextSelectionGeometry> QuerySelectionGeometry() const override {
        auto range = value.selection.Range();
        auto first = QueryTextInputGeometry(input.session_id, {range.start, range.start});
        auto last = QueryTextInputGeometry(input.session_id, {range.end, range.end});
        if (first.result_code != TextInputResultCode::Ok || last.result_code != TextInputResultCode::Ok) return TextSelectionGeometry{};
        return TextSelectionGeometry{first.caret, last.caret, first.caret};
    }
    Color SelectionHandleColor() const noexcept override { return Color::Rgb(112, 171, 255); }
    void EndTextInput(TextInputSessionId session, TextInputEndReason) override { if (session == input.session_id) { input.session_id = 0; value.composition.reset(); baseline.reset(); ++input.revision; } }
};
namespace {
struct EditorSurface {
    class Extension;
    std::shared_ptr<EditorState::Impl> model;
    bool operator==(const EditorSurface&) const = default;
};
class EditorSurface::Extension : public NodeExtension {
    std::shared_ptr<EditorState::Impl> model_;
    bool focused_ {};
    bool dragging_ {};
public:
    Extension(ViewNode&, const EditorSurface& spec) : model_(spec.model) {}
    void Update(ViewNode&, const EditorSurface& spec) { model_ = spec.model; InvalidatePaint(); }
    std::shared_ptr<TextInputClient> GetTextInputClient() noexcept override { return model_; }
    TextSelectionClient* GetTextSelectionClient() noexcept override { return model_.get(); }
    bool HitTest(ViewNode& node, Point point) const override { return point.x >= 0 && point.y >= 0 && point.x <= node.Bounds().width && point.y <= node.Bounds().height; }
    void OnFocusChanged(ViewNode&, bool focused, bool) override { focused_ = focused; InvalidatePaint(); }
    PointerResult OnPointer(ViewNode&, const PointerEvent& event) override {
        if (event.type == PointerEventType::Down || (event.type == PointerEventType::Move && dragging_)) {
            auto hit = model_->QueryTextInputPosition(model_->input.session_id, event.position);
            auto value = model_->value; if (!dragging_) value.selection.anchor = hit.position.offset; value.selection.active = hit.position.offset;
            dragging_ = true; model_->publish_(value); InvalidatePaint(); return PointerResult::Capture;
        }
        if (event.type == PointerEventType::Up || event.type == PointerEventType::Cancel) { dragging_ = false; return PointerResult::Handled; }
        return PointerResult::Ignored;
    }
    PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer& measurer) override {
        auto& model = *model_; model.viewport = node.LayoutSize();
        TextStyle style {.font = Font::Monospace(14), .foreground = Color::Rgb(218, 224, 235)};
        auto metrics = measurer.Metrics(style.font); model.ascent = metrics.ascent; model.lineHeight = std::max(22.0F, metrics.LineHeight() + 3);
        auto [row, x] = model.location_(model.value.selection.active);
        auto visible = static_cast<std::size_t>(std::max(1.0F, std::floor((model.viewport.height - 20) / model.lineHeight)));
        if (model.revealCaret) {
            if (row < model.topLine) model.topLine = row;
            else if (row >= model.topLine + visible) model.topLine = row - visible + 1;
            model.revealCaret = false;
        }
        auto last = std::min(model.lines.size(), model.topLine + visible + 2);
        for (auto i = model.topLine; i < last; ++i) {
            auto& line = model.lines[i]; auto& positions = model.positions[i];
            if (!positions.empty()) continue;
            positions.push_back({0, 0});
            auto count = units(line.text);
            const bool ascii = std::all_of(line.text.begin(), line.text.end(), [](unsigned char c) { return c >= 32 && c < 127; });
            const float cell = measurer.MeasureRun("M", style).advance;
            if (ascii) { for (TextOffset column = 1; column <= count; ++column) positions.push_back({column, column * cell}); continue; }
            for (TextOffset column = 1; column <= count; ++column) {
                try { auto prefix = AttributedText(line.text).TextInRange({0, column}); positions.push_back({column, measurer.MeasureRun(prefix, style).advance}); } catch (...) {}
            }
        }
        return PaintInvalidation::Both;
    }
    void PaintAboveContent(const ViewNode&, PaintContext& context) const override {
        const auto& model = *model_; auto visible = static_cast<std::size_t>(std::max(1.0F, std::ceil(model.viewport.height / model.lineHeight)));
        auto last = std::min(model.lines.size(), model.topLine + visible);
        context.DrawRect({0, 0, model.viewport.width, model.viewport.height}, Color::Rgb(19, 24, 34));
        context.DrawRect({0, 0, 50, model.viewport.height}, Color::Rgb(16, 21, 30));
        auto geometry = model.QueryTextInputGeometry(model.input.session_id, model.value.selection.Range());
        for (auto rectangle : geometry.range_rects) context.DrawRect(rectangle, Color::Rgb(42, 67, 105));
        TextStyle style {.font = Font::Monospace(14), .foreground = Color::Rgb(213, 221, 233)};
        for (auto i = model.topLine; i < last; ++i) {
            auto y = 10 + (i - model.topLine) * model.lineHeight;
            context.DrawText({8, static_cast<float>(y), 38, model.lineHeight}, std::to_string(i + 1), {.font = Font::Monospace(12), .foreground = Color::Rgb(100, 115, 137)});
            context.DrawText({64, static_cast<float>(y), std::max(0.0F, model.viewport.width - 72), model.lineHeight}, model.lines[i].text, style);
        }
        if (focused_) context.DrawRect(geometry.caret, Color::Rgb(112, 171, 255));
    }
};
}
EditorState::EditorState(std::shared_ptr<Application> application, std::string document, std::function<void()> changed) : impl_(std::make_shared<Impl>()) { impl_->application = std::move(application); impl_->document = std::move(document); impl_->changed = std::move(changed); impl_->refresh_(); }
void EditorState::refresh() { impl_->refresh_(); }
void EditorState::undo() { impl_->application->execute({{"command", "document.undo"}, {"params", {{"document", impl_->document}, {"revision", impl_->documentRevision}}}}); impl_->refresh_(); if (impl_->changed) impl_->changed(); }
void EditorState::redo() { impl_->application->execute({{"command", "document.redo"}, {"params", {{"document", impl_->document}, {"revision", impl_->documentRevision}}}}); impl_->refresh_(); if (impl_->changed) impl_->changed(); }
void EditorState::insert(std::string text) { auto next = impl_->value; impl_->replace_(next, next.selection.Range(), text); impl_->publish_(next); }
std::string EditorState::document_id() const { return impl_->document; }
TextSelection EditorState::selection() const { return impl_->value.selection; }
std::shared_ptr<TextInputClient> EditorState::text_input_client() const { return impl_; }
View code_editor(std::shared_ptr<EditorState> state) {
    return Canvas([](PaintContext&, Size) {}).With(Grow{1}, Frame{.min_width = 120, .min_height = 160}, Focusable{}, PointerCursor{PointerCursorKind::Text}, ClipChildren{}, EditorSurface{state->impl_})
        .On<ViewEvents::ScrollInput>([state](const ScrollInputEvent& event) {
            auto& model = *state->impl_;
            auto row = static_cast<std::int64_t>(model.topLine) + static_cast<std::int64_t>(std::round(event.delta_y / model.lineHeight));
            model.topLine = static_cast<std::size_t>(std::clamp<std::int64_t>(row, 0, model.lines.size() - 1));
            model.revealCaret = false;
            if (model.changed) model.changed();
            return true;
        });
}
}

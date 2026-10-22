// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.ui.editor;
import std;
import huxerui;
import meditor.application;
import meditor.document;
import meditor.syntax;

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
void normalize_value(TextEditingValue& value, std::optional<std::vector<LineEnding>>& endings, LineEnding preferred) {
    if (!endings || value.text.find('\r') == std::string::npos) return;
    std::vector<TextOffset> removed; std::vector<LineEnding> normalizedEndings; TextOffset offset {}; std::size_t line {};
    for (std::size_t i = 0; i < value.text.size(); ++i) {
        const auto c = static_cast<unsigned char>(value.text[i]);
        if (c == '\r') {
            if (i + 1 < value.text.size() && value.text[i + 1] == '\n') removed.push_back(offset);
            else { value.text[i] = '\n'; normalizedEndings.push_back(preferred); }
        } else if (c == '\n') normalizedEndings.push_back(endings->at(line++));
        if ((c & 0xc0) != 0x80) offset += c >= 0xf0 ? 2 : 1;
    }
    if (line != endings->size()) throw std::runtime_error("Invalid newline metadata");
    *endings = std::move(normalizedEndings);
    auto map = [&](TextOffset position) { return position - static_cast<TextOffset>(std::lower_bound(removed.begin(), removed.end(), position) - removed.begin()); };
    value.selection = {map(value.selection.anchor), map(value.selection.active)};
    if (value.composition) value.composition = TextRange{map(value.composition->start), map(value.composition->end)};
    value.text = normalize_line_endings(value.text, LineEnding::Lf);
}
struct Line { std::size_t byte {}; TextOffset unit {}; std::string text; };
}
struct EditorState::Impl : TextInputClient, TextSelectionClient {
    std::shared_ptr<Application> application;
    std::string document;
    std::function<void()> changed;
    TextEditingValue value;
    TextPresentation presentation;
    TextFormat format;
    struct Baseline { TextEditingValue value; std::vector<LineEnding> endings; bool bom {}; };
    std::optional<Baseline> baseline;
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
    syntax::Language language {syntax::Language::Plain};
    syntax::Cache syntaxCache;
    std::vector<TextRange> searchMatches;
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
        std::vector<std::string_view> sourceLines; sourceLines.reserve(lines.size());
        for (const auto& line : lines) sourceLines.push_back(line.text);
        syntaxCache.update(sourceLines, language);
        positions.resize(lines.size());
        for (std::size_t i = 0; i < lines.size() && i < oldLines.size(); ++i)
            if (lines[i].text == oldLines[i].text) positions[i] = std::move(oldPositions[i]);
    }
    void refresh_() {
        auto response = application->execute({{"command", "document.read"}, {"params", {{"document", document}}}});
        if (!response.value("ok", false)) return;
        auto data = response.at("data"); auto revision = data.at("revision").get<std::uint64_t>();
        if (revision == documentRevision) return;
        language = syntax::detect(data.at("path").get<std::string>());
        presentation = TextPresentation(data.at("text").get<std::string>());
        value.text = presentation.text(); format = presentation.format();
        format.lineEnding = data.at("format").at("lineEnding") == "crlf" ? LineEnding::CrLf : LineEnding::Lf;
        documentRevision = revision; searchMatches.clear();
        const auto length = units(value.text);
        auto clamp = [&](TextOffset offset) {
            offset = std::clamp<TextOffset>(offset, 0, length);
            try { byte_at(value.text, offset); } catch (const std::runtime_error&) { --offset; }
            return offset;
        };
        value.selection = {clamp(value.selection.anchor), clamp(value.selection.active)};
        value.composition.reset(); baseline.reset(); compositionGroup.clear();
        ++input.revision; ++input.content_revision; rebuild_();
    }
    std::vector<LineEnding> line_endings_() const { auto items = presentation.line_endings(); return {items.begin(), items.end()}; }
    bool publish_(TextEditingValue next, const std::string& historyGroup = {}, std::optional<std::vector<LineEnding>> endings = {}, std::optional<bool> bom = {}) {
        if (!valid_utf8(next.text)) return false;
        if (format.loneCarriageReturns) {
            if (next.text != value.text || next.composition || (endings && !std::ranges::equal(*endings, presentation.line_endings()))) return false;
        } else normalize_value(next, endings, format.lineEnding);
        const bool contentChanged = next.text != value.text || (endings && !std::ranges::equal(*endings, presentation.line_endings())) || (bom && *bom != format.utf8Bom);
        if (contentChanged) {
            if (!endings) return false;
            auto raw = encode_text_format(next.text, *endings, bom.value_or(format.utf8Bom));
            const auto& before = presentation.raw_text();
            std::size_t prefix {}, suffix {};
            while (prefix < before.size() && prefix < raw.size() && before[prefix] == raw[prefix]) ++prefix;
            while (prefix && prefix < before.size() && (static_cast<unsigned char>(before[prefix]) & 0xc0) == 0x80) --prefix;
            while (suffix < before.size() - prefix && suffix < raw.size() - prefix && before[before.size() - suffix - 1] == raw[raw.size() - suffix - 1]) ++suffix;
            while (suffix && before.size() > suffix && (static_cast<unsigned char>(before[before.size() - suffix]) & 0xc0) == 0x80) --suffix;
            const auto inserted = raw.substr(prefix, raw.size() - prefix - suffix);
            TextPresentation prepared(std::move(raw));
            if (next.text.starts_with("\xef\xbb\xbf") && prepared.text() == next.text.substr(3)) {
                next.text.erase(0, 3);
                auto map = [](TextOffset position) { return std::max<TextOffset>(0, position - 1); };
                next.selection = {map(next.selection.anchor), map(next.selection.active)};
                if (next.composition) next.composition = TextRange{map(next.composition->start), map(next.composition->end)};
            }
            auto reply = application->execute({{"command", "document.edit"}, {"params", {{"document", document}, {"revision", documentRevision}, {"historyGroup", historyGroup}, {"edits", Json::array({{{"start", prefix}, {"end", before.size() - suffix}, {"text", inserted}}})}}}});
            if (!reply.value("ok", false)) { refresh_(); return false; }
            documentRevision = reply["data"]["revision"]; searchMatches.clear(); ++input.content_revision;
            presentation = std::move(prepared); format = presentation.format();
            format.lineEnding = reply["data"]["format"]["lineEnding"] == "crlf" ? LineEnding::CrLf : LineEnding::Lf;
        }
        revealCaret = revealCaret || next.text != value.text || next.selection != value.selection;
        value = std::move(next); ++input.revision; if (contentChanged) rebuild_();
        if (changed) changed();
        return true;
    }
    void replace_(TextEditingValue& next, TextRange range, const std::string& text, std::vector<LineEnding>& endings, std::span<const LineEnding> preserved = {}) {
        auto start = byte_at(next.text, range.start), end = byte_at(next.text, range.end);
        if (start > end) throw std::runtime_error("Invalid range");
        const auto first = static_cast<std::size_t>(std::count(next.text.begin(), next.text.begin() + start, '\n'));
        const auto removed = static_cast<std::size_t>(std::count(next.text.begin() + start, next.text.begin() + end, '\n'));
        auto inserted = text;
        for (std::size_t i = 0; i < inserted.size(); ++i) if (inserted[i] == '\r' && (i + 1 == inserted.size() || inserted[i + 1] != '\n')) inserted[i] = '\n';
        const auto added = static_cast<std::size_t>(std::count(inserted.begin(), inserted.end(), '\n'));
        endings.erase(endings.begin() + first, endings.begin() + first + removed);
        if (preserved.empty()) endings.insert(endings.begin() + first, added, format.lineEnding);
        else {
            if (preserved.size() != added) throw std::runtime_error("Invalid newline metadata");
            endings.insert(endings.begin() + first, preserved.begin(), preserved.end());
        }
        next.text.replace(start, end - start, inserted); auto caret = range.start + units(inserted); next.selection = {caret, caret};
    }
    TextInputConfiguration Configuration() const override { TextInputConfiguration config; config.multiline = true; config.read_only = format.loneCarriageReturns; return config; }
    TextInputState State() const override { auto result = input; result.selection = value.selection; result.composition = value.composition; return result; }
    TextInputState BeginTextInput(TextInputSessionId session) override { input.session_id = session; ++input.revision; return State(); }
    TextInputApplyResult ApplyTextInput(const TextInputCommandBatch& batch) override {
        if (batch.session_id != input.session_id) return {TextInputResultCode::SessionMismatch};
        if (batch.commands.empty()) return {TextInputResultCode::Rejected};
        auto next = value; auto original = baseline; auto endings = line_endings_(); bool bom = format.utf8Bom;
        try {
            for (const auto& command : batch.commands) {
                if (format.loneCarriageReturns && command.kind != TextInputCommandKind::SetSelection) return {TextInputResultCode::Rejected};
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
                    original = Baseline{next, endings, bom}; next.composition = range; break;
                case TextInputCommandKind::UpdateComposition: {
                    if (!original) original = Baseline{next, endings, bom};
                    auto old = next.composition; replace_(next, range, command.text, endings);
                    next.composition = old ? TextRange{old->start, old->end + units(command.text) - range.Length()} : TextRange{range.start, range.start + units(command.text)};
                    break;
                }
                case TextInputCommandKind::CommitText:
                    replace_(next, range, command.text, endings); next.composition.reset(); original.reset(); break;
                case TextInputCommandKind::FinishComposition: next.composition.reset(); original.reset(); break;
                case TextInputCommandKind::CancelComposition:
                    if (original) { next = original->value; endings = original->endings; bom = original->bom; } next.composition.reset(); original.reset(); break;
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
                    auto firstEnding = static_cast<std::size_t>(std::count(next.text.begin(), next.text.begin() + byte_at(next.text, selected.start), '\n'));
                    auto count = static_cast<std::size_t>(std::count(selectedText.begin(), selectedText.end(), '\n'));
                    std::vector<LineEnding> selectedEndings(endings.begin() + firstEnding, endings.begin() + firstEnding + count);
                    replace_(next, {a, b}, selectedText, endings, selectedEndings); next.selection = {a, a + selected.Length()}; next.composition.reset(); original.reset(); break;
                }
                }
                if (command.selection_after) next.selection = *command.selection_after;
                byte_at(next.text, next.selection.anchor); byte_at(next.text, next.selection.active);
                if (next.composition) { byte_at(next.text, next.composition->start); byte_at(next.text, next.composition->end); }
            }
            if ((original || baseline) && compositionGroup.empty()) compositionGroup = compositionPrefix + std::to_string(++compositionSequence);
            if (!publish_(next, compositionGroup, std::move(endings), bom)) return {TextInputResultCode::Rejected};
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
        auto next = value; auto endings = line_endings_(); auto selection = next.selection.Range(); auto caret = next.selection.active;
        auto move = [&](TextOffset target) { if (!event.modifiers.shift) next.selection.anchor = target; next.selection.active = target; };
        auto previous = [&](TextOffset offset) { if (!offset) return offset; auto byte = byte_at(next.text, offset); --byte; while (byte && (static_cast<unsigned char>(next.text[byte]) & 0xc0) == 0x80) --byte; return units(std::string_view(next.text).substr(0, byte)); };
        auto following = [&](TextOffset offset) { auto byte = byte_at(next.text, offset); if (byte == next.text.size()) return offset; ++byte; while (byte < next.text.size() && (static_cast<unsigned char>(next.text[byte]) & 0xc0) == 0x80) ++byte; return units(std::string_view(next.text).substr(0, byte)); };
        if (event.key == Key::ArrowLeft) move(previous(caret));
        else if (event.key == Key::ArrowRight) move(following(caret));
        else if (event.key == Key::Home) { auto [row, x] = location_(caret); move(lines[row].unit); }
        else if (event.key == Key::End) { auto [row, x] = location_(caret); move(lines[row].unit + units(lines[row].text)); }
        else if (event.key == Key::ArrowUp || event.key == Key::ArrowDown) { auto [row, x] = location_(caret); auto target = event.key == Key::ArrowUp ? (row ? row - 1 : 0) : std::min(row + 1, lines.size() - 1); auto hit = QueryTextInputPosition(input.session_id, {64 + x, 10 + (static_cast<float>(target) - topLine) * lineHeight + 1}); move(hit.position.offset); }
        else if (event.key == Key::Backspace) replace_(next, selection.IsCollapsed() ? TextRange{previous(caret), caret} : selection, "", endings);
        else if (event.key == Key::Delete) replace_(next, selection.IsCollapsed() ? TextRange{caret, following(caret)} : selection, "", endings);
        else if (event.key == Key::Enter) replace_(next, selection, "\n", endings);
        else if (event.key == Key::Tab) replace_(next, selection, "    ", endings);
        else return TextInputKeyResult::Unhandled;
        publish_(next, {}, std::move(endings)); return TextInputKeyResult::Handled;
    }
    bool CanPerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) const override {
        if (value.composition || (format.loneCarriageReturns && (action == TextEditingAction::Cut || action == TextEditingAction::Paste))) return false;
        if (action == TextEditingAction::SelectAll) return !value.text.empty();
        return clipboard && (action == TextEditingAction::Paste || !value.selection.IsCollapsed());
    }
    bool PerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) override {
        if (!CanPerformTextEditingAction(action, clipboard)) return false;
        auto next = value; auto endings = line_endings_();
        if (action == TextEditingAction::SelectAll) next.selection = {0, units(next.text)};
        else if (action == TextEditingAction::Paste) {
            auto text = clipboard->ReadText(); if (!text) return false;
            replace_(next, next.selection.Range(), *text, endings);
        } else {
            auto range = next.selection.Range();
            auto text = next.text.substr(byte_at(next.text, range.start), byte_at(next.text, range.end) - byte_at(next.text, range.start));
            if (!clipboard->WriteText(text)) return false;
            if (action == TextEditingAction::Copy) return true;
            replace_(next, range, "", endings);
        }
        return publish_(next, {}, std::move(endings));
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
    void EndTextInput(TextInputSessionId session, TextInputEndReason) override { if (session == input.session_id) { input.session_id = 0; value.composition.reset(); baseline.reset(); compositionGroup.clear(); ++input.revision; } }
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
        context.DrawRect({0, 0, model.viewport.width, model.viewport.height}, model.dark ? Color::Rgb(19, 24, 34) : Color::Rgb(250, 252, 255));
        context.DrawRect({0, 0, 50, model.viewport.height}, model.dark ? Color::Rgb(16, 21, 30) : Color::Rgb(238, 242, 248));
        for (auto i = model.topLine; i < last; ++i) {
            const auto& line = model.lines[i]; const auto lineEnd = line.unit + units(line.text);
            auto x = [&](TextOffset unit) {
                const auto& positions = model.positions[i]; auto position = std::lower_bound(positions.begin(), positions.end(), unit - line.unit, [](auto item, auto offset) { return item.first < offset; });
                return position == positions.end() ? (positions.empty() ? 0.0F : positions.back().second) : position->second;
            };
            for (const auto& match : model.searchMatches) {
                if (match.end <= line.unit) continue;
                if (match.start > lineEnd) break;
                const auto start = std::max(match.start, line.unit), end = std::min(match.end, lineEnd);
                if (start < end || (match.start <= lineEnd && match.end > lineEnd)) {
                    const auto left = x(start), right = start < end ? x(end) : left + 6;
                    context.DrawRect({64 + left, 10 + (i - model.topLine) * model.lineHeight, std::max(1.0F, right - left), model.lineHeight}, model.dark ? Color::Rgb(58, 45, 18) : Color::Rgb(249, 230, 169));
                }
            }
        }
        auto geometry = model.QueryTextInputGeometry(model.input.session_id, model.value.selection.Range());
        for (auto rectangle : geometry.range_rects) context.DrawRect(rectangle, model.dark ? Color::Rgb(31, 49, 76) : Color::Rgb(214, 233, 255));
        TextStyle style {.font = Font::Monospace(14), .foreground = model.dark ? Color::Rgb(213, 221, 233) : Color::Rgb(35, 47, 65)};
        for (auto i = model.topLine; i < last; ++i) {
            auto y = 10 + (i - model.topLine) * model.lineHeight;
            context.DrawText({8, static_cast<float>(y), 38, model.lineHeight}, std::to_string(i + 1), {.font = Font::Monospace(12), .foreground = Color::Rgb(100, 115, 137)});
            const auto& text = model.lines[i].text;
            std::vector<TextStyleRange> ranges;
            std::size_t byte {}; TextOffset offset {};
            for (const auto& token : model.syntaxCache.tokens(i)) {
                offset += units(std::string_view(text).substr(byte, token.start - byte));
                auto end = offset + units(std::string_view(text).substr(token.start, token.end - token.start));
                Color color;
                switch (token.role) {
                case syntax::Role::Keyword: color = model.dark ? Color::Rgb(197, 157, 255) : Color::Rgb(112, 48, 160); break;
                case syntax::Role::Comment: color = model.dark ? Color::Rgb(145, 165, 144) : Color::Rgb(66, 112, 62); break;
                case syntax::Role::String: color = model.dark ? Color::Rgb(226, 176, 139) : Color::Rgb(151, 64, 29); break;
                case syntax::Role::Emphasis: color = model.dark ? Color::Rgb(228, 185, 243) : Color::Rgb(132, 53, 148); break;
                case syntax::Role::Code: color = model.dark ? Color::Rgb(226, 176, 139) : Color::Rgb(151, 64, 29); break;
                case syntax::Role::Number: color = model.dark ? Color::Rgb(151, 210, 188) : Color::Rgb(26, 112, 91); break;
                case syntax::Role::Key:
                case syntax::Role::Link:
                case syntax::Role::Heading:
                case syntax::Role::Marker:
                case syntax::Role::Directive: color = model.dark ? Color::Rgb(127, 188, 240) : Color::Rgb(33, 98, 161); break;
                }
                ranges.push_back({{offset, end}, {.foreground = color}});
                offset = end; byte = token.end;
            }
            // One attributed paragraph preserves shaping, selection and UTF-16 geometry.
            context.DrawText({64, static_cast<float>(y), std::max(0.0F, model.viewport.width - 72), model.lineHeight}, AttributedText::FromRanges(text, std::move(ranges)), style);
        }
        if (focused_) context.DrawRect(geometry.caret, Color::Rgb(112, 171, 255));
    }
};
}
EditorState::EditorState(std::shared_ptr<Application> application, std::string document, std::function<void()> changed) : impl_(std::make_shared<Impl>()) { impl_->application = std::move(application); impl_->document = std::move(document); impl_->changed = std::move(changed); impl_->refresh_(); }
void EditorState::refresh() { impl_->refresh_(); }
void EditorState::undo() { impl_->application->execute({{"command", "document.undo"}, {"params", {{"document", impl_->document}, {"revision", impl_->documentRevision}}}}); impl_->refresh_(); if (impl_->changed) impl_->changed(); }
void EditorState::redo() { impl_->application->execute({{"command", "document.redo"}, {"params", {{"document", impl_->document}, {"revision", impl_->documentRevision}}}}); impl_->refresh_(); if (impl_->changed) impl_->changed(); }
void EditorState::insert(std::string text) { auto next = impl_->value; auto endings = impl_->line_endings_(); impl_->replace_(next, next.selection.Range(), text, endings); impl_->publish_(next, {}, std::move(endings)); }
std::uint64_t EditorState::revision() const { return impl_->documentRevision; }
std::pair<std::size_t, std::size_t> EditorState::selection_bytes() const { auto range = impl_->value.selection.Range(); return {byte_at(impl_->value.text, range.start), byte_at(impl_->value.text, range.end)}; }
bool EditorState::select_match(TextSelection selection, std::uint64_t revision) {
    if (revision != impl_->documentRevision) return false;
    try { byte_at(impl_->value.text, selection.anchor); byte_at(impl_->value.text, selection.active); } catch (...) { return false; }
    auto next = impl_->value; next.selection = selection; return impl_->publish_(std::move(next));
}
bool EditorState::select_byte_range(std::size_t start, std::size_t end, std::uint64_t revision) {
    if (revision != impl_->documentRevision) return false;
    try { impl_->presentation.raw_offset(start); impl_->presentation.raw_offset(end); } catch (...) { return false; }
    return select_match({units(std::string_view(impl_->value.text).substr(0, start)), units(std::string_view(impl_->value.text).substr(0, end))}, revision);
}
bool EditorState::set_search_results(std::vector<TextRange> matches, std::uint64_t revision) {
    if (revision != impl_->documentRevision) return false;
    TextOffset end {};
    try { for (auto range : matches) { if (range.start < end || range.end <= range.start) return false; byte_at(impl_->value.text, range.start); byte_at(impl_->value.text, range.end); end = range.end; } } catch (...) { return false; }
    impl_->searchMatches = std::move(matches); if (impl_->changed) impl_->changed(); return true;
}
void EditorState::clear_search_results() { impl_->searchMatches.clear(); if (impl_->changed) impl_->changed(); }
TextFormat EditorState::text_format() const { return impl_->format; }
std::string EditorState::language_id() const { return std::string(syntax::language_name(impl_->language)); }
bool EditorState::highlighting_limited() const { return impl_->syntaxCache.limited(); }
std::string EditorState::document_id() const { return impl_->document; }
TextSelection EditorState::selection() const { return impl_->value.selection; }
std::shared_ptr<TextInputClient> EditorState::text_input_client() const { return impl_; }
View code_editor(std::shared_ptr<EditorState> state, bool dark) {
    state->impl_->dark = dark;
    return Canvas([](PaintContext&, Size) {}).Key("code-editor").With(Grow{1}, Frame{.min_width = 120, .min_height = 160}, Focusable{}, PointerCursor{PointerCursorKind::Text}, ClipChildren{}, EditorSurface{state->impl_})
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

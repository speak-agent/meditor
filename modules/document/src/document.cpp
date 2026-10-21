module meditor.document;
import std;

namespace meditor {
namespace {
std::pair<char32_t, std::size_t> decode(std::string_view text, std::size_t at) {
    if (at >= text.size()) throw std::runtime_error("Position is outside the document");
    auto lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) return {lead, 1};
    std::size_t count = lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    if (!count || at + count > text.size()) throw std::runtime_error("Invalid UTF-8");
    char32_t point = lead & ((1u << (7 - count)) - 1);
    for (std::size_t i = 1; i < count; ++i) {
        auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xc0) != 0x80) throw std::runtime_error("Invalid UTF-8");
        point = (point << 6) | (byte & 0x3f);
    }
    if ((count == 2 && point < 0x80) || (count == 3 && point < 0x800) || (count == 4 && point < 0x10000) || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) throw std::runtime_error("Invalid UTF-8");
    return {point, count};
}
bool boundary(std::string_view text, std::size_t offset) {
    return offset <= text.size() && (offset == text.size() || (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
}
}
bool valid_utf8(std::string_view text) {
    try { for (std::size_t i = 0; i < text.size();) i += decode(text, i).second; return true; }
    catch (...) { return false; }
}
Document::Document(std::string text) : original_(std::move(text)), saved_(original_) {
    if (!valid_utf8(original_)) throw std::runtime_error("Only valid UTF-8 documents can be edited");
    if (!original_.empty()) pieces_.push_back({false, 0, original_.size()});
}
std::string Document::text() const {
    std::string result; result.reserve(size());
    for (auto piece : pieces_) result.append(piece.added ? added_ : original_, piece.offset, piece.length);
    return result;
}
std::uint64_t Document::revision() const noexcept { return revision_; }
bool Document::dirty() const { return text() != saved_; }
std::size_t Document::size() const noexcept {
    std::size_t result {}; for (auto piece : pieces_) result += piece.length; return result;
}
void Document::check_revision_(std::uint64_t revision) const {
    if (revision != revision_) throw std::runtime_error("revision-conflict");
}
void Document::apply(std::vector<Edit> edits, std::uint64_t expectedRevision, std::string historyGroup) {
    check_revision_(expectedRevision);
    const auto before = text();
    std::stable_sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start < b.start; });
    std::size_t previousEnd {};
    bool first = true;
    for (const auto& edit : edits) {
        if (edit.start > edit.end || !boundary(before, edit.start) || !boundary(before, edit.end) || (!first && edit.start < previousEnd) || !valid_utf8(edit.text)) throw std::runtime_error("Invalid or overlapping edit ranges");
        previousEnd = edit.end; first = false;
    }
    if (edits.empty()) return;
    auto next = pieces_;
    auto added = added_;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        std::vector<Piece> result;
        std::size_t cursor {};
        for (auto piece : next) {
            auto end = cursor + piece.length;
            if (cursor < it->start) {
                auto length = std::min(end, it->start) - cursor;
                if (length) result.push_back({piece.added, piece.offset, length});
            }
            cursor = end;
        }
        if (!it->text.empty()) { result.push_back({true, added.size(), it->text.size()}); added += it->text; }
        cursor = 0;
        for (auto piece : next) {
            auto end = cursor + piece.length;
            if (end > it->end) {
                auto skip = it->end > cursor ? it->end - cursor : 0;
                result.push_back({piece.added, piece.offset + skip, piece.length - skip});
            }
            cursor = end;
        }
        next = std::move(result);
    }
    if (historyGroup.empty() || historyGroup != historyGroup_ || undo_.empty()) undo_.push_back({pieces_});
    historyGroup_ = std::move(historyGroup);
    redo_.clear(); added_ = std::move(added); pieces_ = std::move(next); ++revision_;
}
bool Document::undo(std::uint64_t expectedRevision) {
    check_revision_(expectedRevision); historyGroup_.clear(); if (undo_.empty()) return false;
    redo_.push_back({pieces_}); pieces_ = std::move(undo_.back().pieces); undo_.pop_back(); ++revision_; return true;
}
bool Document::redo(std::uint64_t expectedRevision) {
    check_revision_(expectedRevision); historyGroup_.clear(); if (redo_.empty()) return false;
    undo_.push_back({pieces_}); pieces_ = std::move(redo_.back().pieces); redo_.pop_back(); ++revision_; return true;
}
void Document::mark_saved(std::string text) { saved_ = std::move(text); }
const std::string& Document::saved_text() const noexcept { return saved_; }
std::size_t Document::offset_at(Position position) const {
    const auto value = text(); std::size_t line {}, column {}, i {};
    while (i < value.size()) {
        if (line == position.line && column == position.character) return i;
        auto [point, count] = decode(value, i);
        if (point == '\n') { if (line == position.line) break; ++line; column = 0; }
        else column += point > 0xffff ? 2 : 1;
        i += count;
    }
    if (line == position.line && column == position.character) return i;
    throw std::runtime_error("Invalid UTF-16 document position");
}
Position Document::position_at(std::size_t offset) const {
    const auto value = text();
    if (!boundary(value, offset)) throw std::runtime_error("Invalid UTF-8 byte boundary");
    Position result;
    for (std::size_t i = 0; i < offset;) {
        auto [point, count] = decode(value, i); i += count;
        if (point == '\n') { ++result.line; result.character = 0; }
        else result.character += point > 0xffff ? 2 : 1;
    }
    return result;
}
}

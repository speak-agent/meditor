// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.document;
export import meditor.syntax;
import std;

export namespace meditor {
inline constexpr std::size_t MAX_DOCUMENT_BYTES {32 * 1024 * 1024};
enum class LineEnding { Lf, CrLf };
struct TextFormat {
    bool utf8Bom {};
    LineEnding lineEnding {LineEnding::Lf};
    bool mixedLineEndings {};
    bool loneCarriageReturns {};
    std::size_t lineBreaks {};
};
TextFormat inspect_text_format(std::string_view text);
std::string normalize_line_endings(std::string_view text, LineEnding ending);
std::string encode_text_format(std::string_view display, std::span<const LineEnding> endings, bool utf8Bom);
// Immutable raw/display snapshot. GUI offsets address BOM-free, LF display bytes;
// document and CLI offsets continue to address the original UTF-8 file bytes.
class TextPresentation {
    std::string raw_, text_;
    std::vector<std::size_t> crlf_;
    std::vector<LineEnding> endings_;
    TextFormat format_;
public:
    explicit TextPresentation(std::string raw = {});
    const std::string& raw_text() const noexcept;
    const std::string& text() const noexcept;
    TextFormat format() const noexcept;
    std::span<const LineEnding> line_endings() const noexcept;
    std::size_t raw_offset(std::size_t displayByte) const;
    // Rejects UTF-8 continuation bytes and the interior of a CRLF pair.
    // Raw offset zero and the boundary after a hidden BOM both map to zero.
    std::size_t display_offset(std::size_t rawByte) const;
};
struct TextMatch {
    std::size_t start {}, end {}, utf16Start {}, utf16End {};
    bool operator==(const TextMatch&) const = default;
};
struct TextSearch { std::vector<TextMatch> matches; std::size_t total {}; bool truncated {}; };
struct TextReplacement { std::string text; std::size_t count {}; };
TextSearch find_text(std::string_view text, std::string_view query, std::size_t start = 0, std::size_t limit = 1000, bool backward = false);
TextReplacement replace_all_text(std::string_view text, std::string_view query, std::string_view replacement);
TextReplacement replace_all_display(const TextPresentation& text, std::string_view query, std::string_view replacement, LineEnding preferred);
struct Position {
    std::size_t line {};
    std::size_t character {};
};
// UTF-16 coordinates in the supplied UTF-8 text, including any BOM and CR.
std::size_t utf8_offset_at(std::string_view text, Position position);
struct Edit {
    std::size_t start {};
    std::size_t end {};
    std::string text;
};
class Document {
    struct Piece { bool added {}; std::size_t offset {}; std::size_t length {}; };
    struct Snapshot { std::vector<Piece> pieces; };
    std::string original_;
    std::string added_;
    std::string saved_;
    std::vector<Piece> pieces_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::uint64_t revision_ {1};
    std::string historyGroup_;
    void check_revision_(std::uint64_t revision) const;
public:
    explicit Document(std::string text = {});
    std::string text() const;
    std::uint64_t revision() const noexcept;
    bool dirty() const;
    std::size_t size() const noexcept;
    void apply(std::vector<Edit> edits, std::uint64_t expectedRevision, std::string historyGroup = {});
    bool undo(std::uint64_t expectedRevision);
    bool redo(std::uint64_t expectedRevision);
    void mark_saved(std::string savedText);
    const std::string& saved_text() const noexcept;
    std::size_t offset_at(Position position) const;
    Position position_at(std::size_t offset) const;
};
bool valid_utf8(std::string_view text);
}

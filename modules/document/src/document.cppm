// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.document;
import std;

export namespace meditor {
struct Position {
    std::size_t line {};
    std::size_t character {};
};
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

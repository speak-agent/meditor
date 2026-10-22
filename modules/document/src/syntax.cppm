// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.syntax;
import std;

export namespace meditor::syntax {
enum class Language { Plain, Cpp };
enum class Role { Keyword, Comment, String, Number, Directive };
// Byte ranges in a single UTF-8 line. Text is never modified by highlighting.
struct Token {
    std::size_t start {}, end {};
    Role role {};
    bool operator==(const Token&) const = default;
};
struct State {
    enum class Mode { Normal, Comment, Raw, String, Character, LineComment };
    Mode mode {Mode::Normal};
    std::string rawEnd;
    bool operator==(const State&) const = default;
};
struct Line {
    std::string text;
    State before, after;
    std::vector<Token> tokens;
};
Language detect(std::string_view fileName);
// Incremental at line boundaries; unchanged text/state reuses the previous result.
// Above the byte budget, every line remains plain (limited() is true).
class Cache {
    std::vector<Line> lines_;
    Language language_ {Language::Plain};
    bool limited_ {};
public:
    static constexpr std::size_t MAX_BYTES {2 * 1024 * 1024};
    static constexpr std::size_t MAX_LINE_BYTES {16 * 1024};
    static constexpr std::size_t MAX_TOKENS {65536};
    void update(std::span<const std::string_view> lines, Language language);
    std::span<const Token> tokens(std::size_t line) const;
    bool limited() const noexcept;
};
}

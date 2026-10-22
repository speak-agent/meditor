// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.syntax;
import std;

namespace meditor::syntax {
namespace {
bool alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 128; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool keyword(std::string_view word) {
    static constexpr std::string_view WORDS {" alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq module import "};
    return WORDS.find(" " + std::string(word) + " ") != std::string_view::npos;
}
void scan(Line& line) {
    using Mode = State::Mode;
    auto& state = line.after;
    const std::string_view text {line.text};
    std::size_t i {};
    auto emit = [&](std::size_t start, Role role) { if (i > start) line.tokens.push_back({start, i, role}); };
    while (i < text.size()) {
        auto start = i;
        if (state.mode == Mode::Comment) {
            auto end = text.find("*/", i);
            i = end == text.npos ? text.size() : end + 2;
            if (end != text.npos) state.mode = Mode::Normal;
            emit(start, Role::Comment); continue;
        }
        if (state.mode == Mode::LineComment) {
            i = text.size(); state.mode = text.ends_with('\\') ? Mode::LineComment : Mode::Normal;
            emit(start, Role::Comment); continue;
        }
        if (state.mode == Mode::Raw) {
            auto end = text.find(state.rawEnd, i);
            i = end == text.npos ? text.size() : end + state.rawEnd.size();
            if (end != text.npos) { state.mode = Mode::Normal; state.rawEnd.clear(); }
            emit(start, Role::String); continue;
        }
        if (state.mode == Mode::String || state.mode == Mode::Character) {
            auto quote = state.mode == Mode::String ? '"' : '\'';
            bool continued {};
            while (i < text.size()) {
                if (text[i] == '\\') { ++i; if (i == text.size()) { continued = true; break; } ++i; }
                else if (text[i++] == quote) { state.mode = Mode::Normal; break; }
            }
            if (!continued) state.mode = Mode::Normal;
            emit(start, Role::String); continue;
        }
        if (text.substr(i).starts_with("//")) { state.mode = Mode::LineComment; continue; }
        if (text.substr(i).starts_with("/*")) {
            i += 2; state.mode = Mode::Comment;
            auto end = text.find("*/", i); i = end == text.npos ? text.size() : end + 2;
            if (end != text.npos) state.mode = Mode::Normal;
            emit(start, Role::Comment); continue;
        }
        // Raw delimiters are at most 16 characters; quoted prefixes are part of the token.
        bool raw {};
        for (auto prefix : {"u8R\"", "uR\"", "UR\"", "LR\"", "R\""}) {
            if (!text.substr(i).starts_with(prefix)) continue;
            auto delimiter = i + std::string_view(prefix).size();
            auto relative = text.substr(delimiter, 17).find('(');
            auto open = relative == text.npos ? text.npos : delimiter + relative;
            if (open == text.npos || open - delimiter > 16) break;
            auto value = text.substr(delimiter, open - delimiter);
            if (value.find_first_of(" ()\\\t\v\f\r") != text.npos) break;
            state.rawEnd = ")" + std::string(value) + "\""; state.mode = Mode::Raw;
            auto end = text.find(state.rawEnd, open + 1);
            i = end == text.npos ? text.size() : end + state.rawEnd.size();
            if (end != text.npos) { state.mode = Mode::Normal; state.rawEnd.clear(); }
            emit(start, Role::String); raw = true; break;
        }
        if (raw) continue;
        std::size_t prefix {};
        for (auto candidate : {"u8", "u", "U", "L"}) {
            auto size = std::string_view(candidate).size();
            if (text.substr(i).starts_with(candidate) && i + size < text.size() && (text[i + size] == '"' || text[i + size] == '\'')) { prefix = size; break; }
        }
        if (text[i + prefix] == '"' || text[i + prefix] == '\'') {
            auto quote = text[i + prefix]; i += prefix + 1;
            state.mode = quote == '"' ? Mode::String : Mode::Character;
            bool continued {};
            while (i < text.size()) {
                if (text[i] == '\\') { ++i; if (i == text.size()) { continued = true; break; } ++i; }
                else if (text[i++] == quote) { state.mode = Mode::Normal; break; }
            }
            if (!continued) state.mode = Mode::Normal;
            emit(start, Role::String); continue;
        }
        if (text[i] == '#' && text.substr(0, i).find_first_not_of(" \t") == text.npos) {
            ++i; while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
            while (i < text.size() && alpha(text[i])) ++i;
            emit(start, Role::Directive); continue;
        }
        if (digit(text[i]) || (text[i] == '.' && i + 1 < text.size() && digit(text[i + 1]))) {
            ++i;
            while (i < text.size()) {
                auto c = text[i];
                if (alpha(c) || digit(c) || c == '.' || c == '\'') ++i;
                else if ((c == '+' || c == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E' || text[i - 1] == 'p' || text[i - 1] == 'P')) ++i;
                else break;
            }
            emit(start, Role::Number); continue;
        }
        if (alpha(text[i])) {
            ++i; while (i < text.size() && (alpha(text[i]) || digit(text[i]))) ++i;
            if (keyword(text.substr(start, i - start))) emit(start, Role::Keyword);
            continue;
        }
        ++i;
    }
    // An empty continuation line terminates a spliced string or line comment.
    if (text.empty() && (state.mode == Mode::String || state.mode == Mode::Character || state.mode == Mode::LineComment)) state.mode = Mode::Normal;
}
}
Language detect(std::string_view fileName) {
    auto dot = fileName.rfind('.');
    if (dot == fileName.npos) return Language::Plain;
    auto extension = std::string(fileName.substr(dot));
    for (auto& c : extension) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    for (auto known : {".cppm", ".ixx", ".mpp", ".cpp", ".cc", ".cxx", ".c", ".h", ".hh", ".hpp", ".hxx"})
        if (extension == known) return Language::Cpp;
    return Language::Plain;
}
void Cache::update(std::span<const std::string_view> lines, Language language) {
    std::size_t bytes {}; limited_ = false;
    for (auto line : lines) {
        if (line.size() > MAX_LINE_BYTES || line.size() >= MAX_BYTES - bytes) { limited_ = true; break; }
        bytes += line.size() + 1;
    }
    if (limited_ || language == Language::Plain) { lines_.clear(); language_ = language; return; }
    auto old = std::move(lines_); lines_.clear(); lines_.reserve(lines.size()); State state;
    std::size_t tokenCount {};
    for (std::size_t row = 0; row < lines.size(); ++row) {
        if (language == language_ && row < old.size() && old[row].text == lines[row] && old[row].before == state) lines_.push_back(std::move(old[row]));
        else { Line line {.text = std::string(lines[row]), .before = state, .after = state}; scan(line); lines_.push_back(std::move(line)); }
        tokenCount += lines_.back().tokens.size();
        if (tokenCount > MAX_TOKENS) { lines_.clear(); limited_ = true; language_ = language; return; }
        state = lines_.back().after;
    }
    language_ = language;
}
std::span<const Token> Cache::tokens(std::size_t line) const { return line < lines_.size() ? std::span<const Token>(lines_[line].tokens) : std::span<const Token>{}; }
bool Cache::limited() const noexcept { return limited_; }
}

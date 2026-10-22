// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.syntax;
import std;

namespace meditor::syntax::detail {
namespace {
bool space(char c) { return c == ' ' || c == '\t'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 128; }
std::size_t after_space(std::string_view text, std::size_t i) { while (i < text.size() && space(text[i])) ++i; return i; }
std::size_t assignment(std::string_view text) {
    char quote {};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (quote) {
            if (quote == '"' && text[i] == '\\') { if (i + 1 < text.size()) ++i; }
            else if (text[i] == quote) quote = 0;
        } else if (text[i] == '"' || text[i] == '\'') quote = text[i];
        else if (text[i] == '#') break;
        else if (text[i] == '=') return i;
    }
    return text.npos;
}
}
void scan_config(Line& line, Language language) {
    using Mode = State::Mode;
    const std::string_view text {line.text}; auto& state = line.after;
    const bool toml = language == Language::Toml, comments = language == Language::Jsonc;
    const auto equal = toml && state.mode == Mode::Normal ? assignment(text) : text.npos;
    std::size_t i {};
    auto emit = [&](std::size_t start, Role role) { if (i > start) line.tokens.push_back({start, i, role}); };
    while (i < text.size()) {
        auto start = i;
        if (comments && state.mode == Mode::Comment) {
            auto end = text.find("*/", i); i = end == text.npos ? text.size() : end + 2;
            if (end != text.npos) state.mode = Mode::Normal;
            emit(start, Role::Comment); continue;
        }
        if (toml && (state.mode == Mode::BasicMulti || state.mode == Mode::LiteralMulti)) {
            const bool basic = state.mode == Mode::BasicMulti;
            const auto closing = basic ? "\"\"\"" : "'''";
            while (i < text.size()) {
                if (basic && text[i] == '\\') { i += std::min<std::size_t>(2, text.size() - i); continue; }
                if (text.substr(i).starts_with(closing)) { i += 3; state.mode = Mode::Normal; break; }
                ++i;
            }
            emit(start, Role::String); continue;
        }
        if (toml && text[i] == '#') { i = text.size(); emit(start, Role::Comment); continue; }
        if (comments && text.substr(i).starts_with("//")) { i = text.size(); emit(start, Role::Comment); continue; }
        if (comments && text.substr(i).starts_with("/*")) {
            auto end = text.find("*/", i + 2); i = end == text.npos ? text.size() : end + 2;
            if (end == text.npos) state.mode = Mode::Comment;
            emit(start, Role::Comment); continue;
        }
        if (toml && text[i] == '[' && text.substr(0, i).find_first_not_of(" \t") == text.npos) {
            // TOML table / array-of-tables headers. Values occur after an assignment.
            auto end = text.npos; char quote {};
            for (auto cursor = i + 1; cursor < text.size(); ++cursor) {
                if (quote) {
                    if (quote == '"' && text[cursor] == '\\') { if (cursor + 1 < text.size()) ++cursor; }
                    else if (text[cursor] == quote) quote = 0;
                } else if (text[cursor] == '"' || text[cursor] == '\'') quote = text[cursor];
                else if (text[cursor] == ']') { end = cursor; break; }
            }
            if (end != text.npos) {
                i = end + 1; if (i < text.size() && text[i] == ']') ++i;
                emit(start, Role::Key); continue;
            }
        }
        if (text[i] == '"' || (toml && text[i] == '\'')) {
            char quote = text[i]; const bool basic = quote == '"';
            if (toml && text.substr(i).starts_with(basic ? "\"\"\"" : "'''")) {
                i += 3; state.mode = basic ? Mode::BasicMulti : Mode::LiteralMulti;
                while (i < text.size()) {
                    if (basic && text[i] == '\\') { i += std::min<std::size_t>(2, text.size() - i); continue; }
                    if (text.substr(i).starts_with(basic ? "\"\"\"" : "'''")) { i += 3; state.mode = Mode::Normal; break; }
                    ++i;
                }
                emit(start, Role::String); continue;
            }
            ++i; bool closed {};
            while (i < text.size()) {
                if (basic && text[i] == '\\') { i += std::min<std::size_t>(2, text.size() - i); continue; }
                if (text[i++] == quote) { closed = true; break; }
            }
            auto next = after_space(text, i);
            const bool key = closed && ((next < text.size() && text[next] == (toml ? '=' : ':')) || (toml && equal != text.npos && i <= equal));
            emit(start, key ? Role::Key : Role::String); continue;
        }
        if (digit(text[i]) || ((text[i] == '-' || (toml && text[i] == '+')) && i + 1 < text.size() && digit(text[i + 1]))) {
            ++i;
            while (i < text.size() && (digit(text[i]) || text[i] == '.' || text[i] == 'e' || text[i] == 'E' ||
                (toml && (word(text[i]) || text[i] == '_' || text[i] == ':' || text[i] == '-')) ||
                ((text[i] == '+' || text[i] == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E')))) ++i;
            auto next = after_space(text, i);
            emit(start, toml && ((equal != text.npos && i <= equal) || (next < text.size() && text[next] == '=')) ? Role::Key : Role::Number); continue;
        }
        if (word(text[i])) {
            ++i; while (i < text.size() && (word(text[i]) || digit(text[i]) || (toml && (text[i] == '-' || text[i] == '.')))) ++i;
            auto value = text.substr(start, i - start); auto next = after_space(text, i);
            if (toml && ((equal != text.npos && i <= equal) || (next < text.size() && text[next] == '='))) emit(start, Role::Key);
            else if (value == "true" || value == "false" || (!toml && value == "null") || (toml && (value == "inf" || value == "nan"))) emit(start, Role::Keyword);
            continue;
        }
        ++i;
    }
}
}

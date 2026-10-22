// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.syntax;
import std;

namespace meditor::syntax::detail {
namespace {
bool space(char c) { return c == ' ' || c == '\t'; }
std::string_view trim(std::string_view text) {
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}
Language fence_language(std::string_view tag) {
    if (tag == "cpp" || tag == "c++" || tag == "cppm" || tag == "c") return Language::Cpp;
    if (tag == "json") return Language::Json;
    if (tag == "jsonc") return Language::Jsonc;
    if (tag == "toml") return Language::Toml;
    return Language::Plain;
}
std::size_t run(std::string_view text, std::size_t i, char marker) {
    auto end = i; while (end < text.size() && text[end] == marker) ++end; return end - i;
}
}
void scan_markdown(Line& line) {
    auto& state = line.after; const std::string_view text {line.text};
    std::size_t first {}; while (first < text.size() && text[first] == ' ') ++first;
    const bool fenceCandidate = first <= 3 && first < text.size() && (text[first] == '`' || text[first] == '~');
    const auto fenceSize = fenceCandidate ? run(text, first, text[first]) : 0;
    if (state.fence) {
        if (fenceCandidate && text[first] == state.fence && fenceSize >= state.fenceLength && trim(text.substr(first + fenceSize)).empty()) {
            line.tokens.push_back({first, text.size(), Role::Directive}); state = {}; return;
        }
        std::size_t indent {}; while (indent < std::min(state.fenceIndent, text.size()) && text[indent] == ' ') ++indent;
        Line child {.text = std::string(text.substr(indent))};
        child.after.mode = state.embeddedMode; child.after.rawEnd = state.embeddedRawEnd;
        if (state.fenceLanguage == Language::Cpp) scan_cpp(child);
        else if (state.fenceLanguage != Language::Plain) scan_config(child, state.fenceLanguage);
        else if (indent < text.size()) child.tokens.push_back({0, text.size() - indent, Role::Code});
        state.embeddedMode = child.after.mode; state.embeddedRawEnd = std::move(child.after.rawEnd);
        for (auto token : child.tokens) { token.start += indent; token.end += indent; line.tokens.push_back(token); }
        return;
    }
    if (fenceSize >= 3 && (text[first] != '`' || text.substr(first + fenceSize).find('`') == text.npos)) {
        state.fence = text[first]; state.fenceLength = fenceSize; state.fenceIndent = first;
        auto info = trim(text.substr(first + fenceSize)); auto end = info.find_first_of(" \t");
        state.fenceLanguage = fence_language(info.substr(0, end));
        line.tokens.push_back({first, text.size(), Role::Directive}); return;
    }
    if (first <= 3 && first < text.size() && text[first] == '#') {
        auto size = run(text, first, '#');
        if (size <= 6 && (first + size == text.size() || space(text[first + size]))) {
            line.tokens.push_back({first, text.size(), Role::Heading}); return;
        }
    }
    std::size_t i {};
    if (first <= 3 && first < text.size()) {
        i = first;
        // Block markers remain independently styled from inline content.
        while (i < text.size() && text[i] == '>') {
            line.tokens.push_back({i, i + 1, Role::Marker}); ++i;
            while (i < text.size() && space(text[i])) ++i;
        }
        auto markerEnd = i;
        if (i < text.size() && (text[i] == '-' || text[i] == '+' || text[i] == '*')) ++markerEnd;
        else {
            while (markerEnd < text.size() && text[markerEnd] >= '0' && text[markerEnd] <= '9') ++markerEnd;
            if (markerEnd > i && markerEnd - i <= 9 && markerEnd < text.size() && (text[markerEnd] == '.' || text[markerEnd] == ')')) ++markerEnd;
            else markerEnd = i;
        }
        if (markerEnd > i && markerEnd < text.size() && space(text[markerEnd])) { line.tokens.push_back({i, markerEnd, Role::Marker}); i = markerEnd; }
    }
    // Bound delimiter searches on malformed text; repeated unmatched openers must
    // not turn a keystroke into quadratic work. Exhaustion uses the shared fallback.
    std::size_t work = text.size() * 8 + 128;
    auto find = [&](std::string_view needle, std::size_t start) {
        const auto length = std::min(work, text.size() - start);
        auto found = text.substr(start, length).find(needle);
        work -= found == text.npos ? length : std::min(length, found + needle.size());
        return found == text.npos ? text.npos : start + found;
    };
    auto escaped = [&](std::size_t offset) {
        std::size_t count {};
        while (offset && work && text[--offset] == '\\') { ++count; --work; }
        return count % 2 != 0;
    };
    while (i < text.size() && work) {
        --work;
        const auto start = i;
        if (text[i] == '\\' && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) < 128) { i += 2; continue; }
        if (text[i] == '`') {
            auto count = run(text, i, '`'); i += count; auto search = i;
            while (search < text.size()) {
                auto end = find("`", search); if (end == text.npos) break;
                auto endCount = run(text, end, '`');
                work -= std::min(work, endCount);
                if (endCount == count) { i = end + count; line.tokens.push_back({start, i, Role::Code}); break; }
                search = end + endCount;
            }
            continue;
        }
        if (text[i] == '[' || (text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[')) {
            auto open = text[i] == '!' ? i + 1 : i;
            auto end = find("]", open + 1);
            if (end != text.npos && !escaped(end) && end + 1 < text.size() && text[end + 1] == '(') {
                std::size_t cursor = end + 2, depth {1};
                while (cursor < text.size() && depth && work) {
                    --work;
                    if (text[cursor] == '\\' && cursor + 1 < text.size()) { cursor += 2; continue; }
                    if (text[cursor] == '(') ++depth;
                    else if (text[cursor] == ')') --depth;
                    ++cursor;
                }
                if (!depth) { i = cursor; line.tokens.push_back({start, i, Role::Link}); continue; }
            }
        }
        if (text[i] == '*' || text[i] == '_') {
            const std::size_t count = i + 1 < text.size() && text[i + 1] == text[i] ? 2 : 1;
            const bool insideWord = text[i] == '_' && i && !space(text[i - 1]) && text[i - 1] != '(';
            if (!insideWord && i + count < text.size() && !space(text[i + count])) {
                auto delimiter = text.substr(i, count); auto search = i + count;
                while (search < text.size()) {
                    auto end = find(delimiter, search); if (end == text.npos) break;
                    if (!escaped(end) && end > i + count && !space(text[end - 1])) {
                        i = end + count; line.tokens.push_back({start, i, Role::Emphasis}); break;
                    }
                    search = end + count;
                }
                if (i != start) continue;
            }
        }
        ++i;
    }
    if (!work) { line.limited = true; line.tokens.clear(); }
}
}

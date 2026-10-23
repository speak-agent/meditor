// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.document;
import std;

namespace meditor {
namespace {
constexpr std::size_t MAX_BYTES {MAX_DOCUMENT_BYTES};
std::size_t units(std::string_view text) {
    std::size_t count {}; for (unsigned char c : text) if ((c & 0xc0) != 0x80) count += c >= 0xf0 ? 2 : 1;
    return count;
}
void validate(std::string_view text, std::string_view query, std::size_t start) {
    if (query.empty() || query.size() > 4096 || !valid_utf8(query) || !valid_utf8(text) || start > text.size() ||
        (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80))
        throw std::invalid_argument("Invalid literal search query or UTF-8 boundary");
}
template<class Emit>
std::size_t scan(std::string_view text, std::string_view query, std::size_t start, bool backward, Emit emit) {
    validate(text, query, start);
    std::string pattern(query); if (backward) std::reverse(pattern.begin(), pattern.end());
    std::vector<std::size_t> prefix(pattern.size());
    for (std::size_t i = 1, j = 0; i < pattern.size(); ++i) {
        while (j && pattern[i] != pattern[j]) j = prefix[j - 1];
        if (pattern[i] == pattern[j]) ++j;
        prefix[i] = j;
    }
    const auto queryUnits = units(query);
    auto offset = units(text.substr(0, start)); std::size_t matched {}, total {};
    auto consume = [&](std::size_t i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const auto size = (c & 0xc0) == 0x80 ? 0U : c >= 0xf0 ? 2U : 1U;
        if (backward) offset -= size; else offset += size;
        while (matched && text[i] != pattern[matched]) matched = prefix[matched - 1];
        if (text[i] == pattern[matched]) ++matched;
        if (matched != pattern.size()) return;
        ++total;
        if (backward) emit(TextMatch{i, i + pattern.size(), offset, offset + queryUnits});
        else emit(TextMatch{i + 1 - pattern.size(), i + 1, offset - queryUnits, offset});
        matched = 0; // Search and replacement both use non-overlapping matches.
    };
    if (backward) { for (auto i = start; i; ) consume(--i); }
    else for (auto i = start; i < text.size(); ++i) consume(i);
    return total;
}
void append(std::string& output, std::string_view text) {
    if (text.size() > MAX_BYTES - output.size()) throw std::length_error("Replacement exceeds the document size limit");
    output.append(text);
}
}
TextSearch find_text(std::string_view text, std::string_view query, std::size_t start, std::size_t limit, bool backward) {
    if (!limit || limit > 10000) throw std::invalid_argument("Invalid search result limit");
    TextSearch result;
    result.total = scan(text, query, start, backward, [&](TextMatch match) { if (result.matches.size() < limit) result.matches.push_back(match); });
    result.truncated = result.total > result.matches.size(); return result;
}
TextReplacement replace_all_text(std::string_view text, std::string_view query, std::string_view replacement) {
    if (!valid_utf8(replacement)) throw std::invalid_argument("Invalid replacement UTF-8");
    TextReplacement result; std::size_t end {};
    result.count = scan(text, query, 0, false, [&](TextMatch match) {
        append(result.text, text.substr(end, match.start - end)); append(result.text, replacement); end = match.end;
    });
    append(result.text, text.substr(end)); return result;
}
TextReplacement replace_all_display(const TextPresentation& text, std::string_view query, std::string_view replacement, LineEnding preferred) {
    if (!valid_utf8(replacement)) throw std::invalid_argument("Invalid replacement UTF-8");
    // Input CR/CRLF are line breaks; existing raw text outside matches stays exact.
    std::string input(replacement);
    for (std::size_t i = 0; i < input.size(); ++i) if (input[i] == '\r' && (i + 1 == input.size() || input[i + 1] != '\n')) input[i] = '\n';
    input = normalize_line_endings(input, preferred);
    TextReplacement result; std::size_t end {};
    result.count = scan(text.text(), query, 0, false, [&](TextMatch match) {
        const auto start = text.raw_offset(match.start), next = text.raw_offset(match.end);
        append(result.text, std::string_view(text.raw_text()).substr(end, start - end)); append(result.text, input); end = next;
    });
    append(result.text, std::string_view(text.raw_text()).substr(end)); return result;
}
}

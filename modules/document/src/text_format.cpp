// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.document;
import std;

namespace meditor {
TextFormat inspect_text_format(std::string_view text) {
    TextFormat format; format.utf8Bom = text.starts_with("\xef\xbb\xbf");
    bool lf {}, crlf {};
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && (i + 1 == text.size() || text[i + 1] != '\n')) format.loneCarriageReturns = true;
        if (text[i] != '\n') continue;
        const bool pair = i && text[i - 1] == '\r';
        if (!format.lineBreaks++) format.lineEnding = pair ? LineEnding::CrLf : LineEnding::Lf;
        if (pair) crlf = true; else lf = true;
    }
    format.mixedLineEndings = lf && crlf;
    return format;
}
std::string normalize_line_endings(std::string_view text, LineEnding ending) {
    std::string result; result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        if (text[i] == '\n' && ending == LineEnding::CrLf) result += '\r';
        result += text[i];
    }
    return result;
}
std::string encode_text_format(std::string_view display, std::span<const LineEnding> endings, bool utf8Bom) {
    if (!valid_utf8(display) || display.find("\r\n") != display.npos || static_cast<std::size_t>(std::count(display.begin(), display.end(), '\n')) != endings.size())
        throw std::invalid_argument("Display text and newline metadata do not match");
    std::string result = utf8Bom ? "\xef\xbb\xbf" : ""; result.reserve(result.size() + display.size() + endings.size());
    std::size_t line {};
    for (char c : display) { if (c == '\n' && endings[line++] == LineEnding::CrLf) result += '\r'; result += c; }
    return result;
}
TextPresentation::TextPresentation(std::string raw) : raw_(std::move(raw)), format_(inspect_text_format(raw_)) {
    if (!valid_utf8(raw_)) throw std::runtime_error("Only valid UTF-8 documents can be displayed");
    text_.reserve(raw_.size());
    for (std::size_t i = format_.utf8Bom ? 3 : 0; i < raw_.size(); ++i) {
        if (raw_[i] == '\r' && i + 1 < raw_.size() && raw_[i + 1] == '\n') { crlf_.push_back(text_.size()); continue; }
        if (raw_[i] == '\n') endings_.push_back(i && raw_[i - 1] == '\r' ? LineEnding::CrLf : LineEnding::Lf);
        text_ += raw_[i];
    }
}
const std::string& TextPresentation::raw_text() const noexcept { return raw_; }
const std::string& TextPresentation::text() const noexcept { return text_; }
TextFormat TextPresentation::format() const noexcept { return format_; }
std::span<const LineEnding> TextPresentation::line_endings() const noexcept { return endings_; }
std::size_t TextPresentation::raw_offset(std::size_t byte) const {
    if (byte > text_.size() || (byte < text_.size() && (static_cast<unsigned char>(text_[byte]) & 0xc0) == 0x80))
        throw std::runtime_error("Invalid display UTF-8 boundary");
    return byte + (format_.utf8Bom ? 3 : 0) + static_cast<std::size_t>(std::lower_bound(crlf_.begin(), crlf_.end(), byte) - crlf_.begin());
}
}

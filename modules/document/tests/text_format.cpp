// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.document;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Text format contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace meditor;
    const std::string raw = "\xef\xbb\xbf" "a\r\n你好🙂\nx\r\n";
    TextPresentation view(raw);
    require(view.text() == "a\n你好🙂\nx\n" && view.raw_text() == raw);
    require(view.format().utf8Bom && view.format().mixedLineEndings && view.format().lineEnding == LineEnding::CrLf);
    require(view.format().lineBreaks == 3 && view.line_endings().size() == 3);
    require(view.raw_offset(0) == 3 && view.raw_offset(1) == 4 && view.raw_offset(2) == 6 && view.raw_offset(view.text().size()) == raw.size());
    bool rejected {}; try { view.raw_offset(3); } catch (...) { rejected = true; } require(rejected);
    require(encode_text_format(view.text(), view.line_endings(), true) == raw);
    require(normalize_line_endings("a\r\nb\n", LineEnding::CrLf) == "a\r\nb\r\n");
    require(normalize_line_endings("a\r\nb\n", LineEnding::Lf) == "a\nb\n");
    require(TextPresentation("\xef\xbb\xbf").text().empty());
    require(inspect_text_format("x\ry").loneCarriageReturns);
    rejected = false; try { TextPresentation invalid("\xff\xfe"); } catch (...) { rejected = true; } require(rejected);
    rejected = false; try { encode_text_format("a\nb", {}, false); } catch (...) { rejected = true; } require(rejected);
    std::mt19937 random {81};
    for (int test = 0; test < 500; ++test) {
        std::string bytes = test % 2 ? "\xef\xbb\xbf" : "";
        for (int line = 0; line < 16; ++line) { bytes += "你好🙂"; bytes += random() % 2 ? "\n" : "\r\n"; }
        TextPresentation sample(bytes);
        require(encode_text_format(sample.text(), sample.line_endings(), sample.format().utf8Bom) == bytes);
        for (std::size_t offset = 0; offset <= sample.text().size(); ++offset) {
            if (offset < sample.text().size() && (static_cast<unsigned char>(sample.text()[offset]) & 0xc0) == 0x80) continue;
            auto original = sample.raw_offset(offset);
            require(original <= bytes.size());
            if (offset < sample.text().size()) require(bytes[original] == sample.text()[offset] || (bytes[original] == '\r' && sample.text()[offset] == '\n'));
        }
    }
    std::println("BOM/CRLF display mapping, exact mixed-ending round trips and invalid input: passed");
}

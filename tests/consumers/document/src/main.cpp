// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.document;
import meditor.syntax;
int main() {
    meditor::Document document("original");
    document.apply({{0, 8, "independent consumer"}}, document.revision());
    if (document.text() != "independent consumer") return 1;
    if (!document.undo(document.revision()) || document.text() != "original") return 2;
    meditor::syntax::Cache syntax;
    std::vector<std::string_view> lines {"```json", R"({"example": true})", "```"};
    syntax.update(lines, meditor::syntax::Language::Markdown);
    if (syntax.tokens(1).empty() || syntax.tokens(1)[0].role != meditor::syntax::Role::Key) return 3;
    meditor::TextPresentation view("\xef\xbb\xbf" "a\r\nb\n");
    if (view.text() != "a\nb\n" || view.raw_offset(0) != 3 || meditor::encode_text_format(view.text(), view.line_endings(), true) != view.raw_text()) return 4;
    std::println("independent document and syntax consumer: passed");
}

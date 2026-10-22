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
    if (meditor::find_text("你好 🙂 你好", "你好").total != 2) return 5;
    auto changed = meditor::replace_all_display(view, "a", "X", meditor::LineEnding::CrLf);
    if (changed.count != 1 || changed.text != "\xef\xbb\xbf" "X\r\nb\n") return 6;
    if (view.display_offset(meditor::utf8_offset_at(view.raw_text(), {1, 1})) != 3) return 7;
    if (meditor::utf8_position_at(view.raw_text(), 6).line != 1 || meditor::utf8_position_at(view.raw_text(), 6).character != 0) return 8;
    std::println("independent document and syntax consumer: passed");
}

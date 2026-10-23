// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.document;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) { if (!value) throw std::runtime_error("Search contract failed at line " + std::to_string(at.line())); }
}
int main() {
    using namespace meditor;
    auto result = find_text("你好🙂 你好🙂", "🙂");
    require(result.total == 2 && result.matches[0] == TextMatch{6, 10, 2, 4} && result.matches[1] == TextMatch{17, 21, 7, 9});
    result = find_text("你好🙂 你好🙂", "🙂", 21, 1, true);
    require(result.total == 2 && result.truncated && result.matches[0].start == 17);
    require(find_text("aaa", "aa", 1).matches[0].start == 1);
    require(find_text("aaa", "aa", 3, 1000, true).matches[0].start == 1);
    require(find_text("a.* a.*", ".*").total == 2); // Literal, never regex.
    require(replace_all_text("aaaa", "aa", "X").text == "XX");
    TextPresentation view("\xef\xbb\xbf" "foo\r\nbar\nfoo\r\n");
    auto changed = replace_all_display(view, "foo", "你好\n🙂", LineEnding::CrLf);
    require(changed.count == 2 && changed.text == "\xef\xbb\xbf" "你好\r\n🙂\r\nbar\n你好\r\n🙂\r\n");
    bool rejected {}; try { find_text("🙂", "x", 1); } catch (...) { rejected = true; } require(rejected);
    rejected = false; try { find_text("x", ""); } catch (...) { rejected = true; } require(rejected);
    rejected = false; try { replace_all_text(std::string(100, 'a'), "a", std::string(1024 * 1024, 'b')); } catch (const std::length_error&) { rejected = true; } require(rejected);
    // Compare forward and backward navigation against the standard byte search on valid text.
    std::mt19937 random {91};
    for (int test = 0; test < 500; ++test) {
        std::string text, query;
        for (int i = 0; i < 150; ++i) text += "abc"[random() % 3];
        for (int i = 0; i < 1 + test % 8; ++i) query += "abc"[random() % 3];
        const auto start = random() % (text.size() + 1);
        auto forward = find_text(text, query, start, 10000);
        std::vector<std::size_t> expected;
        for (auto at = text.find(query, start); at != text.npos; at = text.find(query, at + query.size())) expected.push_back(at);
        require(forward.total == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) require(forward.matches[i].start == expected[i]);
        auto backward = find_text(text, query, start, 10000, true); expected.clear();
        for (auto end = start; end >= query.size();) { auto at = text.rfind(query, end - query.size()); if (at == text.npos) break; expected.push_back(at); end = at; }
        require(backward.total == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) require(backward.matches[i].start == expected[i]);
    }
    std::string pathological(1024 * 1024, 'a');
    require(find_text(pathological, std::string(4000, 'a') + "b").total == 0);
    std::println("Linear literal search, Unicode navigation, bounded results and format-preserving replacement: passed");
}

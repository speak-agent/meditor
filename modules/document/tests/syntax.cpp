// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.syntax;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Syntax contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace meditor::syntax;
    require(detect("src/main.CPPM") == Language::Cpp && detect("notes.md") == Language::Plain);
    Cache cache;
    std::vector<std::string_view> source {"export module 世界; // 🙂", "/* comment", "return */ import std;", "auto s = u8R\"tag(/*", "export module fake;", ")tag\"; int n = 1'024 + 0x1.fp+2;", "#define VALUE \"text\""};
    cache.update(source, Language::Cpp);
    require(!cache.limited());
    require(cache.tokens(0).size() == 3 && cache.tokens(0)[0] == Token{0, 6, Role::Keyword});
    require(cache.tokens(0).back().role == Role::Comment);
    require(cache.tokens(1).size() == 1 && cache.tokens(2)[0].role == Role::Comment && cache.tokens(2)[1].role == Role::Keyword);
    require(cache.tokens(4).size() == 1 && cache.tokens(4)[0].role == Role::String);
    require(cache.tokens(5).back().role == Role::Number);
    require(cache.tokens(6)[0].role == Role::Directive && cache.tokens(6).back().role == Role::String);
    source[1] = "// closed"; cache.update(source, Language::Cpp);
    require(cache.tokens(2)[0].role == Role::Keyword); // Previous multiline state must not survive an edit.
    source[3] = "auto s = 0;"; cache.update(source, Language::Cpp);
    require(cache.tokens(4)[0].role == Role::Keyword);
    source = {"// spliced \\", "export module hidden;", "export module visible;", "auto s = \"continued\\", "still string\"; return 0;", "auto t = \"unterminated", "import std;"};
    cache.update(source, Language::Cpp);
    require(cache.tokens(1).size() == 1 && cache.tokens(1)[0].role == Role::Comment);
    require(cache.tokens(2)[0].role == Role::Keyword && cache.tokens(4)[0].role == Role::String);
    require(cache.tokens(4)[1].role == Role::Keyword && cache.tokens(6)[0].role == Role::Keyword);
    cache.update(source, Language::Plain); require(cache.tokens(0).empty());
    std::string large(Cache::MAX_BYTES + 1, 'x'); source = {large};
    cache.update(source, Language::Cpp); require(cache.limited() && cache.tokens(0).empty());
    source = {"import std;"}; cache.update(source, Language::Cpp);
    require(!cache.limited() && cache.tokens(0)[0].role == Role::Keyword);
    // Byte boundaries remain valid with multibyte identifiers and comments.
    source = {"auto 世界 = \"🙂\"; // 中文"}; cache.update(source, Language::Cpp);
    require(cache.tokens(0).size() == 3 && cache.tokens(0)[1].role == Role::String);
    std::string longLine(Cache::MAX_LINE_BYTES + 1, 'x'); source = {longLine};
    cache.update(source, Language::Cpp); require(cache.limited());
    std::string manyTokens;
    for (int i = 0; i < 4000; ++i) manyTokens += "int ";
    source.assign(17, manyTokens); cache.update(source, Language::Cpp);
    require(cache.limited() && cache.tokens(0).empty());
    // An incremental result must equal a cold scan after arbitrary line edits.
    std::mt19937 random {42};
    std::vector<std::string_view> corpus {"/*", "*/ import std;", "R\"x(raw", ")x\";", "// comment", "export module 世界;", "auto s = \"🙂\";", "", "1'000", "#include <vector>"};
    source.assign(32, "import std;");
    for (int step = 0; step < 300; ++step) {
        source[random() % source.size()] = corpus[random() % corpus.size()];
        cache.update(source, Language::Cpp); Cache fresh; fresh.update(source, Language::Cpp);
        for (std::size_t row = 0; row < source.size(); ++row) {
            require(std::ranges::equal(cache.tokens(row), fresh.tokens(row)));
            std::size_t end {};
            for (auto token : cache.tokens(row)) {
                require(token.start >= end && token.start < token.end && token.end <= source[row].size());
                require((static_cast<unsigned char>(source[row][token.start]) & 0xc0) != 0x80);
                end = token.end;
            }
        }
    }
    std::println("C++ Modules lexical state, incremental invalidation and bounded fallback: passed");
}

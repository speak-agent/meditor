// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.syntax;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Config/Markdown syntax contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace meditor::syntax;
    require(detect("README.MD") == Language::Markdown && detect("mcpp.toml") == Language::Toml);
    require(detect("settings.JSONC") == Language::Jsonc && detect("data.json") == Language::Json);
    require(detect("mcpp.lock") == Language::Toml && detect("project/mcpp.lock") == Language::Toml);
    require(detect("unknown.lock") == Language::Plain); // A lockfile is not necessarily TOML.
    Cache cache;
    std::vector<std::string_view> lines {R"({"键": "🙂", "count": -1.2e+3, "active": true})", "/* comment", "still */ null // rest"};
    cache.update(lines, Language::Jsonc);
    require(cache.tokens(0).size() == 6 && cache.tokens(0)[0].role == Role::Key && cache.tokens(0)[1].role == Role::String);
    require(cache.tokens(0)[3].role == Role::Number && cache.tokens(0).back().role == Role::Keyword);
    require(cache.tokens(1)[0].role == Role::Comment && cache.tokens(2).size() == 3);
    cache.update(lines, Language::Json);
    require(std::ranges::none_of(cache.tokens(1), [](auto token) { return token.role == Role::Comment; }));
    lines = {"[package]", "name = 'demo#name' # comment", "deps.json = { version = \"3.12\", enabled = true }", "text = \"\"\"hello", "# still string", "world\"\"\" # end", "literal = '''text", "\\''' # end"};
    cache.update(lines, Language::Toml);
    require(cache.tokens(0)[0].role == Role::Key && cache.tokens(1).size() == 3);
    require(cache.tokens(1)[1].role == Role::String && cache.tokens(1)[2].role == Role::Comment);
    require(cache.tokens(2)[0].role == Role::Key && cache.tokens(2)[1].role == Role::Key);
    require(cache.tokens(4).size() == 1 && cache.tokens(4)[0].role == Role::String);
    require(cache.tokens(5).size() == 2 && cache.tokens(5)[1].role == Role::Comment);
    require(cache.tokens(7).size() == 2 && cache.tokens(7)[1].role == Role::Comment);
    lines = {R"(["bracket]#key"] # comment)", R"("equal=key" = "# value")"};
    cache.update(lines, Language::Toml);
    require(cache.tokens(0).size() == 2 && cache.tokens(0)[0].end == 16 && cache.tokens(0)[1].role == Role::Comment);
    require(cache.tokens(1)[0].role == Role::Key && cache.tokens(1)[1].role == Role::String);
    lines = {"# 标题 🙂", "> - **bold** and `code` with [link](docs/hello.md)", "```cpp", "export module demo; /*", "```", "normal text", "~~~json", "{\"key\": true}", "~~~~", "  ```toml", "  title = '''hello", "  world'''", "  ```"};
    cache.update(lines, Language::Markdown);
    require(!cache.limited() && cache.tokens(0)[0].role == Role::Heading);
    require(cache.tokens(1).size() == 5 && cache.tokens(1)[2].role == Role::Emphasis && cache.tokens(1)[3].role == Role::Code && cache.tokens(1)[4].role == Role::Link);
    require(cache.tokens(3)[0].role == Role::Keyword && cache.tokens(3).back().role == Role::Comment);
    require(cache.tokens(4)[0].role == Role::Directive && cache.tokens(5).empty());
    require(cache.tokens(7)[0].role == Role::Key && cache.tokens(7)[1].role == Role::Keyword);
    require(cache.tokens(10)[0].start == 2 && cache.tokens(11)[0].role == Role::String);
    lines[2] = "plain text"; cache.update(lines, Language::Markdown);
    require(cache.tokens(3).empty()); // Removing an opener invalidates subsequent embedded state.
    lines = {"````cpp", "```", "export module visible;", "````", "_one_ foo_bar **two**", "\\*escaped*"};
    cache.update(lines, Language::Markdown);
    require(cache.tokens(1).empty() && cache.tokens(2)[0].role == Role::Keyword);
    require(cache.tokens(4).size() == 2 && cache.tokens(5).empty());
    std::string pathological(12000, '['); pathological += "](unclosed";
    lines = {pathological}; cache.update(lines, Language::Markdown);
    require(cache.limited() && cache.tokens(0).empty());
    std::mt19937 random {73};
    const std::vector<std::string_view> corpus {"```cpp", "```", "~~~jsonc", "~~~", "# Title", "> - *word*", "[link](target)", "/*", "*/", "\"key\": true", "text = \"\"\"", "# comment", ""};
    for (auto language : {Language::Json, Language::Jsonc, Language::Toml, Language::Markdown}) {
        lines.assign(24, "");
        for (int step = 0; step < 200; ++step) {
            lines[random() % lines.size()] = corpus[random() % corpus.size()];
            cache.update(lines, language); Cache fresh; fresh.update(lines, language);
            require(cache.limited() == fresh.limited());
            for (std::size_t row = 0; row < lines.size(); ++row) {
                require(std::ranges::equal(cache.tokens(row), fresh.tokens(row)));
                std::size_t end {};
                for (auto token : cache.tokens(row)) {
                    require(token.start >= end && token.start < token.end && token.end <= lines[row].size()); end = token.end;
                }
            }
        }
    }
    std::println("JSON/JSONC, TOML multiline strings and Markdown embedded lexical state: passed");
}

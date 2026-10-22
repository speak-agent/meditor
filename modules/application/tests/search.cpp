// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;

namespace {
void require(bool value, std::source_location at = std::source_location::current()) { if (!value) throw std::runtime_error("Search command contract failed at line " + std::to_string(at.line())); }
}
int main() {
    using meditor::Json;
    auto root = std::filesystem::temp_directory_path() / ("meditor-find-core-" + std::to_string(std::random_device{}())); std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const std::string original = "\xef\xbb\xbf" "needle\r\n🙂 needle\n";
    { std::ofstream file(root / "main.cppm", std::ios::binary); file << original; }
    meditor::Application app(root);
    auto call = [&](std::string command, Json params) { auto result = app.execute({{"command", command}, {"params", std::move(params)}}); require(result["ok"].get<bool>()); return result["data"]; };
    auto id = call("document.open", {{"path", "main.cppm"}})["document"].get<std::string>();
    auto found = call("document.find", {{"document", id}, {"revision", 1}, {"query", "needle"}, {"coordinateSpace", "display"}});
    require(found["total"] == 2 && found["matches"][0]["start"] == 0 && found["matches"][0]["rawStart"] == 3 && found["matches"][0]["utf16Start"] == 0);
    found = call("document.find", {{"document", id}, {"revision", 1}, {"query", "needle"}}); require(found["matches"][0]["start"] == 3 && found["matches"][0]["utf16Start"] == 1);
    auto unchanged = app.execute({{"command", "document.replace"}, {"params", {{"document", id}, {"revision", 1}, {"query", "needle"}, {"replacement", "needle"}, {"coordinateSpace", "display"}}}});
    require(unchanged["data"]["revision"] == 1 && unchanged["effects"].empty() && unchanged["data"]["nextStart"] == 6);
    auto changed = call("document.replace", {{"document", id}, {"revision", 1}, {"query", "needle"}, {"replacement", "好\n"}, {"coordinateSpace", "display"}, {"all", true}});
    require(changed["replacements"] == 2 && changed["revision"] == 2 && changed["dirty"].get<bool>());
    require(call("document.read", {{"document", id}})["text"] == "\xef\xbb\xbf" "好\r\n\r\n🙂 好\r\n\n");
    std::ifstream file(root / "main.cppm", std::ios::binary); require(std::string(std::istreambuf_iterator<char>(file), {}) == original); file.close();
    auto stale = app.execute({{"command", "document.find"}, {"params", {{"document", id}, {"revision", 1}, {"query", "needle"}}}}); require(stale["error"]["code"] == "revision-conflict");
    call("document.undo", {{"document", id}, {"revision", 2}}); require(call("document.read", {{"document", id}})["text"] == original);
    auto invalid = app.execute({{"command", "document.find"}, {"params", {{"document", id}, {"revision", 3}, {"query", ""}}}}); require(invalid["error"]["code"] == "invalid-params");
    auto largeId = call("document.open", {{"path", "large.cppm"}, {"create", true}})["document"].get<std::string>();
    call("document.edit", {{"document", largeId}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", std::string(100, 'a')}}})}});
    auto excessive = app.execute({{"command", "document.replace"}, {"params", {{"document", largeId}, {"revision", 2}, {"query", "a"}, {"replacement", std::string(1024 * 1024, 'b')}, {"all", true}}}});
    require(excessive["error"]["code"] == "document-too-large");
    require(call("document.read", {{"document", largeId}})["text"] == std::string(100, 'a'));
    require(call("document.read", {{"document", largeId}})["revision"] == 2 && !std::filesystem::exists(root / "large.cppm"));
    std::println("Revisioned search/replace commands preserve raw coordinates, BOM, mixed endings and atomic failure: passed");
}

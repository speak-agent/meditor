// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import nlohmann.json;
import meditor.document;
import meditor.language.locations;
import meditor.platform;

namespace {
using Json = nlohmann::json;
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Language location contract failed at line " + std::to_string(at.line()));
}
std::string uri(const std::filesystem::path& path) {
    const auto text = meditor::path_to_utf8(std::filesystem::absolute(path));
    std::string result = text.starts_with('/') ? "file://" : "file:///";
    constexpr std::string_view hex = "0123456789ABCDEF";
    for (unsigned char ch : text) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.') result += ch;
        else { result += '%'; result += hex[ch >> 4]; result += hex[ch & 15]; }
    }
    return result;
}
}
int main() {
    using namespace meditor::language;
    const auto base = std::filesystem::temp_directory_path() / std::filesystem::u8path("meditor-location 空间-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(base / "project");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {base};
    const auto root = std::filesystem::weakly_canonical(base / "project");
    require(meditor::same_path_spelling(root / "child/../file", root / "file"));
    require(!meditor::same_path_spelling(root / "first", root / "second"));
    const std::string name = "模块 target.cppm";
    const auto target = root / std::filesystem::u8path(name);
    const std::string original = "\xef\xbb\xbf" "a\r\n你好🙂\n";
    meditor::write_file_atomic(target, original);
    meditor::write_file_atomic(base / "outside.cppm", "private synthetic text");
    require(workspace_path_for_uri(uri(target), root) == name);
    require(workspace_path_for_uri(uri(root / "nested/file.cppm"), root) == "nested/file.cppm");
    require(!workspace_path_for_uri(uri(base / "project-other/file.cppm"), root));
    require(workspace_path_for_uri(uri(target), root.root_path()) == meditor::path_to_utf8(target.lexically_relative(root.root_path())));
    require(workspace_path_for_uri("file://LOCALHOST" + uri(target).substr(7), root) == name);
    auto uppercase = uri(target); uppercase.replace(0, 7, "FILE://");
    require(workspace_path_for_uri(uppercase, root) == name);
#if defined(_WIN32)
    require(!workspace_path_for_uri(uri(target) + ":stream", root));
    auto driveAlias = uri(target);
    if (driveAlias.size() > 9 && driveAlias[8] >= 'A' && driveAlias[8] <= 'Z' && driveAlias[9] == ':') {
        driveAlias[8] += 'a' - 'A'; require(workspace_path_for_uri(driveAlias, root) == name);
    }
#elif defined(__APPLE__)
    const auto canonical = uri(target);
    const std::string prefix = "file:///private/var/";
    if (canonical.starts_with(prefix)) require(workspace_path_for_uri("file:///var/" + canonical.substr(prefix.size()), root) == name);
#endif
    for (const auto& value : {uri(base / "outside.cppm"), uri(root / "../outside.cppm"),
            "file://remote.invalid" + uri(target).substr(7), uri(target) + "?query", uri(target) + "#fragment",
            uri(target) + "%00", uri(target) + "%GG", uri(target) + "%", std::string("https://example.invalid/file"), std::string("file:relative.cppm")})
        require(!workspace_path_for_uri(value, root));
    const Json range {{"start", {{"line", 1}, {"character", 2}}}, {"end", {{"line", 1}, {"character", 4}}}};
    auto result = Json::array({{{"uri", uri(target)}, {"range", range}},
        {{"targetUri", uri(target)}, {"targetRange", range}, {"targetSelectionRange", range}},
        {{"uri", uri(base / "outside.cppm")}, {"range", range}},
        {{"uri", uri(target)}, {"range", {{"start", {{"line", 1}, {"character", 3}}}, {"end", {{"line", 1}, {"character", 4}}}}}},
        {{"uri", uri(root / "missing.cppm")}, {"range", range}}});
    auto captured = collect_locations(result, root, {});
    require(captured.locations.size() == 5 && captured.texts.size() == 1 && captured.textBytes == original.size());
    require(captured.locations[0]["available"] && captured.locations[1]["available"]);
    require(captured.locations[2]["reason"] == "outside-workspace" && !captured.locations[2].contains("path"));
    require(captured.locations[3]["reason"] == "invalid-range" && captured.locations[4]["reason"] == "target-unavailable");
    require(captured.locations.dump().find(meditor::path_to_utf8(base)) == std::string::npos);
    const std::string unsaved = "x\n你好🙂!\n";
    captured = collect_locations(result[0], root, {{name, unsaved}});
    require(captured.locations[0]["available"] && captured.texts.at(name) == unsaved && meditor::read_file(target) == original);
#if defined(_WIN32)
    const auto alias = root / std::filesystem::u8path("模块 TARGET.cppm");
    std::error_code aliasError;
    if (std::filesystem::equivalent(target, alias, aliasError) && !aliasError) {
        auto aliased = collect_locations(Json{{"uri", uri(alias)}, {"range", range}}, root, {{name, unsaved}});
        require(aliased.locations[0]["path"] == name && aliased.texts.at(name) == unsaved);
    }
#endif
    auto many = Json::array(); for (int i = 0; i < 201; ++i) many.push_back(result[0]);
    captured = collect_locations(many, root, {});
    require(captured.truncated && captured.locations.size() == 200 && captured.textBytes == original.size());
    many.clear(); std::map<std::string, std::string_view> opened;
    for (int i = 0; i < 33; ++i) {
        auto path = std::to_string(i) + ".cppm"; opened[path] = original;
        many.push_back({{"uri", uri(root / path)}, {"range", range}});
    }
    captured = collect_locations(many, root, opened);
    require(captured.truncated && captured.texts.size() == 32 && captured.locations.back()["reason"] == "navigation-limit");
    {
        const std::string large(meditor::MAX_DOCUMENT_BYTES, 'x');
        std::map<std::string, std::string_view> buffers;
        Json locations = Json::array();
        const Json firstCharacter {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}};
        for (int i = 0; i < 3; ++i) {
            const auto path = "large-" + std::to_string(i) + ".cppm"; buffers[path] = large;
            locations.push_back({{"uri", uri(root / path)}, {"range", firstCharacter}});
        }
        captured = collect_locations(locations, root, buffers);
        require(captured.textBytes == 64 * 1024 * 1024 && captured.texts.size() == 2);
        require(captured.truncated && captured.locations.back()["reason"] == "navigation-limit");
        captured = {};
    }
    std::error_code ec; std::filesystem::create_symlink(base / "outside.cppm", root / "escape.cppm", ec);
    if (!ec) require(!workspace_path_for_uri(uri(root / "escape.cppm"), root));
    std::stop_source stop; stop.request_stop(); bool cancelled {};
    try { collect_locations(result, root, {}, stop.get_token()); } catch (...) { cancelled = true; }
    require(cancelled);
    std::println("Workspace file URI boundaries, exact target snapshots, Unicode ranges, cancellation and navigation limits: passed");
}

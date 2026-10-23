// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.language.locations;
import std;
import nlohmann.json;
import meditor.document;
import meditor.platform;

namespace meditor::language {
using Json = nlohmann::json;
namespace {
std::optional<std::filesystem::path> relative_inside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    // Some NDK std modules do not expose path-iterator equality. Split canonical generic spellings instead.
    const auto base = path_to_utf8(root), value = path_to_utf8(candidate);
    if (base.empty() || !root.is_absolute()) return {};
    const bool trailing = base.back() == '/';
    const auto separators = static_cast<std::size_t>(std::count(base.begin(), base.end(), '/')) + (trailing ? 0 : 1);
    std::size_t seen {}, end = value.size();
    for (std::size_t i = 0; i < value.size(); ++i) if (value[i] == '/' && ++seen == separators) { end = i + (trailing ? 1 : 0); break; }
    if (trailing && seen < separators) return {};
    const auto prefix = std::filesystem::u8path(value.substr(0, end));
    if (!same_path_spelling(prefix, root)) return {};
#if defined(_WIN32)
    // Prefix spelling can differ on Windows. Directory identity also handles case-sensitive folders safely.
    if (prefix != root) {
        std::error_code ec;
        if (!same_path_spelling(prefix.root_name(), root.root_name()) || !std::filesystem::equivalent(prefix, root, ec) || ec) return {};
    }
#endif
    if (end < value.size() && value[end] == '/') ++end;
    return end == value.size() ? std::filesystem::path(".") : std::filesystem::u8path(value.substr(end));
}
int hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
Position position(const Json& value) {
    auto number = [&](const char* field) {
        const auto& item = value.at(field);
        if (!item.is_number_integer()) throw std::invalid_argument("Invalid source position");
        const auto result = item.get<std::int64_t>();
        if (result < 0 || result > static_cast<std::int64_t>(MAX_DOCUMENT_BYTES)) throw std::invalid_argument("Invalid source position");
        return static_cast<std::size_t>(result);
    };
    return {number("line"), number("character")};
}
Json range(const Json& value, std::string_view text) {
    const auto start = position(value.at("start")), end = position(value.at("end"));
    if (std::tie(start.line, start.character) > std::tie(end.line, end.character)) throw std::invalid_argument("Reversed source range");
    for (auto offset : {utf8_offset_at(text, start), utf8_offset_at(text, end)})
        if (offset && offset < text.size() && text[offset - 1] == '\r' && text[offset] == '\n') throw std::invalid_argument("Source range splits CRLF");
    return {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}};
}
}
NavigationSet::NavigationSet() : locations(Json::array()) {}
std::optional<std::string> workspace_path_for_uri(std::string_view uri, const std::filesystem::path& root) {
    try {
        if (uri.size() < 8 || uri.size() > 32768 || uri.find_first_of("?#\\") != uri.npos) return {};
        auto scheme = std::string(uri.substr(0, 7));
        for (auto& ch : scheme) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (scheme != "file://") return {};
        uri.remove_prefix(7);
        const auto slash = uri.find('/');
        if (slash == uri.npos) return {};
        auto authority = std::string(uri.substr(0, slash));
        for (auto& ch : authority) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (!authority.empty() && authority != "localhost") return {};
        uri.remove_prefix(slash);
        std::string decoded; decoded.reserve(uri.size());
        for (std::size_t i = 0; i < uri.size(); ++i) {
            if (uri[i] != '%') decoded += uri[i];
            else {
                if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0) return {};
                decoded += static_cast<char>((hex(uri[i + 1]) << 4) | hex(uri[i + 2])); i += 2;
            }
        }
        if (decoded.find('\0') != decoded.npos || !valid_utf8(decoded)) return {};
#if defined(_WIN32)
        if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') decoded.erase(0, 1);
        if (decoded.find(':', decoded.size() > 1 && decoded[1] == ':' ? 2 : 0) != decoded.npos) return {};
#endif
        auto candidate = std::filesystem::u8path(decoded).lexically_normal();
#if defined(__APPLE__)
        // The native temporary-directory API and language engines can use either spelling of /private/var.
        if (path_to_utf8(root).starts_with("/private/var/") && path_to_utf8(candidate).starts_with("/var/")) candidate = std::filesystem::path("/private") / candidate.relative_path();
#endif
        if (!candidate.is_absolute() || !relative_inside(root, candidate)) return {};
        candidate = std::filesystem::weakly_canonical(candidate);
        const auto relative = relative_inside(root, candidate);
        if (!relative) return {};
        return path_to_utf8(*relative);
    } catch (const std::exception&) { return {}; }
}
NavigationSet collect_locations(const Json& result, const std::filesystem::path& root,
        const std::map<std::string, std::string_view>& opened, std::stop_token stop) {
    NavigationSet set;
    if (result.is_null()) return set;
    if (!result.is_array() && !result.is_object()) throw std::invalid_argument("Invalid language locations");
    const auto count = result.is_array() ? result.size() : 1;
    set.truncated = count > 200;
    for (std::size_t i = 0; i < std::min<std::size_t>(count, 200); ++i) {
        if (stop.stop_requested()) throw std::runtime_error("Navigation capture cancelled");
        const auto& item = result.is_array() ? result.at(i) : result;
        Json entry {{"available", false}, {"reason", "invalid-location"}};
        try {
            const auto uri = item.at(item.contains("targetUri") ? "targetUri" : "uri").get<std::string>();
            auto path = workspace_path_for_uri(uri, root);
            if (!path) { entry["reason"] = "outside-workspace"; set.locations.push_back(std::move(entry)); continue; }
            if (!opened.contains(*path)) for (const auto& [name, text] : opened) {
                if (!same_path_spelling(std::filesystem::u8path(*path), std::filesystem::u8path(name))) continue;
                std::error_code ec;
                if (std::filesystem::equivalent(root / std::filesystem::u8path(*path), root / std::filesystem::u8path(name), ec) && !ec) { path = name; break; }
            }
            entry["path"] = *path;
            entry["reason"] = "target-unavailable";
            if (!set.texts.contains(*path)) {
                if (set.texts.size() >= 32) { set.truncated = true; entry["reason"] = "navigation-limit"; set.locations.push_back(std::move(entry)); continue; }
                const auto file = root / std::filesystem::u8path(*path);
                if (!opened.contains(*path) && !std::filesystem::is_regular_file(file)) throw std::invalid_argument("Target is not a file");
                auto text = opened.contains(*path) ? std::string(opened.at(*path)) : read_file(file, MAX_DOCUMENT_BYTES);
                if (!valid_utf8(text)) throw std::invalid_argument("Invalid target encoding");
                if (text.size() > 64 * 1024 * 1024 - set.textBytes) { set.truncated = true; entry["reason"] = "navigation-limit"; set.locations.push_back(std::move(entry)); continue; }
                set.textBytes += text.size(); set.texts.emplace(*path, std::move(text));
            }
            entry["reason"] = "invalid-range";
            entry["range"] = range(item.at(item.contains("targetUri") ? "targetSelectionRange" : "range"), set.texts.at(*path));
            entry["available"] = true; entry.erase("reason");
        } catch (const std::exception&) {}
        set.locations.push_back(std::move(entry));
    }
    return set;
}
}

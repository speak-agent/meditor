// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.language.locations;
import std;
import nlohmann.json;

export namespace meditor::language {
struct NavigationSet {
    nlohmann::json locations;
    std::map<std::string, std::string> texts;
    std::size_t textBytes {};
    bool truncated {};
    NavigationSet();
};
std::optional<std::string> workspace_path_for_uri(std::string_view uri, const std::filesystem::path& root);
NavigationSet collect_locations(const nlohmann::json& result, const std::filesystem::path& root,
    const std::map<std::string, std::string_view>& opened, std::stop_token stop = {});
}

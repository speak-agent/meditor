// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.settings;
import std;
import nlohmann.json;

export namespace meditor {
class SettingsError : public std::runtime_error {
public:
    std::string code;
    SettingsError(std::string kind, std::string message);
};
class SettingsStore {
    std::filesystem::path file_;
    std::filesystem::path file_path_() const;
    nlohmann::json read_() const;
public:
    explicit SettingsStore(std::filesystem::path file = {});
    nlohmann::json get() const;
    nlohmann::json update(std::uint64_t revision, const nlohmann::json& changes);
};
}

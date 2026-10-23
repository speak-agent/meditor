// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.application;
import std;
import nlohmann.json;
import meditor.document;
export import meditor.runtime;
export import meditor.i18n;

export namespace meditor {
using Json = nlohmann::json;
class Application {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    static const Json& metadata_();
public:
    explicit Application(std::filesystem::path workspace = std::filesystem::current_path(), std::filesystem::path toolRoot = {}, std::filesystem::path settingsFile = {}, const RuntimeOptions& runtime = {});
    ~Application();
    Application(const Application&) = delete;
    Json execute(const Json& request);
    Json capabilities() const;
    std::string session_id() const;
};
}

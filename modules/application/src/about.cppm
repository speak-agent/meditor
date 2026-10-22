// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.about;
import std;
import nlohmann.json;

export namespace meditor::about {
nlohmann::json describe(const std::filesystem::path& tools);
nlohmann::json licenses(const std::filesystem::path& tools);
}

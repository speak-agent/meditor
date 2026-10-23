// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.build.metadata;
import std;

export namespace meditor::build {
std::filesystem::path compile_metadata(const std::filesystem::path& repository, const std::filesystem::path& outputDirectory,
    std::string_view version, std::string_view payloadTarget, std::string_view sourceCommit = {}, bool appleRuntime = false);
}

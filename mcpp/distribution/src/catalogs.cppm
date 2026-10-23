// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.build.catalogs;
import std;

export namespace meditor::build {
// Emits a self-contained Modules resource; rejects incomplete translation tables.
std::filesystem::path compile_catalogs(const std::filesystem::path& resources, const std::filesystem::path& outputDirectory);
}

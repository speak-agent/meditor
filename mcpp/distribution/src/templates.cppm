// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.build.templates;
import std;

export namespace meditor::build {
// Emits a self-contained Modules catalog of the versioned meditor project templates.
std::filesystem::path compile_templates(const std::filesystem::path& resources, const std::filesystem::path& outputDirectory);
}

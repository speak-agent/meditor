// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.launch;
import std;
export namespace meditor {
enum class LaunchMode { Gui, Cli };
struct LaunchRequest {
    LaunchMode mode {LaunchMode::Gui};
    std::vector<std::string> arguments {"meditor"};
};
LaunchRequest parse_launch(std::span<const std::string_view> arguments, const std::filesystem::path& directory);
}

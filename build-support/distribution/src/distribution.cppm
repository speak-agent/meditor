// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.distribution;
import std;

export namespace meditor::distribution {
struct Placement {
    std::filesystem::path source;
    std::string destination;
};
struct PayloadPlan {
    std::filesystem::path receipt;
    std::vector<Placement> files;
};
// Returns an empty plan for an unprepared development tree; required delivery refuses it.
PayloadPlan plan(const std::filesystem::path& directory, std::string_view target, bool required);
std::string payload_target(std::string_view arch, std::string_view os, std::string_view environment);
}

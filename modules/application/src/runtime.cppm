// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.runtime;
import std;
import meditor.platform;

export namespace meditor {
struct RuntimeOptions {
    std::filesystem::path binaryDirectory;
    std::filesystem::path dataRoot;
    bool portable {};
};
struct RuntimeDirectories {
    std::filesystem::path installationRoot;
    std::filesystem::path dataRoot;
    bool portable {};
    static RuntimeDirectories resolve(const RuntimeOptions& options = {});
    std::filesystem::path settings_file() const;
    ProcessEnvironment tool_environment() const;
    void prepare_tools() const;
};
}

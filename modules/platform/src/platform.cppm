// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.platform;
import std;

export namespace meditor {
struct ProcessRead { std::string data; bool ended {}; };
using ProcessEnvironment = std::map<std::string, std::optional<std::string>>;
class FileLock {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit FileLock(const std::filesystem::path& path);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
};
class Process {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Process();
    ~Process();
    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;
    Process(const Process&) = delete;
    void start(const std::vector<std::string>& arguments, const std::filesystem::path& directory, bool mergeErrors = true, const ProcessEnvironment& environment = {});
    void write(std::string_view data);
    void close_input();
    ProcessRead read(std::chrono::milliseconds timeout);
    std::optional<int> poll();
    void terminate();
};
std::string path_to_utf8(const std::filesystem::path& path);
std::string read_file(const std::filesystem::path& path, std::size_t limit = 32 * 1024 * 1024);
void write_file_atomic(const std::filesystem::path& path, std::string_view data);
std::filesystem::path executable_directory();
std::filesystem::path user_config_directory();
std::filesystem::path user_cache_directory();
std::string system_language_tag();
std::string redact(std::string text, const std::filesystem::path& workspace = {});
}

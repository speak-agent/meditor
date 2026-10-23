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
    void write(std::string_view data, std::chrono::milliseconds timeout = std::chrono::seconds(5), std::stop_token stop = {});
    void close_input();
    ProcessRead read(std::chrono::milliseconds timeout);
    std::optional<int> poll();
    void terminate();
};
class ChannelListener;
class Channel {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Channel();
    ~Channel();
    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    bool write_line(std::string_view data, std::chrono::milliseconds timeout = std::chrono::seconds(5), std::stop_token stop = {});
    std::optional<std::string> read_line(std::chrono::milliseconds timeout, std::stop_token stop = {});
    bool ended() const;
    void close();
private:
    friend class ChannelListener;
    friend std::unique_ptr<Channel> open_channel(const std::string& address, std::chrono::milliseconds timeout);
    void adopt(int descriptor);
};
std::unique_ptr<Channel> open_channel(const std::string& address, std::chrono::milliseconds timeout);
class ChannelListener {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    // Binds and listens; throws when the address is already serving.
    explicit ChannelListener(const std::string& address);
    ~ChannelListener();
    ChannelListener(const ChannelListener&) = delete;
    ChannelListener& operator=(const ChannelListener&) = delete;
    std::unique_ptr<Channel> accept(std::chrono::milliseconds timeout, std::stop_token stop = {});
};
std::unique_ptr<Channel> open_channel(const std::string& address, std::chrono::milliseconds timeout);
std::string local_channel_address(std::string_view key);
std::string path_to_utf8(const std::filesystem::path& path);
// Windows ordinal case folding, exact spelling elsewhere; aliases also need filesystem identity checks.
bool same_path_spelling(const std::filesystem::path& left, const std::filesystem::path& right);
std::string read_file(const std::filesystem::path& path, std::size_t limit = 32 * 1024 * 1024);
class FileReadOnlyError : public std::runtime_error {
public:
    FileReadOnlyError();
};
void write_file_atomic(const std::filesystem::path& path, std::string_view data);
std::filesystem::path executable_directory();
// Attach a Windows GUI executable to its invoking console, preserving redirected streams.
void prepare_cli_console();
std::filesystem::path user_config_directory();
std::filesystem::path user_cache_directory();
std::string system_language_tag();
std::string redact(std::string text, const std::filesystem::path& workspace = {});
}

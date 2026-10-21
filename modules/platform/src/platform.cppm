export module meditor.platform;
import std;

export namespace meditor {
struct ProcessRead { std::string data; bool ended {}; };
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
    void start(const std::vector<std::string>& arguments, const std::filesystem::path& directory, bool mergeErrors = true);
    void write(std::string_view data);
    void close_input();
    ProcessRead read(std::chrono::milliseconds timeout);
    std::optional<int> poll();
    void terminate();
};
std::string read_file(const std::filesystem::path& path, std::size_t limit = 32 * 1024 * 1024);
void write_file_atomic(const std::filesystem::path& path, std::string_view data);
std::filesystem::path executable_directory();
std::filesystem::path user_config_directory();
std::string redact(std::string text, const std::filesystem::path& workspace = {});
}

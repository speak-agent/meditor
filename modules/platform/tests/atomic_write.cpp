// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/resource.h>
#include <signal.h>
#endif
import std;
import meditor.platform;
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Atomic file contract failed at line " + std::to_string(at.line()));
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
struct WriteLimit {
    rlimit previous {};
    using Handler = void (*)(int);
    Handler handler {SIG_DFL};
    explicit WriteLimit(rlim_t bytes) {
        if (getrlimit(RLIMIT_FSIZE, &previous)) throw std::runtime_error("Could not read process write limit");
        handler = signal(SIGXFSZ, SIG_IGN);
        if (handler == SIG_ERR) throw std::runtime_error("Could not isolate process write limit signal");
        auto next = previous; next.rlim_cur = bytes;
        if (setrlimit(RLIMIT_FSIZE, &next)) { signal(SIGXFSZ, handler); throw std::runtime_error("Could not set process write limit"); }
    }
    ~WriteLimit() { setrlimit(RLIMIT_FSIZE, &previous); signal(SIGXFSZ, handler); }
};
#endif
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("meditor-atomic-write-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const auto file = root / std::filesystem::u8path("中文.cppm");
    const std::string original = "original\r\n";
    meditor::write_file_atomic(file, original);
    const auto stamp = std::filesystem::last_write_time(file);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    require(std::filesystem::status(file).permissions() == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    for (const auto bytes : {rlim_t{0}, rlim_t{4}}) {
        bool rejected {};
        { WriteLimit limit(bytes); try { meditor::write_file_atomic(file, "replacement content"); } catch (const std::exception&) { rejected = true; } }
        require(rejected && meditor::read_file(file) == original && std::filesystem::last_write_time(file) == stamp);
        require(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1);
    }
#endif
    const std::string replacement(2 * 1024 * 1024 + 19, 'x');
    meditor::write_file_atomic(file, replacement); require(meditor::read_file(file) == replacement);
    meditor::write_file_atomic(file, ""); require(meditor::read_file(file).empty());
#if !defined(_WIN32)
    const auto longFile = root / (std::string(230, 'a') + ".cppm");
    meditor::write_file_atomic(longFile, "first");
    meditor::write_file_atomic(longFile, "replacement");
    require(meditor::read_file(longFile) == "replacement");
#endif
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    std::println("Atomic writes handle chunked and empty data; POSIX process write-limit faults preserve original files and clean partial output: passed");
#else
    std::println("Chunked and empty atomic writes with Unicode paths: passed");
#endif
}

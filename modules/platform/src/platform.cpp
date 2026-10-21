module;
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <signal.h>
#include <fcntl.h>
#include <cerrno>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif
module meditor.platform;
import std;

namespace meditor {
struct Process::Impl {
#if defined(_WIN32)
    HANDLE process {}, input {}, output {}, job {};
#elif !defined(__EMSCRIPTEN__)
    pid_t process {-1}; int input {-1}, output {-1};
#endif
    std::optional<int> code;
};
Process::Process() : impl_(std::make_unique<Impl>()) {}
Process::~Process() { terminate(); }
Process::Process(Process&&) noexcept = default;
Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) { terminate(); impl_ = std::move(other.impl_); }
    return *this;
}
#if defined(_WIN32)
namespace {
std::wstring wide(std::string_view text) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size && !text.empty()) throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring value(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), size); return value;
}
std::wstring quote(std::string_view text) {
    auto value = wide(text); std::wstring result = L"\""; std::size_t slashes {};
    for (auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result += ch;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
}
#endif
void Process::start(const std::vector<std::string>& args, const std::filesystem::path& directory, bool mergeErrors) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (args.empty()) throw std::runtime_error("Empty process command");
    for (const auto& arg : args) if (arg.find('\0') != std::string::npos) throw std::runtime_error("Invalid process argument");
#if defined(__EMSCRIPTEN__)
    throw std::runtime_error("Local tool processes are unavailable in a browser");
#elif defined(_WIN32)
    if (impl_->process) throw std::runtime_error("Process already started");
    SECURITY_ATTRIBUTES attributes {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childInput {}, childOutput {};
    if (!CreatePipe(&childInput, &impl_->input, &attributes, 0)) throw std::runtime_error("Could not create process input");
    if (!CreatePipe(&impl_->output, &childOutput, &attributes, 0)) { CloseHandle(childInput); CloseHandle(impl_->input); impl_->input = nullptr; throw std::runtime_error("Could not create process output"); }
    SetHandleInformation(impl_->input, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(impl_->output, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullError = mergeErrors ? nullptr : CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup {}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput; startup.hStdOutput = childOutput; startup.hStdError = mergeErrors ? childOutput : nullError;
    std::wstring command; for (const auto& arg : args) { if (!command.empty()) command += L' '; command += quote(arg); }
    PROCESS_INFORMATION process {}; auto cwd = directory.wstring();
    bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    CloseHandle(childInput); CloseHandle(childOutput); if (nullError && nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
    if (!started) { CloseHandle(impl_->input); CloseHandle(impl_->output); impl_->input = impl_->output = nullptr; throw std::runtime_error("Tool could not be started"); }
    impl_->process = process.hProcess;
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!impl_->job || !SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(impl_->job, impl_->process)) {
        TerminateProcess(impl_->process, 125); CloseHandle(process.hThread); throw std::runtime_error("Could not isolate the tool process");
    }
    ResumeThread(process.hThread); CloseHandle(process.hThread);
#else
    if (impl_->process > 0) throw std::runtime_error("Process already started");
    int input[2], output[2];
    if (pipe(input)) throw std::runtime_error("Could not create process input");
    if (pipe(output)) { close(input[0]); close(input[1]); throw std::runtime_error("Could not create process output"); }
    std::vector<char*> arguments; for (const auto& arg : args) arguments.push_back(const_cast<char*>(arg.c_str())); arguments.push_back(nullptr);
    impl_->process = fork();
    if (!impl_->process) {
        setpgid(0, 0); dup2(input[0], STDIN_FILENO); dup2(output[1], STDOUT_FILENO);
        if (mergeErrors) dup2(output[1], STDERR_FILENO);
        else { int fd = open("/dev/null", O_WRONLY); if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); } }
        close(input[0]); close(input[1]); close(output[0]); close(output[1]);
        if (!directory.empty() && chdir(directory.c_str())) _exit(125);
        execvp(arguments[0], arguments.data()); _exit(errno == ENOENT ? 127 : 126);
    }
    close(input[0]); close(output[1]);
    if (impl_->process < 0) { close(input[1]); close(output[0]); throw std::runtime_error("Tool could not be started"); }
    setpgid(impl_->process, impl_->process);
    impl_->input = input[1]; impl_->output = output[0];
    fcntl(impl_->input, F_SETFD, FD_CLOEXEC); fcntl(impl_->output, F_SETFD, FD_CLOEXEC);
    fcntl(impl_->output, F_SETFL, fcntl(impl_->output, F_GETFL) | O_NONBLOCK);
#endif
}
void Process::write(std::string_view data) {
#if defined(__EMSCRIPTEN__)
    throw std::runtime_error("Tool processes unavailable");
#else
    while (!data.empty()) {
#if defined(_WIN32)
        DWORD sent {}; if (!impl_->input || !WriteFile(impl_->input, data.data(), static_cast<DWORD>(std::min<std::size_t>(data.size(), 65536)), &sent, nullptr) || !sent) throw std::runtime_error("Tool input closed");
#else
        if (impl_->input < 0) throw std::runtime_error("Tool input closed");
        // Ignore SIGPIPE so a dead language server cannot terminate the editor.
        signal(SIGPIPE, SIG_IGN);
        auto sent = ::write(impl_->input, data.data(), data.size());
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) throw std::runtime_error("Tool input closed");
#endif
        data.remove_prefix(sent);
    }
#endif
}
void Process::close_input() {
    if (!impl_) return;
#if defined(_WIN32)
    if (impl_->input) { CloseHandle(impl_->input); impl_->input = nullptr; }
#elif !defined(__EMSCRIPTEN__)
    if (impl_->input >= 0) { close(impl_->input); impl_->input = -1; }
#endif
}
ProcessRead Process::read(std::chrono::milliseconds timeout) {
    std::array<char, 16384> bytes;
#if defined(__EMSCRIPTEN__)
    return {{}, true};
#elif defined(_WIN32)
    auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        DWORD available {};
        if (!impl_->output || !PeekNamedPipe(impl_->output, nullptr, 0, nullptr, &available, nullptr)) return {{}, true};
        if (available) { DWORD count {}; if (!ReadFile(impl_->output, bytes.data(), std::min<DWORD>(available, bytes.size()), &count, nullptr)) return {{}, true}; return {std::string(bytes.data(), count), false}; }
        if (poll()) return {{}, true};
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return {};
#else
    if (impl_->output < 0) return {{}, true};
    pollfd descriptor {impl_->output, POLLIN, 0};
    int result; do { result = ::poll(&descriptor, 1, static_cast<int>(timeout.count())); } while (result < 0 && errno == EINTR);
    if (!result) return {};
    auto count = ::read(impl_->output, bytes.data(), bytes.size());
    if (count > 0) return {std::string(bytes.data(), count), false};
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return {};
    return {{}, true};
#endif
}
std::optional<int> Process::poll() {
    if (!impl_) return {};
    if (impl_->code) return impl_->code;
#if defined(_WIN32)
    if (!impl_->process || WaitForSingleObject(impl_->process, 0) != WAIT_OBJECT_0) return {};
    DWORD value {}; GetExitCodeProcess(impl_->process, &value); impl_->code = static_cast<int>(value);
#elif !defined(__EMSCRIPTEN__)
    if (impl_->process <= 0) return {};
    int status {}; auto result = waitpid(impl_->process, &status, WNOHANG);
    if (!result || (result < 0 && errno == EINTR)) return {};
    impl_->code = result < 0 ? 125 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    return impl_->code;
}
void Process::terminate() {
    if (!impl_) return;
    close_input();
#if defined(_WIN32)
    if (impl_->job) { CloseHandle(impl_->job); impl_->job = nullptr; }
    if (impl_->process) { if (!poll()) TerminateProcess(impl_->process, 125); WaitForSingleObject(impl_->process, 5000); CloseHandle(impl_->process); impl_->process = nullptr; }
    if (impl_->output) { CloseHandle(impl_->output); impl_->output = nullptr; }
#elif !defined(__EMSCRIPTEN__)
    if (impl_->process > 0) {
        // The process group also owns descendants after the direct child exits.
        kill(-impl_->process, SIGTERM);
        for (int attempt = 0; attempt < 20 && !poll(); ++attempt) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        kill(-impl_->process, SIGKILL);
        if (!impl_->code) { int status {}; while (waitpid(impl_->process, &status, 0) < 0 && errno == EINTR) {} }
        impl_->process = -1;
    }
    if (impl_->output >= 0) { close(impl_->output); impl_->output = -1; }
#endif
}
std::string read_file(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary); if (!input) throw std::runtime_error("Could not open the file");
    std::string result; std::array<char, 16384> buffer;
    while (input) { input.read(buffer.data(), buffer.size()); result.append(buffer.data(), input.gcount()); if (result.size() > limit) throw std::runtime_error("File exceeds the editing limit"); }
    if (!input.eof()) throw std::runtime_error("Could not read the file"); return result;
}
void write_file_atomic(const std::filesystem::path& path, std::string_view data) {
    auto temporary = path; temporary += ".meditor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(std::random_device{}()) + ".tmp";
    try {
        { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output.write(data.data(), static_cast<std::streamsize>(data.size())); output.flush(); if (!output) throw std::runtime_error("Could not write the file"); }
        std::error_code error;
        auto permissions = std::filesystem::status(path, error).permissions();
        if (!error) std::filesystem::permissions(temporary, permissions, error);
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Could not replace the file");
#else
        std::filesystem::rename(temporary, path, error); if (error) throw std::runtime_error("Could not replace the file");
#endif
    } catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
}
std::filesystem::path executable_directory() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0'); auto count = GetModuleFileNameW(nullptr, path.data(), path.size()); if (count && count < path.size()) return std::filesystem::path(path.substr(0, count)).parent_path();
#elif defined(__linux__)
    std::error_code error; auto path = std::filesystem::read_symlink("/proc/self/exe", error); if (!error) return path.parent_path();
#elif defined(__APPLE__)
    std::uint32_t size {}; _NSGetExecutablePath(nullptr, &size); std::string path(size, '\0'); if (!_NSGetExecutablePath(path.data(), &size)) return std::filesystem::weakly_canonical(path.c_str()).parent_path();
#endif
    return {};
}
std::string redact(std::string text, const std::filesystem::path& workspace) {
    auto replace = [&](std::string_view source, std::string_view target) {
        if (source.size() < 3) return;
        for (std::size_t at = 0; (at = text.find(source, at)) != std::string::npos; at += target.size()) text.replace(at, source.size(), target);
    };
    if (!workspace.empty()) { replace(workspace.string(), "<workspace>"); replace(workspace.generic_string(), "<workspace>"); }
    for (const char* variable : {"HOME", "USERPROFILE", "TMPDIR"}) if (auto value = std::getenv(variable)) replace(value, variable == std::string_view("TMPDIR") ? "<temp>" : "<home>");
    for (const char* variable : {"USER", "USERNAME", "HOSTNAME", "COMPUTERNAME"}) if (auto value = std::getenv(variable)) replace(value, "<identity>");
    text = std::regex_replace(text, std::regex(R"((https?://)[^/\s:@]+:[^/\s@]+@)"), "$1<credentials>@");
    text = std::regex_replace(text, std::regex(R"((token|password|secret|api_key|access_token)=([^\s&]+))", std::regex::icase), "$1=<redacted>");
    text = std::regex_replace(text, std::regex(R"(([A-Za-z]:[\\/]|/(?:home|Users|tmp|var|opt|private|usr)/)[^\s\"<>]*)"), "<path>");
    return text;
}
}

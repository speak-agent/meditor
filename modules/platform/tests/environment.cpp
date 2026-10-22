// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.platform;

namespace {
std::optional<std::string> value(const char* key) {
    const auto* text = std::getenv(key);
    return text ? std::optional<std::string>(text) : std::nullopt;
}
void require(bool condition) { if (!condition) throw std::runtime_error("Process environment contract failed"); }
}
int main(int argc, char** argv) {
    if (argc > 2 && std::string_view(argv[1]) == "--directories") {
        const auto root = std::filesystem::u8path(argv[2]);
#if defined(__APPLE__)
        require(meditor::user_config_directory() == root / "home/Library/Application Support");
        require(meditor::user_cache_directory() == root / "home/Library/Caches");
#else
        require(meditor::user_config_directory() == root / "configuration");
        require(meditor::user_cache_directory() == root / "cache");
#endif
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--child") {
        require(value("MEDITOR_ENV_FIXTURE") == "child-only value with spaces");
        require(!value("PATH"));
        std::println("isolated child environment");
        return 0;
    }
    const auto previousFixture = value("MEDITOR_ENV_FIXTURE"), previousPath = value("PATH");
#if defined(_WIN32)
    const auto* pathKey = "path"; // Windows environment names are case-insensitive.
#else
    const auto* pathKey = "PATH";
#endif
    meditor::Process child;
    child.start({std::filesystem::absolute(argv[0]).string(), "--child"}, std::filesystem::current_path(), true,
        {{"MEDITOR_ENV_FIXTURE", "child-only value with spaces"}, {pathKey, std::nullopt}});
    child.close_input();
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        require(std::chrono::steady_clock::now() < deadline);
        auto read = child.read(std::chrono::milliseconds(20)); output += read.data;
        if (read.ended) { if (auto result = child.poll()) { require(*result == 0); break; } }
    }
    require(output.find("isolated child environment") != std::string::npos);
    require(value("MEDITOR_ENV_FIXTURE") == previousFixture && value("PATH") == previousPath);
    const auto locationRoot = std::filesystem::temp_directory_path() / std::filesystem::u8path("meditor-locations-世界-" + std::to_string(std::random_device{}()));
    auto utf8 = [](const std::filesystem::path& path) { const auto bytes = path.u8string(); return std::string(bytes.begin(), bytes.end()); };
    meditor::Process directories;
    directories.start({std::filesystem::absolute(argv[0]).string(), "--directories", utf8(locationRoot)}, std::filesystem::current_path(), true,
        {{"HOME", utf8(locationRoot / "home")}, {"USERPROFILE", utf8(locationRoot / "home")},
         {"APPDATA", utf8(locationRoot / "configuration")}, {"LOCALAPPDATA", utf8(locationRoot / "cache")},
         {"XDG_CONFIG_HOME", utf8(locationRoot / "configuration")}, {"XDG_CACHE_HOME", utf8(locationRoot / "cache")}});
    directories.close_input();
    const auto directoryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        require(std::chrono::steady_clock::now() < directoryDeadline);
        auto read = directories.read(std::chrono::milliseconds(20));
        if (read.ended) { if (auto result = directories.poll()) { require(*result == 0); break; } }
    }
    require(!std::filesystem::exists(locationRoot));
    bool rejected {};
    try { meditor::Process invalid; invalid.start({"unresolved-tool"}, {}, true, {{"MEDITOR_ENV_FIXTURE", "value"}}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    rejected = false;
    try { meditor::Process invalid; invalid.start({std::filesystem::absolute(argv[0]).string()}, {}, true, {{"MEDITOR_ENV_FIXTURE", std::string("safe\0hidden", 11)}}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    rejected = false;
    try { meditor::Process invalid; invalid.start({std::filesystem::absolute(argv[0]).string()}, {}, true, {{"INVALID=KEY", "value"}}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    std::println("Child environment overrides/removal preserve parent and validate executable/key: passed");
}

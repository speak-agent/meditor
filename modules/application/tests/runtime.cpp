// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.runtime;
import meditor.platform;

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Runtime directory contract failed"); }
template<class Function> void rejected(Function function) {
    bool failed {}; try { function(); } catch (const std::exception&) { failed = true; }
    require(failed);
}
}
int main() {
    const auto temporary = std::filesystem::temp_directory_path() / ("meditor-runtime-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(temporary);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup {temporary};
    auto package = temporary / std::filesystem::u8path("便携 package");
    std::filesystem::create_directories(package / "bin");
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":1,"dataDirectory":"data"})");
    const auto first = meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"});
    require(first.portable && first.installationRoot == std::filesystem::weakly_canonical(package));
    require(first.dataRoot == first.installationRoot / "data");
    require(!std::filesystem::exists(first.dataRoot));
    first.prepare_tools();
    require(std::filesystem::is_directory(first.dataRoot / "mcpp"));
    require(std::filesystem::is_directory(first.dataRoot / "cache/mcppls"));
    meditor::write_file_atomic(first.settings_file(), "synthetic settings checkpoint");
    const auto moved = temporary / "relocated package";
    std::filesystem::rename(package, moved); package = moved;
    rejected([&] { first.prepare_tools(); });
    require(!std::filesystem::exists(first.installationRoot));
    const auto second = meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"});
    require(second.dataRoot != first.dataRoot);
    require(meditor::read_file(second.settings_file()) == "synthetic settings checkpoint");
    for (const auto& [key, value] : second.tool_environment()) {
        require(value.has_value());
        require(std::filesystem::u8path(*value).is_absolute());
        require(std::filesystem::u8path(*value).lexically_relative(second.dataRoot).begin()->string() != "..");
    }
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":1,"dataDirectory":"../outside"})");
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"}); });
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin", .dataRoot = temporary / "outside"}); });
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":1,"dataDirectory":"data/../outside"})");
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"}); });
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":1})");
    meditor::write_file_atomic(package / "bin/portable.json", R"({"schemaVersion":1})");
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"}); });
    std::filesystem::remove(package / "bin/portable.json");
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin", .dataRoot = temporary / "outside"}); });
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":2})");
    rejected([&] { meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin"}); });
    std::filesystem::remove(package / "portable.json");
    const auto installed = meditor::RuntimeDirectories::resolve({.binaryDirectory = package / "bin", .dataRoot = temporary / "custom data"});
    require(!installed.portable && !std::filesystem::exists(installed.dataRoot));
    installed.prepare_tools();
    require(std::filesystem::is_directory(installed.dataRoot / "temp"));
    std::println("Portable markers, relocation, data confinement and explicit installed data roots: passed");
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;
import meditor.platform;
import meditor.runtime;

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Portable lifecycle contract failed"); }
}
int main() {
    const auto temporary = std::filesystem::temp_directory_path() / ("meditor-portable-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(temporary);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup {temporary};
    const auto workspace = temporary / "project"; std::filesystem::create_directories(workspace);
    meditor::write_file_atomic(workspace / "user.cppm", "export module user;\n"); // user projects live outside the payload

    auto package = temporary / std::filesystem::u8path(u8"便携 meditor");
    std::filesystem::create_directories(package / "bin");
    meditor::write_file_atomic(package / "portable.json", R"({"schemaVersion":1,"dataDirectory":"data"})");
    meditor::write_file_atomic(package / "bin" / "meditor", "old payload");
    std::filesystem::create_directories(package / "data");
    meditor::write_file_atomic(package / "data" / "settings.json", R"({"schemaVersion":1,"entries":{}})");

    auto payload = temporary / "candidate";
    std::filesystem::create_directories(payload / "bin");
    meditor::write_file_atomic(payload / "portable.json", R"({"schemaVersion":1,"dataDirectory":"data"})");
    meditor::write_file_atomic(payload / "bin" / "meditor", "new payload");

    meditor::Application app(workspace, {}, {}, {.binaryDirectory = package / "bin"});
    auto run = [&](meditor::Json request) { return app.execute(request); };

    // A11: a real candidate swaps in, data survives, no staging leftovers.
    const auto updated = run({{"command", "portable.update"}, {"params", {{"payload", payload.string()}}}});
    require(updated.value("ok", false));
    require(updated["data"].value("updated", false) && updated["data"].value("dataPreserved", false));
    require(meditor::read_file(package / "bin" / "meditor") == "new payload");
    require(meditor::read_file(package / "data" / "settings.json") == R"({"schemaVersion":1,"entries":{}})");
    for (const auto& entry : std::filesystem::directory_iterator(temporary))
        require(entry.path().filename().u8string().find(u8".incoming-") == std::u8string::npos && entry.path().filename().u8string().find(u8".backup-") == std::u8string::npos);

    // An invalid candidate is rejected before any mutation; the installation is untouched.
    const auto broken = temporary / "broken";
    std::filesystem::create_directories(broken / "bin");
    const auto rejected = run({{"command", "portable.update"}, {"params", {{"payload", broken.string()}}}});
    require(!rejected.value("ok", false) && rejected["error"].value("code", "") == "invalid-payload");
    require(meditor::read_file(package / "bin" / "meditor") == "new payload");
    require(std::filesystem::exists(package / "data" / "settings.json"));

    // Uninstall cleans portable data on request and never touches user projects.
    const auto uninstalled = run({{"command", "portable.uninstall"}, {"params", {{"removeData", true}}}});
    require(uninstalled.value("ok", false) && uninstalled["data"].value("dataRemoved", false));
    require(!std::filesystem::exists(package / "data"));
    require(meditor::read_file(workspace / "user.cppm") == "export module user;\n");
    require(std::filesystem::exists(package / "portable.json")); // payload removal stays a user action

    // Installed runtimes reject portable lifecycle operations.
    meditor::Application installed(workspace, {}, {}, {.dataRoot = temporary / "runtime"});
    const auto notPortable = installed.execute({{"command", "portable.update"}, {"params", {{"payload", payload.string()}}}});
    require(!notPortable.value("ok", false) && notPortable["error"].value("code", "") == "not-portable");

    std::println("Portable lifecycle: verified swap, data preservation, staging cleanup, invalid payload rejection, uninstall boundaries: passed");
}

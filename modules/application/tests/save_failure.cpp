// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;
import meditor.platform;
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Save failure contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using meditor::Json;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-save-failure-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    const auto file = root / "main.cppm";
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code ec; std::filesystem::permissions(path / "blocked", std::filesystem::perms::owner_all, ec);
        std::filesystem::permissions(path / "main.cppm", std::filesystem::perms::owner_all, ec);
        std::filesystem::remove_all(path, ec);
    } } cleanup {root};
    meditor::write_file_atomic(file, "original");
    meditor::Application app(root, {}, {}, {.dataRoot = root / "runtime"});
    auto call = [&](std::string command, Json params) { return app.execute({{"command", command}, {"params", std::move(params)}}); };
    auto id = call("document.open", {{"path", "main.cppm"}})["data"]["document"].get<std::string>();
    call("document.edit", {{"document", id}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 8}, {"text", "changed"}}})}});
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);
    const auto stamp = std::filesystem::last_write_time(file);
    auto failed = call("document.save", {{"document", id}, {"revision", 2}});
    require(failed["error"]["code"] == "file-read-only" && failed["effects"].empty());
    auto buffer = call("document.read", {{"document", id}})["data"];
    require(buffer["text"] == "changed" && buffer["revision"] == 2 && buffer["dirty"].get<bool>());
    require(meditor::read_file(file) == "original" && std::filesystem::last_write_time(file) == stamp);
    std::set<std::string> workspaceEntries; for (const auto& item : std::filesystem::directory_iterator(root)) workspaceEntries.insert(meditor::path_to_utf8(item.path().filename()));
    require(workspaceEntries == std::set<std::string>{"main.cppm", "runtime"}); // No leaked save temporaries; runtime/ is the explicit data root.
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    require(call("document.save", {{"document", id}, {"revision", 2}})["ok"].get<bool>());
    require(meditor::read_file(file) == "changed" && call("document.read", {{"document", id}})["data"]["dirty"] == false);
    call("document.edit", {{"document", id}, {"revision", 2}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "retry "}}})}});
    std::filesystem::rename(file, root / "renamed.cppm");
    require(call("document.save", {{"document", id}, {"revision", 3}})["error"]["code"] == "disk-conflict");
    require(!std::filesystem::exists(file) && meditor::read_file(root / "renamed.cppm") == "changed");
    require(call("document.read", {{"document", id}})["data"]["dirty"].get<bool>());
#if !defined(_WIN32)
    const auto blocked = root / "blocked"; std::filesystem::create_directory(blocked);
    meditor::write_file_atomic(blocked / "nested.cppm", "original");
    auto nested = call("document.open", {{"path", "blocked/nested.cppm"}})["data"]["document"].get<std::string>();
    call("document.edit", {{"document", nested}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 8}, {"text", "pending"}}})}});
    std::filesystem::permissions(blocked, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    require(call("document.save", {{"document", nested}, {"revision", 2}})["ok"] == false);
    require(meditor::read_file(blocked / "nested.cppm") == "original");
    require(call("document.read", {{"document", nested}})["data"]["dirty"].get<bool>());
    std::filesystem::permissions(blocked, std::filesystem::perms::owner_all);
    require(call("document.save", {{"document", nested}, {"revision", 2}})["ok"].get<bool>());
    require(meditor::read_file(blocked / "nested.cppm") == "pending");
#endif
    std::println("Read-only save rejection preserves original bytes, pending edits and retry; renamed files are not recreated: passed");
}

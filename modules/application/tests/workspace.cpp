// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;
import meditor.platform;
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Workspace transaction contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using meditor::Json;
    auto base = std::filesystem::temp_directory_path() / ("meditor-switch-" + std::to_string(std::random_device{}()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {base};
    auto first = base / "first", second = base / std::filesystem::u8path("第二 workspace");
    std::filesystem::create_directories(first); std::filesystem::create_directories(second);
    meditor::write_file_atomic(first / "main.cppm", "first");
    meditor::write_file_atomic(second / std::filesystem::u8path("模块.cppm"), "second");
    meditor::Application app(first, {}, {}, {.dataRoot = base / "runtime"});
    auto call = [&](std::string command, Json params = Json::object()) {
        return app.execute({{"command", command}, {"params", std::move(params)}});
    };
    const auto session = app.session_id();
    auto before = call("workspace.canLeave"); require(before["data"]["canLeave"].get<bool>() && before["workspaceGeneration"] == 1);
    auto document = call("document.open", {{"path", "main.cppm"}})["data"]["document"].get<std::string>();
    call("workspace.trust", {{"allow", true}});
    require(call("document.edit", {{"document", document}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "edited "}}})}})["ok"].get<bool>());
    require(call("workspace.canLeave")["data"]["unsavedDocuments"] == Json::array({document}));
    auto params = Json{{"path", meditor::path_to_utf8(second)}, {"generation", 1}};
    require(call("workspace.switch", params)["error"]["code"] == "unsaved-documents");
    require(call("document.read", {{"document", document}})["data"]["text"] == "edited first");
    // Same-root selection is harmless even with dirty documents.
    require(call("workspace.switch", {{"path", "."}, {"generation", 1}})["data"]["changed"] == false);
    require(call("workspace.switch", {{"path", meditor::path_to_utf8(base / "missing")}, {"generation", 1}})["error"]["code"] == "not-found");
    require(call("workspace.switch", {{"path", "main.cppm"}, {"generation", 1}})["error"]["code"] == "invalid-path");
    require(call("document.save", {{"document", document}, {"revision", 2}})["ok"].get<bool>());
    auto switched = call("workspace.switch", params);
    require(switched["ok"].get<bool>() && switched["data"]["changed"].get<bool>() && switched["workspaceGeneration"] == 2);
    require(switched["data"]["listing"]["files"] == Json::array({"模块.cppm"}));
    require(switched["effects"] == Json::array({"workspace-change"}) && app.session_id() == session);
    require(call("workspace.status")["data"]["trusted"] == false && call("workspace.status")["data"]["documents"].empty());
    require(call("document.read", {{"document", document}})["error"]["code"] == "not-found");
    require(call("workspace.switch", params)["error"]["code"] == "workspace-conflict");
    require(app.execute({{"command", "workspace.trust"}, {"workspaceGeneration", 1}, {"params", {{"allow", true}}}})["error"]["code"] == "workspace-conflict");
    auto next = call("document.open", {{"path", "模块.cppm"}}); require(next["ok"].get<bool>() && next["data"]["document"].get<std::string>() != document);
    require(call("document.read", {{"document", next["data"]["document"]}})["data"]["text"] == "second");
    require(meditor::read_file(first / "main.cppm") == "edited first");
    std::println("Workspace switching preserves dirty state and session identity, resets trust and rejects obsolete context: passed");
}

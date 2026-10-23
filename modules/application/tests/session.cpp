// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;
import meditor.session;
import meditor.platform;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Session contract failed at line " + std::to_string(location.line())); }
using meditor::Json;
}
int main() {
    const auto temporary = std::filesystem::temp_directory_path() / ("meditor-session-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(temporary / "workspace");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {temporary};
    const auto workspace = temporary / "workspace";
    const auto dataRoot = temporary / "data";

    meditor::Application application(workspace);
    require(meditor::SessionClient::attach(dataRoot, workspace, std::chrono::milliseconds(300)) == nullptr);

    auto server = std::optional<meditor::SessionServer>(std::in_place, application, dataRoot, workspace);
    require(!server->token().empty());
    require(meditor::SessionClient::attach(temporary / "other-data", workspace, std::chrono::milliseconds(300)) == nullptr);
    bool exclusive {};
    try { auto second = std::optional<meditor::SessionServer>(std::in_place, application, dataRoot, workspace); } catch (const std::exception&) { exclusive = true; }
    require(exclusive);

    auto client = meditor::SessionClient::attach(dataRoot, workspace, std::chrono::seconds(2));
    require(client != nullptr);
    auto opened = client->forward({{"command", "document.open"}, {"params", {{"path", "input.cppm"}, {"create", true}}}});
    require(opened.has_value() && (*opened)["ok"]);
    const auto document = (*opened)["data"]["document"].get<std::string>();

    auto stale = client->forward({{"command", "document.edit"}, {"params", {{"document", document}, {"revision", 0}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "x"}}})}}}});
    require(stale.has_value() && !(*stale)["ok"]);

    const auto status = client->forward({{"command", "document.status"}, {"params", {{"document", document}}}});
    require(status.has_value() && (*status)["ok"]);
    const auto revision = (*status)["data"]["revision"];
    require(application.execute({{"command", "document.edit"}, {"params", {{"document", document}, {"revision", revision}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "host-written "}}})}}}})["ok"]);
    auto read = client->forward({{"command", "document.read"}, {"params", {{"document", document}}}});
    require(read.has_value() && (*read)["ok"] && (*read)["data"]["text"].get<std::string>().starts_with("host-written "));

    auto second_client = meditor::SessionClient::attach(dataRoot, workspace, std::chrono::seconds(2));
    require(second_client != nullptr);
    require(meditor::SessionClient::attach(dataRoot, temporary / "absent-workspace", std::chrono::milliseconds(300)) == nullptr);

    server.reset();
    require(!client->forward({{"command", "workspace.status"}}).has_value());
    require(!std::filesystem::exists(meditor::session_endpoint_file(dataRoot, workspace)));
    require(meditor::SessionClient::attach(dataRoot, workspace, std::chrono::milliseconds(300)) == nullptr);
    std::println("Shared GUI/CLI sessions: attach, exclusive address, shared unsaved buffers, revision rejection and teardown: passed");
}

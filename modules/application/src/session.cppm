// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.session;
import std;
import meditor.platform;
import meditor.application;

export namespace meditor {

// Serves one Application over a line-framed local channel so the GUI and CLI
// processes share documents, tasks and revisions. The endpoint record lives
// under <dataRoot>/sessions/<workspace key>/endpoint.json (owner-only) and is
// removed when the server stops; a crashed server leaves a stale record that
// the next server or client safely reclaims.
class SessionServer {
    struct State;
    std::shared_ptr<State> state_;
public:
    SessionServer(Application& application, const std::filesystem::path& dataRoot, const std::filesystem::path& workspace);
    ~SessionServer();
    SessionServer(const SessionServer&) = delete;
    SessionServer& operator=(const SessionServer&) = delete;
    const std::string& token() const noexcept;
};

// Attaches to a live session for the workspace, or returns nullptr when no
// endpoint answers; callers then fall back to a private local session.
class SessionClient {
    std::unique_ptr<Channel> channel_;
    std::string session_;
    explicit SessionClient(std::unique_ptr<Channel> channel);
public:
    static std::unique_ptr<SessionClient> attach(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace, std::chrono::milliseconds timeout);
    std::optional<Json> forward(const Json& request, std::chrono::milliseconds timeout = std::chrono::seconds(30));
    const std::string& session() const noexcept;
};

// Same endpoint resolution both sides use; exposed for tests.
std::filesystem::path session_endpoint_file(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace);
}

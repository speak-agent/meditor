// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module;
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
module meditor.session;
import std;
import meditor.platform;
import meditor.application;

namespace meditor {
namespace {
std::string workspace_key(const std::filesystem::path& workspace) {
    const auto canonical = std::filesystem::weakly_canonical(workspace).generic_u8string();
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto character : canonical) { hash ^= static_cast<std::uint64_t>(character); hash *= 1099511628211ULL; }
    std::string key;
    for (int shift = 60; shift >= 0; shift -= 4) key += "0123456789abcdef"[(hash >> shift) & 0xF];
    return key;
}
std::string random_token() {
    std::string token;
    for (int index = 0; index < 8; ++index) {
        const std::uint32_t value = std::random_device{}();
        for (int shift = 28; shift >= 0; shift -= 4) token += "0123456789abcdef"[(value >> shift) & 0xF];
    }
    return token;
}
void make_private(const std::filesystem::path& directory) {
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, ignored);
}
std::uint64_t meditor_process_id() {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}
Json error_reply(std::string_view code) {
    return Json{{"schemaVersion", 1}, {"ok", false}, {"error", {{"code", code}}}};
}
}

std::filesystem::path session_endpoint_file(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace) {
    return dataRoot / "sessions" / workspace_key(workspace) / "endpoint.json";
}

struct SessionServer::State {
    std::filesystem::path endpoint;
    std::string token;
    std::unique_ptr<ChannelListener> listener;
    std::stop_source stop;
    std::mutex threadsMutex;
    std::vector<std::jthread> threads;
    ~State() {
        if (!endpoint.empty()) {
            std::error_code ignored;
            std::filesystem::remove(endpoint, ignored);
        }
    }
    void spawn(std::jthread thread) {
        const std::lock_guard<std::mutex> guard(threadsMutex);
        threads.push_back(std::move(thread));
        threads.erase(std::remove_if(threads.begin(), threads.end(), [](const std::jthread& pooled) { return !pooled.joinable(); }), threads.end());
    }
    // Runs before the worker threads release their State references; joining
    // inside ~State would deadlock on that shared ownership.
    void shutdown() {
        stop.request_stop();
        for (;;) {
            std::jthread pooled;
            {
                const std::lock_guard<std::mutex> guard(threadsMutex);
                if (threads.empty()) break;
                pooled = std::move(threads.back());
                threads.pop_back();
            }
            if (pooled.joinable()) pooled.join();
        }
    }
};

SessionServer::SessionServer(Application& application, const std::filesystem::path& dataRoot, const std::filesystem::path& workspace)
        : state_(std::make_shared<State>()) {
    // The listener is the exclusivity gate: bind it before touching any files a
    // competing server might still own.
    state_->token = random_token();
    const auto address = local_channel_address("session-" + workspace_key(workspace));
    state_->listener = std::make_unique<ChannelListener>(address);
    const auto directory = dataRoot / "sessions" / workspace_key(workspace);
    make_private(directory);
    state_->endpoint = directory / "endpoint.json";
    write_file_atomic(state_->endpoint, Json{{"schemaVersion", 1}, {"kind", "meditor.session"}, {"address", address}, {"token", state_->token}, {"pid", meditor_process_id()}}.dump());
    // Handlers must watch the shared State stop source; a jthread's injected
    // token is private to that thread and shutdown would not reach it.
    const auto serve_connection = [state = state_, &application](std::unique_ptr<Channel> connection) {
        const auto stop = state->stop.get_token();
        const auto handshake = connection->read_line(std::chrono::seconds(5), stop);
        bool accepted = false;
        if (handshake) {
            try {
                const auto request = Json::parse(*handshake);
                accepted = request.value("kind", "") == "meditor.session.attach"
                    && request.value("schemaVersion", 0) == 1
                    && request.value("token", "") == state->token;
            } catch (...) { accepted = false; }
        }
        if (!accepted) {
            connection->write_line(error_reply("session-denied").dump());
            return;
        }
        connection->write_line(Json{{"schemaVersion", 1}, {"ok", true}, {"session", application.session_id()}}.dump());
        for (; !stop.stop_requested();) {
            const auto line = connection->read_line(std::chrono::minutes(5), stop);
            if (!line) break;
            Json reply;
            try { reply = application.execute(Json::parse(*line)); }
            catch (...) { reply = error_reply("invalid-json"); }
            if (!connection->write_line(reply.dump())) break;
        }
    };
    auto accept_loop = [state = state_, serve_connection]() {
        for (; !state->stop.stop_requested();) {
            auto connection = state->listener->accept(std::chrono::milliseconds(150), state->stop.get_token());
            if (!connection) continue;
            state->spawn(std::jthread(serve_connection, std::move(connection)));
        }
    };
    state_->spawn(std::jthread(accept_loop));
}
SessionServer::~SessionServer() {
    if (state_) {
        state_->shutdown();
        state_.reset();
    }
}
const std::string& SessionServer::token() const noexcept { return state_->token; }

SessionClient::SessionClient(std::unique_ptr<Channel> channel) : channel_(std::move(channel)) {}

std::unique_ptr<SessionClient> SessionClient::attach(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace, std::chrono::milliseconds timeout) {
    Json endpoint;
    try { endpoint = Json::parse(read_file(session_endpoint_file(dataRoot, workspace), 64 * 1024)); }
    catch (...) { return nullptr; }
    if (endpoint.value("schemaVersion", 0) != 1 || endpoint.value("kind", "") != "meditor.session") return nullptr;
    auto channel = open_channel(endpoint.value("address", ""), timeout);
    if (!channel) return nullptr;
    if (!channel->write_line(Json{{"schemaVersion", 1}, {"kind", "meditor.session.attach"}, {"token", endpoint.value("token", "")}}.dump(), timeout))
        return nullptr;
    const auto reply = channel->read_line(timeout);
    if (!reply) return nullptr;
    try {
        const auto accepted = Json::parse(*reply);
        if (!accepted.value("ok", false)) return nullptr;
        auto client = std::unique_ptr<SessionClient>(new SessionClient(std::move(channel)));
        client->session_ = accepted.value("session", "");
        return client;
    } catch (...) { return nullptr; }
}

std::optional<Json> SessionClient::forward(const Json& request, std::chrono::milliseconds timeout) {
    if (!channel_ || channel_->ended()) return std::nullopt;
    if (!channel_->write_line(request.dump(), timeout)) return std::nullopt;
    const auto reply = channel_->read_line(std::chrono::milliseconds(300000));
    if (!reply) return std::nullopt;
    try { return Json::parse(*reply); }
    catch (...) { return std::nullopt; }
}
const std::string& SessionClient::session() const noexcept { return session_; }
}

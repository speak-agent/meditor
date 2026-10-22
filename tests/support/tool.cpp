// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#endif
import std;
import nlohmann.json;
using Json = nlohmann::json;
void send(Json value) {
    auto body = value.dump(); std::print("Content-Length: {}\r\n\r\n{}", body.size(), body); std::cout.flush();
}
int main(int argc, char** argv) {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const std::string mode = argc > 1 ? argv[1] : "";
    if (const auto* logDirectory = std::getenv("MEDITOR_FIXTURE_ENV_LOG")) {
        Json environment = Json::object();
        for (const auto* key : {"MCPP_HOME", "MCPPLS_CACHE_DIR", "TMPDIR", "TEMP", "TMP"})
            if (const auto* value = std::getenv(key)) environment[key] = value;
        std::filesystem::create_directories(logDirectory);
        std::ofstream output(std::filesystem::path(logDirectory) / (mode == "serve" ? "language.json" : "task.json"));
        output << environment.dump();
    }
    if (argc == 1) return 0;
    if (mode == "sleep") { std::this_thread::sleep_for(std::chrono::seconds(60)); return 0; }
    if (mode == "build") for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "meditor-fixture-long-running") { std::this_thread::sleep_for(std::chrono::seconds(60)); return 0; }
    }
    if (mode == "echo") { for (int i = 2; i < argc; ++i) std::println("{}", argv[i]); return 0; }
    if (mode == "fail") return 17;
    if (mode != "serve") {
        Json args = Json::array(); for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        std::println("{}", Json({{"arguments", args}}).dump()); return 0;
    }
    const auto* faultMode = std::getenv("MEDITOR_FIXTURE_LSP_MODE");
    const std::string fault = faultMode ? faultMode : "";
    std::map<std::string, Json> openDocuments, lastSaved;
    while (true) {
        std::string line; std::size_t length {};
        while (std::getline(std::cin, line)) { if (line == "\r" || line.empty()) break; if (line.starts_with("Content-Length:")) length = std::stoull(line.substr(15)); }
        if (!std::cin || length > 16 * 1024 * 1024) break;
        std::string body(length, '\0'); std::cin.read(body.data(), length); if (!std::cin) break;
        auto request = Json::parse(body); auto method = request.value("method", "");
        if (method == "exit") return 0;
        if (method == "textDocument/didOpen") {
            auto document = request.at("params").at("textDocument");
            openDocuments[document["uri"].get<std::string>()] = {{"version", document["version"]}, {"text", document["text"]}};
            send({{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", document.at("uri")}, {"version", document.at("version")},
                    {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"severity", 2}, {"message", "fixture diagnostic"}}})}}}});
        }
        if (method == "textDocument/didChange") {
            const auto document = request["params"]["textDocument"];
            openDocuments[document["uri"].get<std::string>()] = {{"version", document["version"]}, {"text", request["params"]["contentChanges"][0]["text"]}};
        }
        if (method == "textDocument/didSave") lastSaved[request["params"]["textDocument"]["uri"].get<std::string>()] = openDocuments[request["params"]["textDocument"]["uri"].get<std::string>()];
        if (!request.contains("id")) continue;
        Json result = nullptr;
        if (method == "initialize") result = {{"capabilities", {{"textDocumentSync", 1}, {"positionEncoding", "utf-16"}, {"experimental", {{"cxxModules", {{"version", 1}}}}}}}};
        else if (method == "textDocument/completion") result = {{"isIncomplete", false}, {"items", Json::array({{{"label", "module_value"}, {"kind", 6}}})}};
        else if (method == "textDocument/hover") result = {{"contents", {{"kind", "plaintext"}, {"value", "fixture semantic value"}}}, {"fixture", {{"lastSaved", lastSaved.contains(request["params"]["textDocument"]["uri"].get<std::string>()) ? lastSaved.at(request["params"]["textDocument"]["uri"].get<std::string>()) : Json(nullptr)}}}};
        else if (method == "cxxModules/graph") result = {{"modules", Json::array({{{"name", "fixture"}}})}, {"imports", Json::array()}};
        else if (method == "cxxModules/contexts") result = {{"current", "fixture"}, {"available", Json::array()}};
        else if (method == "textDocument/documentSymbol") result = Json::array();
        send({{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", result}});
        if (method == "textDocument/hover" && fault == "exit-after-hover") return 0;
        if (method == "textDocument/completion" && fault == "flood-and-stall") {
            Json items = Json::array(); for (int i = 0; i < 64; ++i) items.push_back(Json::object());
            for (int i = 0; i < 1000; ++i)
                send({{"jsonrpc", "2.0"}, {"id", "server-" + std::to_string(i)}, {"method", "workspace/configuration"}, {"params", {{"items", items}}}});
            std::this_thread::sleep_for(std::chrono::seconds(10)); return 0;
        }
    }
}

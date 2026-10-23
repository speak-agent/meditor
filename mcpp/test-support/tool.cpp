// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module;
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#endif
module meditor.test.fixture;
import std;
import nlohmann.json;
namespace meditor::testing {
using Json = nlohmann::json;
void send(Json value) {
    auto body = value.dump(); std::print("Content-Length: {}\r\n\r\n{}", body.size(), body); std::cout.flush();
}
void publish(const Json& document, bool verified = true) {
    Json params {{"uri", document.at("uri")}, {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"severity", 2}, {"message", "fixture diagnostic"}}})}};
    if (verified) params["version"] = document.at("version");
    send({{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", std::move(params)}});
}
int run_tool(int argc, char** argv) {
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
    if (mode == "build") {
        std::ifstream configuration(".meditor-fixture-task-mode"); std::string taskMode; std::getline(configuration, taskMode);
        if (taskMode == "silent") return 0;
        if (taskMode == "stream" || taskMode == "stream-fail") {
            std::println("fixture: compiling"); std::cout.flush();
            { std::ofstream started(".meditor-fixture-task-started"); started << "started"; }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!std::filesystem::exists(".meditor-fixture-task-continue")) {
                if (std::chrono::steady_clock::now() >= deadline) return 23;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::println("{}", taskMode == "stream-fail" ? "fixture: compilation failed" : "fixture: linked");
            std::cout.flush(); return taskMode == "stream-fail" ? 17 : 0;
        }
    }
    if (mode != "serve") {
        Json args = Json::array(); for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        std::println("{}", Json({{"arguments", args}}).dump()); return 0;
    }
    const auto* faultMode = std::getenv("MEDITOR_FIXTURE_LSP_MODE");
    std::string fault = faultMode ? faultMode : "";
    if (!faultMode) { std::ifstream file(".meditor-fixture-lsp-mode"); std::getline(file, fault); }
    std::map<std::string, Json> openDocuments, lastSaved;
    bool hierarchicalSymbols {};
    while (true) {
        std::string line; std::size_t length {};
        while (std::getline(std::cin, line)) { if (line == "\r" || line.empty()) break; if (line.starts_with("Content-Length:")) length = std::stoull(line.substr(15)); }
        if (!std::cin || length > 16 * 1024 * 1024) break;
        std::string body(length, '\0'); std::cin.read(body.data(), length); if (!std::cin) break;
        auto request = Json::parse(body); auto method = request.value("method", "");
        if (method == "initialize") hierarchicalSymbols = request["params"]["capabilities"]["textDocument"].value("documentSymbol", Json::object()).value("hierarchicalDocumentSymbolSupport", false);
        if (method == "exit") return 0;
        if (method == "initialize" && fault == "slow-initialize") std::this_thread::sleep_for(std::chrono::seconds(3));
        if (method == "textDocument/didOpen") {
            auto document = request.at("params").at("textDocument");
            openDocuments[document["uri"].get<std::string>()] = {{"version", document["version"]}, {"text", document["text"]}};
            publish(document, fault != "gui-unversioned");
        }
        if (method == "textDocument/didChange") {
            const auto document = request["params"]["textDocument"];
            openDocuments[document["uri"].get<std::string>()] = {{"version", document["version"]}, {"text", request["params"]["contentChanges"][0]["text"]}};
            publish(document, fault != "gui-unversioned");
        }
        if (method == "textDocument/didSave") lastSaved[request["params"]["textDocument"]["uri"].get<std::string>()] = openDocuments[request["params"]["textDocument"]["uri"].get<std::string>()];
        if (!request.contains("id")) continue;
        Json result = nullptr;
        if (method == "initialize") result = {{"capabilities", {{"textDocumentSync", 1}, {"positionEncoding", "utf-16"}, {"experimental", {{"cxxModules", {{"version", 1}}}}}}}};
        else if (method == "textDocument/completion") result = {{"isIncomplete", false}, {"items", Json::array({{{"label", "module_value"}, {"kind", 6}}})}};
        else if (method == "textDocument/hover") result = {{"contents", {{"kind", "plaintext"}, {"value", "fixture semantic value"}}}, {"fixture", {{"lastSaved", lastSaved.contains(request["params"]["textDocument"]["uri"].get<std::string>()) ? lastSaved.at(request["params"]["textDocument"]["uri"].get<std::string>()) : Json(nullptr)}}}};
        else if (method == "textDocument/signatureHelp") result = {{"signatures", Json::array({{{"label", "fixture_fn(int a, int b)"}, {"parameters", Json::array({{{"label", "int a"}}, {{"label", "int b"}}})}, {"activeParameter", 0}}})}, {"activeSignature", 0}};
        else if (method == "textDocument/semanticTokens/range") result = {{"data", Json::array({Json::array({1, 11, 5, 0, 0}), Json::array({0, 15, 2, 4, 0})})}};
        else if ((method == "textDocument/definition" || method == "textDocument/references") && (fault == "nav-locations" || fault == "gui-nav")) {
            const auto source = request["params"]["textDocument"]["uri"].get<std::string>();
            const auto target = source.substr(0, source.rfind('/') + 1) + "target.cppm";
            const Json range = fault == "gui-nav" ? Json{{"start", {{"line", 1}, {"character", 11}}}, {"end", {{"line", 1}, {"character", 16}}}} :
                Json{{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 6}}}};
            if (method == "textDocument/definition") result = {{"targetUri", target}, {"targetRange", range}, {"targetSelectionRange", range}};
            else result = Json::array({{{"uri", target}, {"range", range}},
                {{"uri", source}, {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}},
                {{"uri", "file:///synthetic-outside/private.cppm"}, {"range", range}}});
        }
        else if (method == "cxxModules/graph") result = {{"modules", Json::array({{{"name", "fixture"}}})}, {"imports", Json::array()}};
        else if (method == "cxxModules/contexts") result = {{"current", "fixture"}, {"available", Json::array()}};
        else if (method == "textDocument/documentSymbol") {
            result = Json::array();
            if (fault.starts_with("gui-")) {
                const Json range {{"start", {{"line", 1}, {"character", 11}}}, {"end", {{"line", 1}, {"character", 16}}}};
                result.push_back({{"name", "value🙂"}, {"kind", 12}, {"range", range}, {"selectionRange", range}});
                if (!hierarchicalSymbols) {
                    result[0]["location"] = {{"uri", request["params"]["textDocument"]["uri"]}, {"range", range}};
                    result[0].erase("range"); result[0].erase("selectionRange");
                }
                if (fault == "gui-many-symbols") for (int i = 1; i < 210; ++i) result.push_back(result[0]);
                if (fault == "gui-invalid-symbols") result = Json::object();
                if (fault == "gui-slow-symbols" || fault == "gui-slow-edit") {
                    { std::ofstream marker(".meditor-fixture-symbol-requested"); marker << "requested"; }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
        if (method == "textDocument/completion") result["fixture"] = {{"document", openDocuments[request["params"]["textDocument"]["uri"].get<std::string>()]}};
        if (method == "textDocument/hover" && fault == "ignore-hover") continue;
        if (method == "textDocument/hover" && (fault == "private-error" || fault == "long-private-error")) {
            std::string message = "failure /home/synthetic-user/work token=synthetic-secret ";
            for (int i = 0; i < (fault == "long-private-error" ? 1600 : 400); ++i) message += "🙂";
            send({{"jsonrpc", "2.0"}, {"id", request["id"]}, {"error", {{"code", -32603}, {"message", message}}}}); continue;
        }
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
}

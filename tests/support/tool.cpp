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
    if (argc == 1) return 0;
    std::string mode = argv[1];
    if (mode == "sleep") { std::this_thread::sleep_for(std::chrono::seconds(60)); return 0; }
    if (mode == "echo") { for (int i = 2; i < argc; ++i) std::println("{}", argv[i]); return 0; }
    if (mode == "fail") return 17;
    if (mode != "serve") {
        Json args = Json::array(); for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        std::println("{}", Json({{"arguments", args}}).dump()); return 0;
    }
    while (true) {
        std::string line; std::size_t length {};
        while (std::getline(std::cin, line)) { if (line == "\r" || line.empty()) break; if (line.starts_with("Content-Length:")) length = std::stoull(line.substr(15)); }
        if (!std::cin || length > 16 * 1024 * 1024) break;
        std::string body(length, '\0'); std::cin.read(body.data(), length); if (!std::cin) break;
        auto request = Json::parse(body); auto method = request.value("method", "");
        if (method == "exit") return 0;
        if (method == "textDocument/didOpen") {
            auto document = request.at("params").at("textDocument");
            send({{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", document.at("uri")}, {"version", document.at("version")},
                    {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"severity", 2}, {"message", "fixture diagnostic"}}})}}}});
        }
        if (!request.contains("id")) continue;
        Json result = nullptr;
        if (method == "initialize") result = {{"capabilities", {{"textDocumentSync", 1}, {"positionEncoding", "utf-16"}, {"experimental", {{"cxxModules", {{"version", 1}}}}}}}};
        else if (method == "textDocument/completion") result = {{"isIncomplete", false}, {"items", Json::array({{{"label", "module_value"}, {"kind", 6}}})}};
        else if (method == "textDocument/hover") result = {{"contents", {{"kind", "plaintext"}, {"value", "fixture semantic value"}}}};
        else if (method == "cxxModules/graph") result = {{"modules", Json::array({{{"name", "fixture"}}})}, {"imports", Json::array()}};
        else if (method == "cxxModules/contexts") result = {{"current", "fixture"}, {"available", Json::array()}};
        else if (method == "textDocument/documentSymbol") result = Json::array();
        send({{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", result}});
    }
}

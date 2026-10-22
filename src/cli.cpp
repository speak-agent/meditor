// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.cli;
import std;
import nlohmann.json;
import meditor.application;
import meditor.platform;
import meditor.settings;

namespace {
template<class... Args>
void print_line(std::ostream& output, std::string_view format, Args&&... args) {
    output << std::vformat(format, std::make_format_args(args...)) << '\n';
}
template<class... Args>
void print_line(std::string_view format, Args&&... args) {
    print_line(std::cout, format, std::forward<Args>(args)...);
}
meditor::i18n::Localizer command_localizer(const std::optional<std::string>& languageOverride,
        std::filesystem::path settingsFile, const meditor::RuntimeOptions& runtime) {
    std::string preference = languageOverride.value_or("auto");
    if (!languageOverride) {
        try {
            if (settingsFile.empty()) settingsFile = meditor::RuntimeDirectories::resolve(runtime).settings_file();
            preference = meditor::SettingsStore(settingsFile).get().at("values").at("language").get<std::string>();
        } catch (...) {} // Help and diagnostics stay usable with an unreadable store.
    }
    return meditor::i18n::Localizer(meditor::i18n::select_language(preference, meditor::system_language_tag()));
}
enum class RequestLine { End, Ready, TooLarge };
RequestLine read_request_line(std::istream& input, std::string& line) {
    constexpr std::size_t MAX_REQUEST_BYTES {8 * 1024 * 1024};
    line.clear();
    bool oversized {}, consumed {};
    auto* buffer = input.rdbuf();
    for (;;) {
        const auto value = buffer->sbumpc();
        if (std::char_traits<char>::eq_int_type(value, std::char_traits<char>::eof())) {
            if (!consumed) return RequestLine::End;
            break;
        }
        consumed = true;
        const auto character = std::char_traits<char>::to_char_type(value);
        if (character == '\n') break;
        if (line.size() < MAX_REQUEST_BYTES) line += character;
        else oversized = true;
    }
    return oversized ? RequestLine::TooLarge : RequestLine::Ready;
}
}

int meditor::cli::run(int argc, char** argv) {
    using meditor::Json;
    meditor::i18n::Localizer localizer(meditor::system_language_tag());
    try {
        std::filesystem::path workspace = std::filesystem::current_path(), tools, settings;
        meditor::RuntimeOptions runtime;
        std::optional<std::string> languageOverride;
        std::string command = "capabilities"; Json params = Json::object(); bool serve = false, json = false, trust = false, showHelp = false, stdio = false;
        std::vector<std::string> operation;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto next = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Option requires a value"); return argv[i]; };
            if (arg == "--workspace") workspace = next();
            else if (arg == "--tool-root") tools = next();
            else if (arg == "--settings-file") settings = next();
            else if (arg == "--data-root") runtime.dataRoot = std::filesystem::u8path(next());
            else if (arg == "--portable") runtime.portable = true;
            else if (arg == "--language") {
                languageOverride = next();
                localizer = meditor::i18n::Localizer(meditor::i18n::select_language(*languageOverride, meditor::system_language_tag()));
            }
            else if (arg == "--params") params = Json::parse(next());
            else if (arg == "--format") { if (next() != "json") throw std::runtime_error("Supported format: json"); json = true; }
            else if (arg == "--trust") trust = true;
            else if (arg == "--stdio") stdio = true;
            else if (arg == "--version") { print_line("meditor 0.0.1"); return 0; }
            else if (arg == "--help" || arg == "help") showHelp = true;
            else if (arg.starts_with('-')) throw std::runtime_error("Unknown command option");
            else operation.push_back(std::move(arg));
        }
        if (operation.size() == 1) command = operation.front();
        else if (operation.size() == 2) {
            const auto& group = operation.front();
            if (group != "workspace" && group != "document" && group != "task" && group != "language" && group != "modules" && group != "project" && group != "package" && group != "toolchain" && group != "environment" && group != "settings") throw std::runtime_error("Unknown command group");
            command = group + "." + operation.back();
        } else if (!operation.empty()) throw std::runtime_error("Too many command words");
        serve = command == "serve";
        if (!showHelp && stdio != serve) throw std::runtime_error("serve requires --stdio");
        localizer = command_localizer(languageOverride, settings, runtime);
        if (showHelp) { print_line("{}", localizer.text("cli.help")); return 0; }
        meditor::Application application(workspace, tools, settings, runtime);
        if (trust) application.execute({{"command", "workspace.trust"}, {"params", {{"allow", true}}}});
        if (serve) {
            std::string line;
            for (;;) {
                const auto state = read_request_line(std::cin, line);
                if (state == RequestLine::End) break;
                if (state == RequestLine::TooLarge) {
                    print_line("{}", Json({{"schemaVersion", 1}, {"ok", false}, {"error", {{"code", "request-too-large"}}}}).dump());
                    std::cout.flush(); continue;
                }
                try { print_line("{}", application.execute(Json::parse(line)).dump()); }
                catch (...) { print_line("{}", Json({{"schemaVersion", 1}, {"ok", false}, {"error", {{"code", "invalid-json"}}}}).dump()); }
                std::cout.flush();
            }
            return 0;
        }
        auto result = application.execute({{"command", command}, {"params", params}});
        if (result.value("ok", false) && result["data"].is_object() && result["data"].contains("task")) {
            auto task = result["data"]["task"];
            do { result = application.execute({{"command", "task.wait"}, {"params", {{"task", task}, {"timeoutMs", 120000}}}}); } while (!result.value("ok", false) && result.value("error", Json::object()).value("code", "") == "timeout");
        }
        if (json) print_line("{}", result.dump());
        else if (result.value("ok", false) && command == "about") {
            const auto& info = result.at("data"); const auto& product = info.at("product");
            print_line("{} {}\n{}\n{}", product.at("name").get<std::string>(), product.at("version").get<std::string>(), localizer.text(product.at("descriptionId").get<std::string>()), product.at("license").get<std::string>());
            print_line("{}: {}\n{}: {}", localizer.text("about.contributors"), product.at("contributors").get<std::string>(), localizer.text("about.contact"), product.at("contact").get<std::string>());
            print_line("{}", product.at("sourceUrl").is_string() ? product.at("sourceUrl").get<std::string>() : product.at("repository").get<std::string>());
            if (product.at("sourceCommit").is_null()) print_line("{}", localizer.text("about.sourcePending"));
            print_line("\n{}", localizer.text("about.dependencies"));
            for (const auto* group : {"libraries", "tools"}) for (const auto& entry : info.at(group)) {
                const auto version = entry.at("version").is_string() ? entry.at("version").get<std::string>() : localizer.text("about.unknown");
                print_line("{} · {} · {}", entry.at("id").get<std::string>(), version, entry.at("license").get<std::string>());
                if (entry.contains("state")) {
                    const auto state = entry.at("state").get<std::string>();
                    print_line("  {}", localizer.text(state == "recorded" ? "about.state.recorded" : state == "missing" ? "about.state.missing" : "about.state.unverified"));
                }
            }
            print_line("\n{}", localizer.text("about.scope"));
        }
        else if (result.value("ok", false)) print_line("{}", result.at("data").dump(2));
        else {
            const auto code = result["error"].value("code", "operation-failed"), id = "error." + code;
            print_line(std::cerr, "{}: {}", code, localizer.text(localizer.contains(id) ? id : "error.operation-failed"));
        }
        if (!result.value("ok", false)) return 1;
        if (result["data"].is_object() && result["data"].contains("exitCode")) return result["data"]["exitCode"].get<int>() == 0 ? 0 : 1;
        return 0;
    } catch (...) { print_line(std::cerr, "{}", localizer.text("cli.invalidArguments")); return 2; }
}

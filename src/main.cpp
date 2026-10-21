import std;
import nlohmann.json;
import meditor.application;

int main(int argc, char** argv) {
    using meditor::Json;
    try {
        std::filesystem::path workspace = std::filesystem::current_path(), tools;
        std::string command = "capabilities"; Json params = Json::object(); bool serve = false, json = false, trust = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto next = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Option requires a value"); return argv[i]; };
            if (arg == "--workspace") workspace = next();
            else if (arg == "--tool-root") tools = next();
            else if (arg == "--params") params = Json::parse(next());
            else if (arg == "--format") { if (next() != "json") throw std::runtime_error("Supported format: json"); json = true; }
            else if (arg == "--trust") trust = true;
            else if (arg == "--stdio") {}
            else if (arg == "--version") { std::println("meditor 0.0.1"); return 0; }
            else if (arg == "--help" || arg == "help") {
                std::println("meditor 0.0.1\nUsage: meditor <command> [--workspace DIR] [--params JSON] [--format json]\n       meditor serve --stdio [--workspace DIR]\n\nUse capabilities to list commands. Commands share the GUI application core.\nTool execution requires --trust or an explicit workspace.trust request.\nDocument edits require the current revision. serve retains documents and tasks.\n--tool-root DIR selects a complete local tool payload for development."); return 0;
            } else if (arg.starts_with('-')) throw std::runtime_error("Unknown command option");
            else { command = arg; serve = command == "serve"; }
        }
        meditor::Application application(workspace, tools);
        if (trust) application.execute({{"command", "workspace.trust"}, {"params", {{"allow", true}}}});
        if (serve) {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line.size() > 8 * 1024 * 1024) { std::println("{}", Json({{"schemaVersion", 1}, {"ok", false}, {"error", {{"code", "request-too-large"}}}}).dump()); continue; }
                try { std::println("{}", application.execute(Json::parse(line)).dump()); }
                catch (...) { std::println("{}", Json({{"schemaVersion", 1}, {"ok", false}, {"error", {{"code", "invalid-json"}}}}).dump()); }
                std::cout.flush();
            }
            return 0;
        }
        auto result = application.execute({{"command", command}, {"params", params}});
        if (result.value("ok", false) && result["data"].is_object() && result["data"].contains("task")) {
            auto task = result["data"]["task"];
            do { result = application.execute({{"command", "task.wait"}, {"params", {{"task", task}, {"timeoutMs", 120000}}}}); } while (!result.value("ok", false) && result.value("error", Json::object()).value("code", "") == "timeout");
        }
        if (json) std::println("{}", result.dump());
        else if (result.value("ok", false)) std::println("{}", result.at("data").dump(2));
        else std::println(std::cerr, "{}: {}", result["error"].value("code", "error"), result["error"].value("message", "Command failed"));
        if (!result.value("ok", false)) return 1;
        if (result["data"].is_object() && result["data"].contains("exitCode")) return result["data"]["exitCode"].get<int>() == 0 ? 0 : 1;
        return 0;
    } catch (...) { std::println(std::cerr, "meditor: invalid arguments or unavailable workspace; use --help"); return 2; }
}

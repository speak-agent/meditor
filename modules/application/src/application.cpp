module meditor.application;
import std;
import nlohmann.json;
import meditor.document;
import meditor.platform;
import meditor.settings;

namespace meditor {
namespace {
using Clock = std::chrono::steady_clock;
struct CommandError : std::runtime_error {
    std::string code;
    CommandError(std::string kind, std::string message) : std::runtime_error(std::move(message)), code(std::move(kind)) {}
};
std::string make_uri(const std::filesystem::path& path) {
    auto source = std::filesystem::absolute(path).generic_string();
    std::string result = source.starts_with('/') ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : source) {
        if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~') result += ch;
        else { result += '%'; result += hex[ch >> 4]; result += hex[ch & 15]; }
    }
    return result;
}
Json clean(Json value, const std::filesystem::path& root) {
    if (value.is_string()) return redact(value.get<std::string>(), root);
    if (value.is_array()) for (auto& item : value) item = clean(std::move(item), root);
    if (value.is_object()) for (auto& item : value.items()) item.value() = clean(std::move(item.value()), root);
    return value;
}
class Language {
    Process process_;
    std::mutex mutex_;
    std::mutex writerMutex_;
    std::condition_variable ready_;
    std::map<std::int64_t, Json> replies_;
    std::map<std::string, Json> diagnostics_;
    Json status_;
    std::string failure_;
    Json serverCapabilities_;
    std::atomic<bool> closed_ {false};
    std::atomic<std::int64_t> next_ {1};
    std::jthread reader_;
    void send_(Json message) {
        auto body = message.dump(); std::lock_guard lock(writerMutex_);
        process_.write("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    }
    void receive_(std::stop_token stop) {
        try {
            std::string buffer;
            while (!stop.stop_requested()) {
                auto read = process_.read(std::chrono::milliseconds(100)); buffer += read.data;
                if (buffer.size() > 32 * 1024 * 1024) throw std::runtime_error("Language message exceeds limit");
                while (true) {
                    auto end = buffer.find("\r\n\r\n"); if (end == std::string::npos) break;
                    auto header = buffer.substr(0, end); auto at = header.find("Content-Length:");
                    if (at == std::string::npos) throw std::runtime_error("Invalid language frame");
                    auto length = std::stoull(header.substr(at + 15));
                    if (length > 16 * 1024 * 1024) throw std::runtime_error("Language message exceeds limit");
                    if (buffer.size() < end + 4 + length) break;
                    auto message = Json::parse(buffer.substr(end + 4, length)); buffer.erase(0, end + 4 + length);
                    if (message.contains("method") && message.contains("id")) {
                        auto method = message.value("method", ""); Json result = nullptr;
                        if (method == "workspace/configuration") {
                            result = Json::array(); for (auto& item : message.value("params", Json::object()).value("items", Json::array())) { (void)item; result.push_back(Json::object()); }
                        }
                        if (method == "workspace/applyEdit") result = {{"applied", false}, {"failureReason", "Use an explicit meditor edit transaction"}};
                        send_({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}});
                    } else if (message.contains("id") && message["id"].is_number_integer()) {
                        const auto id = message["id"].get<std::int64_t>();
                        std::lock_guard lock(mutex_); replies_[id] = std::move(message); ready_.notify_all();
                    } else if (message.value("method", "") == "textDocument/publishDiagnostics") {
                        auto params = message.at("params"); auto uri = params.value("uri", "");
                        std::lock_guard lock(mutex_); diagnostics_[uri] = std::move(params);
                    } else if (message.value("method", "") == "cxxModules/status") {
                        std::lock_guard lock(mutex_); status_ = message.at("params");
                    }
                }
                if (read.ended) break;
            }
        } catch (const std::exception& error) { std::lock_guard lock(mutex_); failure_ = error.what(); }
        closed_ = true; ready_.notify_all();
    }
public:
    Language(const std::filesystem::path& tool, const std::filesystem::path& driver, const std::filesystem::path& root) {
        process_.start({tool.string(), "serve"}, root, false);
        reader_ = std::jthread([this](std::stop_token stop) { receive_(stop); });
        try {
            auto init = request("initialize", {{"processId", nullptr}, {"rootUri", make_uri(root)}, {"clientInfo", {{"name", "meditor"}, {"version", "0.0.1"}}},
                {"workspaceFolders", Json::array({{{"uri", make_uri(root)}, {"name", "workspace"}}})},
                {"initializationOptions", {{"mcpp", driver.string()}, {"toolEnvironment", "editor"}}},
                {"capabilities", {{"general", {{"positionEncodings", Json::array({"utf-16"})}}}, {"workspace", {{"configuration", true}}},
                    {"textDocument", {{"synchronization", {{"didSave", true}}}, {"completion", {{"completionItem", {{"snippetSupport", false}}}}}}},
                    {"experimental", {{"cxxModules", {{"version", 1}, {"status", true}, {"graph", true}, {"contexts", true}}}}}}}}, 30000);
            serverCapabilities_ = init.value("capabilities", Json::object());
            notify("initialized", Json::object());
        } catch (...) { reader_.request_stop(); if (reader_.joinable()) reader_.join(); process_.terminate(); throw; }
    }
    ~Language() { reader_.request_stop(); if (reader_.joinable()) reader_.join(); process_.terminate(); }
    Json request(std::string method, Json params, int timeout = 15000) {
        auto id = next_++; send_({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
        std::unique_lock lock(mutex_);
        if (!ready_.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return replies_.contains(id) || closed_; })) {
            lock.unlock(); notify("$/cancelRequest", {{"id", id}}); throw CommandError("timeout", "Language request timed out");
        }
        if (!replies_.contains(id)) throw CommandError("tool-unavailable", "Language server stopped" + (failure_.empty() ? std::string() : ": " + failure_));
        auto response = std::move(replies_.at(id)); replies_.erase(id);
        if (response.contains("error")) throw CommandError("language-error", response["error"].value("message", "Language request failed"));
        return response.value("result", Json(nullptr));
    }
    void notify(std::string method, Json params) { send_({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}); }
    Json diagnostics() { std::lock_guard lock(mutex_); Json values = Json::array(); for (auto& [uri, item] : diagnostics_) values.push_back(item); return values; }
    Json status() { std::lock_guard lock(mutex_); return status_; }
    bool modules_supported() const { return serverCapabilities_.value("experimental", Json::object()).contains("cxxModules"); }
};
struct Task {
    std::mutex mutex;
    std::condition_variable changed;
    std::string state {"running"};
    std::string output;
    int code {-1};
    bool truncated {};
    std::jthread worker;
    ~Task() { worker.request_stop(); if (worker.joinable()) worker.join(); }
};
struct Record {
    std::string path;
    Document document;
    bool existed {};
    std::uint64_t synced {};
    int lspVersion {};
    Record(std::string name, std::string text, bool exists) : path(std::move(name)), document(std::move(text)), existed(exists) {}
};
}
struct Application::Impl {
    std::filesystem::path root;
    std::filesystem::path tools;
    std::string session {"s" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())};
    std::map<std::string, std::unique_ptr<Record>> documents;
    std::map<std::string, std::unique_ptr<Task>> tasks;
    std::unique_ptr<Language> language;
    std::unique_ptr<SettingsStore> settings;
    bool trusted {};
    std::uint64_t nextDocument {1}, nextTask {1};
    std::filesystem::path resolve(std::string value) const {
        std::filesystem::path path(value);
        if (path.is_absolute() || value.find('\0') != std::string::npos) throw CommandError("invalid-path", "Use a workspace-relative file path");
        auto resolved = std::filesystem::weakly_canonical(root / path);
        auto relative = resolved.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..") throw CommandError("invalid-path", "Path is outside the workspace");
        return resolved;
    }
    std::filesystem::path tool(const std::string& name) const {
        std::string executable = name;
#ifdef _WIN32
        executable += ".exe";
#endif
        for (const auto& candidate : {tools / name / "bin" / executable, tools / "bin" / executable, tools / executable}) if (std::filesystem::is_regular_file(candidate)) return std::filesystem::absolute(candidate);
        throw CommandError("tool-unavailable", "The bundled " + name + " tool is unavailable; use meditor doctor");
    }
    Record& record(const Json& params) {
        auto id = params.at("document").get<std::string>();
        if (!documents.contains(id)) throw CommandError("not-found", "Document is not open in this session");
        return *documents.at(id);
    }
    Json summary(const std::string& id, const Record& record, bool includeText = false) const {
        Json result = {{"document", id}, {"path", record.path}, {"revision", record.document.revision()}, {"dirty", record.document.dirty()}, {"bytes", record.document.size()}};
        if (includeText) result["text"] = record.document.text();
        return result;
    }
    Json open(const Json& params) {
        auto path = resolve(params.at("path").get<std::string>()); auto name = path.lexically_relative(root).generic_string();
        for (const auto& [id, record] : documents) if (record->path == name) return summary(id, *record);
        bool exists = std::filesystem::exists(path);
        if (!exists && !params.value("create", false)) throw CommandError("not-found", "File does not exist");
        auto id = "d" + std::to_string(nextDocument++);
        auto record = std::make_unique<Record>(name, exists ? read_file(path) : std::string(), exists);
        auto result = summary(id, *record); documents.emplace(id, std::move(record)); return result;
    }
    void require_trust() const { if (!trusted) throw CommandError("workspace-untrusted", "Explicitly trust this workspace before executing tools"); }
    void sync(Record& record) {
        require_trust();
        if (!language) language = std::make_unique<Language>(tool("mcppls"), tool("mcpp"), root);
        auto uri = make_uri(resolve(record.path));
        if (!record.synced) language->notify("textDocument/didOpen", {{"textDocument", {{"uri", uri}, {"languageId", "cpp"}, {"version", ++record.lspVersion}, {"text", record.document.text()}}}});
        else if (record.synced != record.document.revision()) language->notify("textDocument/didChange", {{"textDocument", {{"uri", uri}, {"version", ++record.lspVersion}}}, {"contentChanges", Json::array({{{"text", record.document.text()}}})}});
        record.synced = record.document.revision();
    }
    Json task_status(const std::string& id) {
        if (!tasks.contains(id)) throw CommandError("not-found", "Task does not exist");
        auto& task = *tasks.at(id); std::lock_guard lock(task.mutex);
        return {{"task", id}, {"state", task.state}, {"exitCode", task.code}, {"output", redact(task.output, root)}, {"truncated", task.truncated}};
    }
    Json start_task(std::vector<std::string> arguments, int timeout) {
        require_trust();
        for (auto& [id, task] : tasks) { std::lock_guard lock(task->mutex); if (task->state == "running") throw CommandError("task-busy", "Wait for or cancel the active workspace task"); }
        if (tasks.size() >= 32) {
            auto completed = tasks.end();
            for (auto it = tasks.begin(); it != tasks.end(); ++it) {
                std::lock_guard lock(it->second->mutex);
                if (it->second->state != "running") { completed = it; break; }
            }
            if (completed != tasks.end()) tasks.erase(completed);
        }
        auto id = "t" + std::to_string(nextTask++); auto task = std::make_unique<Task>(); auto raw = task.get(); auto cwd = root;
        task->worker = std::jthread([raw, cwd, arguments = std::move(arguments), timeout](std::stop_token stop) {
            int code = 125; std::string state = "failed";
            try {
                Process process; process.start(arguments, cwd); process.close_input(); auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
                while (true) {
                    auto read = process.read(std::chrono::milliseconds(30));
                    { std::lock_guard lock(raw->mutex); auto room = 2 * 1024 * 1024 - raw->output.size(); raw->output += read.data.substr(0, room); raw->truncated = raw->truncated || read.data.size() > room; }
                    if (stop.stop_requested()) { state = "cancelled"; process.terminate(); code = 130; break; }
                    if (Clock::now() > deadline) { state = "timed-out"; process.terminate(); code = 124; break; }
                    if (read.ended) { auto result = process.poll(); if (result) { code = *result; state = code ? "failed" : "succeeded"; break; } }
                }
            } catch (...) { std::lock_guard lock(raw->mutex); raw->output += "Tool execution failed"; }
            { std::lock_guard lock(raw->mutex); raw->code = code; raw->state = state; } raw->changed.notify_all();
        });
        tasks.emplace(id, std::move(task)); return {{"task", id}, {"state", "running"}};
    }
    Json dispatch(const std::string& command, const Json& params) {
        if (command == "settings.get") return settings->get();
        if (command == "settings.update") {
            if (!params.at("revision").is_number_unsigned() && params.at("revision").get<std::int64_t>() < 0)
                throw CommandError("invalid-params", "Settings revision must be nonnegative");
            return settings->update(params.at("revision").get<std::uint64_t>(), params.at("changes"));
        }
        if (command == "workspace.status") {
            Json docs = Json::array(); for (const auto& [id, record] : documents) docs.push_back(summary(id, *record));
            return {{"name", root.filename().string()}, {"trusted", trusted}, {"documents", docs}, {"language", language ? language->status() : Json(nullptr)}};
        }
        if (command == "workspace.trust") { trusted = params.at("allow").get<bool>(); if (!trusted) { for (auto& [id, task] : tasks) task->worker.request_stop(); language.reset(); for (auto& [id, record] : documents) record->synced = 0; } return {{"trusted", trusted}}; }
        if (command == "workspace.files") {
            Json result = Json::array(); std::error_code error;
            auto has_entry = []<class Iterator>(const Iterator& iterator) {
                if constexpr (requires { iterator != std::default_sentinel; }) return iterator != std::default_sentinel;
                else return iterator != Iterator{};
            };
            for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error); has_entry(it) && result.size() < 3000; it.increment(error)) {
                if (error) { error.clear(); continue; }
                auto name = it->path().filename().string();
                if (it->is_directory(error) && (name.starts_with('.') || name == "target" || name == "node_modules" || name == "build")) { it.disable_recursion_pending(); continue; }
                if (it->is_regular_file(error) && !it->is_symlink(error)) result.push_back(it->path().lexically_relative(root).generic_string());
            }
            return {{"files", result}, {"truncated", result.size() >= 3000}};
        }
        if (command == "document.open") return open(params);
        if (command.starts_with("document.")) {
            auto& record = this->record(params); auto id = params.at("document").get<std::string>();
            if (command == "document.read") return summary(id, record, true);
            auto revision = params.at("revision").get<std::uint64_t>();
            if (record.document.revision() != revision) throw CommandError("revision-conflict", "Document changed; read the current revision before editing");
            if (command == "document.edit") {
                std::vector<Edit> edits;
                for (const auto& edit : params.at("edits")) {
                    std::size_t start, end;
                    if (edit.contains("range")) {
                        auto range = edit.at("range");
                        start = record.document.offset_at({range.at("start").at("line"), range.at("start").at("character")});
                        end = record.document.offset_at({range.at("end").at("line"), range.at("end").at("character")});
                    } else { start = edit.at("start"); end = edit.at("end"); }
                    edits.push_back({start, end, edit.at("text").get<std::string>()});
                }
                record.document.apply(std::move(edits), revision, params.value("historyGroup", std::string{}));
            } else if (command == "document.undo") record.document.undo(revision);
            else if (command == "document.redo") record.document.redo(revision);
            else if (command == "document.save") {
                auto path = resolve(record.path);
                if (record.existed != std::filesystem::exists(path) || (record.existed && read_file(path) != record.document.saved_text())) throw CommandError("disk-conflict", "The file changed on disk; preserve both versions before saving");
                auto text = record.document.text(); write_file_atomic(path, text); record.document.mark_saved(text); record.existed = true;
                if (language && record.synced) language->notify("textDocument/didSave", {{"textDocument", {{"uri", make_uri(path)}}}});
            } else throw CommandError("unknown-command", "Unknown document command");
            return summary(id, record);
        }
        if (command == "doctor") {
            Json result = {{"version", "0.0.1"}, {"tools", Json::object()}};
            for (auto name : {"mcpp", "mcppls"}) { try { tool(name); result["tools"][name] = {{"available", true}}; } catch (...) { result["tools"][name] = {{"available", false}}; } }
            return result;
        }
        if (command == "task.status" || command == "task.wait" || command == "task.cancel") {
            auto id = params.at("task").get<std::string>(); if (!tasks.contains(id)) throw CommandError("not-found", "Task does not exist");
            auto& task = *tasks.at(id);
            if (command == "task.cancel") task.worker.request_stop();
            if (command == "task.wait") { std::unique_lock lock(task.mutex); if (!task.changed.wait_for(lock, std::chrono::milliseconds(std::clamp(params.value("timeoutMs", 30000), 1, 120000)), [&] { return task.state != "running"; })) throw CommandError("timeout", "Task is still running; query its task ID"); }
            return task_status(id);
        }
        if (command == "build" || command == "run" || command == "test" || command == "project.new" || command == "package.search" || command == "package.add" || command == "package.remove" || command == "toolchain.list" || command == "toolchain.install" || command == "environment.prepare") {
            if (command == "build" || command == "run" || command == "test") for (auto& [id, record] : documents) if (record->document.dirty()) throw CommandError("unsaved-documents", "Save modified documents before building");
            std::vector<std::string> args {tool("mcpp").string()};
            if (command == "project.new") { auto name = params.at("name").get<std::string>(); resolve(name); if (name.empty() || name.starts_with('-')) throw CommandError("invalid-params", "Invalid project name"); args.insert(args.end(), {"new", name}); }
            else if (command.starts_with("package.")) { auto operation = command.substr(8); args.push_back(operation == "search" ? "search" : operation); auto name = params.at("name").get<std::string>(); if (name.empty() || name.starts_with('-')) throw CommandError("invalid-params", "Invalid package name"); args.push_back(name); }
            else if (command == "toolchain.list") args.insert(args.end(), {"toolchain", "list", "--format", "json"});
            else if (command == "toolchain.install") { auto family = params.at("family").get<std::string>(); auto version = params.at("version").get<std::string>(); if ((family != "llvm" && family != "gcc") || version.empty() || version.starts_with('-')) throw CommandError("invalid-params", "Invalid toolchain selection"); args.insert(args.end(), {"toolchain", "install", family, version}); }
            else if (command == "environment.prepare") args.insert(args.end(), {"build", "--configure-only"});
            else {
                args.push_back(command);
                if (params.contains("target")) args.insert(args.end(), {"--target", params.at("target").get<std::string>()});
                if (params.contains("profile")) args.insert(args.end(), {"--profile", params.at("profile").get<std::string>()});
                if (params.contains("features")) args.insert(args.end(), {"--features", params.at("features").get<std::string>()});
            }
            return start_task(std::move(args), std::clamp(params.value("timeoutMs", 120000), 100, 3600000));
        }
        if (command == "language.stop") { language.reset(); for (auto& [id, record] : documents) record->synced = 0; return {{"stopped", true}}; }
        if (command == "language.diagnostics") {
            for (auto& [id, record] : documents) sync(*record);
            return clean(language ? language->diagnostics() : Json::array(), root);
        }
        if (command.starts_with("language.") || command.starts_with("modules.")) {
            auto& record = this->record(params); sync(record);
            if (command.starts_with("modules.")) {
                if (!language->modules_supported()) throw CommandError("capability-unavailable", "Server does not advertise Modules extensions");
                if (command == "modules.graph") return clean(language->request("cxxModules/graph", Json::object()), root);
                if (command == "modules.info") return clean(language->request("cxxModules/moduleInfo", {{"name", params.at("name")}}), root);
                if (command == "modules.contexts") return clean(language->request("cxxModules/contexts", {{"textDocument", {{"uri", make_uri(resolve(record.path))}}}}), root);
                throw CommandError("unknown-command", "Unknown Modules command");
            }
            const std::map<std::string, std::string> methods {{"language.completion", "textDocument/completion"}, {"language.hover", "textDocument/hover"}, {"language.definition", "textDocument/definition"}, {"language.references", "textDocument/references"}, {"language.symbols", "textDocument/documentSymbol"}, {"language.signature", "textDocument/signatureHelp"}};
            if (!methods.contains(command)) throw CommandError("unknown-command", "Unknown language command");
            Json request = {{"textDocument", {{"uri", make_uri(resolve(record.path))}}}};
            if (command != "language.symbols") request["position"] = params.at("position");
            if (command == "language.references") request["context"] = {{"includeDeclaration", true}};
            return clean(language->request(methods.at(command), request), root);
        }
        throw CommandError("unknown-command", "Command is not implemented; query capabilities");
    }
};
Application::Application(std::filesystem::path workspace, std::filesystem::path toolRoot, std::filesystem::path settingsFile) : impl_(std::make_unique<Impl>()) {
    impl_->root = std::filesystem::weakly_canonical(workspace);
    if (!std::filesystem::is_directory(impl_->root)) throw std::runtime_error("Workspace directory does not exist");
    impl_->tools = toolRoot.empty() ? executable_directory() / "tools" : std::filesystem::absolute(toolRoot);
    impl_->settings = std::make_unique<SettingsStore>(std::move(settingsFile));
}
Application::~Application() = default;
std::string Application::session_id() const { return impl_->session; }
Json Application::capabilities() const {
    Json commands = Json::array();
    for (auto name : {"capabilities", "workspace.status", "workspace.files", "workspace.trust", "document.open", "document.read", "document.edit", "document.undo", "document.redo", "document.save", "doctor", "build", "run", "test", "task.status", "task.wait", "task.cancel", "project.new", "package.search", "package.add", "package.remove", "toolchain.list", "toolchain.install", "environment.prepare", "language.completion", "language.hover", "language.definition", "language.references", "language.symbols", "language.signature", "language.diagnostics", "language.stop", "modules.graph", "modules.info", "modules.contexts"}) commands.push_back(name);
    commands.push_back("settings.get"); commands.push_back("settings.update");
    Json schemas = Json::object();
    for (const auto& name : commands) {
        const auto command = name.get<std::string>();
        Json properties = Json::object(); Json required = Json::array();
        auto field = [&](std::string key, std::string type, bool needed = false) { properties[key] = {{"type", type}}; if (needed) required.push_back(key); };
        if (command == "settings.update") { field("revision", "integer", true); field("changes", "object", true); }
        else if (command == "document.open") { field("path", "string", true); field("create", "boolean"); }
        else if (command.starts_with("document.") || command.starts_with("language.") || command.starts_with("modules.")) {
            if (command != "language.stop" && command != "language.diagnostics") field("document", "string", true);
            if (command == "document.edit" || command == "document.undo" || command == "document.redo" || command == "document.save") field("revision", "integer", true);
            if (command == "document.edit") { field("edits", "array", true); field("historyGroup", "string"); }
            if (command == "modules.info") field("name", "string", true);
            if (command == "language.completion" || command == "language.hover" || command == "language.definition" || command == "language.references" || command == "language.signature") field("position", "object", true);
        } else if (command.starts_with("task.")) { field("task", "string", true); field("timeoutMs", "integer"); }
        else if (command == "workspace.trust") field("allow", "boolean", true);
        else if (command == "project.new" || command.starts_with("package.")) field("name", "string", true);
        else if (command == "toolchain.install") { field("family", "string", true); field("version", "string", true); }
        if (command == "build" || command == "run" || command == "test") { field("target", "string"); field("profile", "string"); field("features", "string"); }
        if (command == "build" || command == "run" || command == "test" || command == "project.new" || command.starts_with("package.") || command.starts_with("toolchain.") || command == "environment.prepare") field("timeoutMs", "integer");
        schemas[command] = {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
    }
    return {{"version", "0.0.1"}, {"protocolVersion", 1}, {"positionEncoding", "utf-16"}, {"editOffsets", "utf-8-bytes"}, {"sessionTransport", "stdio-jsonl"}, {"commands", commands}, {"commandSchemas", schemas}};
}
Json Application::execute(const Json& request) {
    Json response = {{"schemaVersion", 1}, {"kind", "meditor.result"}, {"kindVersion", 1}, {"session", session_id()}, {"requestId", request.is_object() ? request.value("id", Json(nullptr)) : Json(nullptr)}, {"effects", Json::array()}};
    try {
        if (!request.is_object()) throw CommandError("invalid-params", "Request must be a JSON object");
        auto command = request.at("command").get<std::string>(); auto params = request.value("params", Json::object());
        if (!params.is_object()) throw CommandError("invalid-params", "Parameters must be an object");
        auto schemas = capabilities().at("commandSchemas");
        if (schemas.contains(command)) {
            const auto& schema = schemas.at(command);
            for (const auto& key : schema.at("required"))
                if (!params.contains(key.get<std::string>())) throw CommandError("invalid-params", "Required parameter is missing");
            for (const auto& item : params.items()) {
                const auto& properties = schema.at("properties");
                if (!properties.contains(item.key())) throw CommandError("invalid-params", "Unknown parameter");
                const auto type = properties.at(item.key()).at("type").get<std::string>();
                const auto& value = item.value();
                bool valid = type == "string" ? value.is_string() : type == "integer" ? value.is_number_integer() : type == "boolean" ? value.is_boolean() : type == "array" ? value.is_array() : type == "object" && value.is_object();
                if (!valid) throw CommandError("invalid-params", "Parameter has the wrong type");
            }
        }
        response["data"] = command == "capabilities" ? capabilities() : impl_->dispatch(command, params);
        response["ok"] = true;
        if (command == "document.edit" || command == "document.undo" || command == "document.redo") response["effects"].push_back("document-change");
        if (command == "document.save") response["effects"].push_back("file-write");
        if (command == "settings.update") response["effects"].push_back("settings-change");
        if (response["data"].is_object() && response["data"].value("state", "") == "running") response["effects"].push_back("tool-execution");
    } catch (const CommandError& error) { response["ok"] = false; response["error"] = {{"code", error.code}, {"message", redact(error.what(), impl_->root)}}; }
    catch (const SettingsError& error) { response["ok"] = false; response["error"] = {{"code", error.code}, {"message", error.what()}}; }
    catch (const nlohmann::json::exception&) { response["ok"] = false; response["error"] = {{"code", "invalid-params"}, {"message", "Missing or invalid command parameters"}}; }
    catch (const std::exception& error) { response["ok"] = false; response["error"] = {{"code", "operation-failed"}, {"message", redact(error.what(), impl_->root)}}; }
    return response;
}
}

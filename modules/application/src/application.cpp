// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.application;
import std;
import meditor.recovery;
import nlohmann.json;
import meditor.document;
import meditor.syntax;
import meditor.platform;
import meditor.language.locations;
import meditor.settings;
import meditor.about;

namespace meditor {
namespace {
using Clock = std::chrono::steady_clock;
struct CommandError : std::runtime_error {
    std::string code;
    CommandError(std::string kind, std::string message) : std::runtime_error(std::move(message)), code(std::move(kind)) {}
};
std::string make_uri(const std::filesystem::path& path) {
    auto source = path_to_utf8(std::filesystem::absolute(path));
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
    static constexpr std::size_t MAX_MESSAGE_BYTES {16 * 1024 * 1024};
    static constexpr std::size_t MAX_QUEUED_BYTES {64 * 1024 * 1024};
    static constexpr std::size_t MAX_QUEUED_MESSAGES {256};
    Process process_;
    std::mutex mutex_;
    std::mutex writerMutex_;
    std::condition_variable_any ready_;
    std::condition_variable_any outboundReady_;
    std::deque<std::string> outbound_;
    std::size_t pendingBytes_ {};
    bool writing_ {};
    std::set<std::int64_t> pendingRequests_;
    std::map<std::int64_t, Json> replies_;
    std::map<std::string, Json> diagnostics_;
    Json status_;
    std::string failure_;
    Json serverCapabilities_;
    std::atomic<bool> closed_ {false};
    std::atomic<std::int64_t> next_ {1};
    std::jthread reader_, writer_;
    std::mutex documentMutex_;
    struct OpenDocument { std::uint64_t revision {}; int version {}; };
    std::map<std::string, OpenDocument> openDocuments_;
    int sync_document_(const std::string& uri, std::uint64_t revision, const std::string& text, int proposed) {
        if (closed_) throw CommandError("tool-unavailable", "Language service is unavailable");
        auto found = openDocuments_.find(uri);
        if (found != openDocuments_.end() && found->second.revision > revision) throw CommandError("revision-conflict", "A newer document snapshot reached the language service");
        if (found != openDocuments_.end() && found->second.revision == revision) return found->second.version;
        const auto version = found == openDocuments_.end() ? proposed : std::max(proposed, found->second.version + 1);
        if (found == openDocuments_.end()) notify("textDocument/didOpen", {{"textDocument", {{"uri", uri}, {"languageId", "cpp"}, {"version", version}, {"text", text}}}});
        else notify("textDocument/didChange", {{"textDocument", {{"uri", uri}, {"version", version}}}, {"contentChanges", Json::array({{{"text", text}}})}});
        openDocuments_[uri] = {revision, version}; return version;
    }
    void fail_(std::string message) {
        { std::lock_guard lock(mutex_); if (failure_.empty()) failure_ = std::move(message); }
        closed_ = true; ready_.notify_all(); outboundReady_.notify_all();
    }
    void send_(Json message) {
        if (closed_) throw CommandError("tool-unavailable", "Language server is unavailable");
        auto body = message.dump();
        if (body.size() > MAX_MESSAGE_BYTES) throw CommandError("language-message-too-large", "Language message exceeds the transport limit");
        auto frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::lock_guard lock(writerMutex_);
        if (closed_) throw CommandError("tool-unavailable", "Language server is unavailable");
        if (outbound_.size() + (writing_ ? 1 : 0) >= MAX_QUEUED_MESSAGES || frame.size() > MAX_QUEUED_BYTES - pendingBytes_)
            throw CommandError("language-busy", "Language service send queue is full");
        outbound_.push_back(std::move(frame)); pendingBytes_ += outbound_.back().size(); outboundReady_.notify_one();
    }
    void transmit_(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::string frame;
            {
                std::unique_lock lock(writerMutex_);
                if (!outboundReady_.wait(lock, stop, [&] { return !outbound_.empty() || closed_; }) || closed_) break;
                frame = std::move(outbound_.front()); outbound_.pop_front(); writing_ = true;
            }
            try { process_.write(frame, std::chrono::seconds(5), stop); }
            catch (const std::exception&) { if (!stop.stop_requested()) fail_("Language server input is unavailable"); break; }
            { std::lock_guard lock(writerMutex_); pendingBytes_ -= frame.size(); writing_ = false; }
        }
        std::lock_guard lock(writerMutex_); outbound_.clear(); pendingBytes_ = 0; writing_ = false;
    }
    void shutdown_() {
        closed_ = true; writer_.request_stop(); reader_.request_stop(); outboundReady_.notify_all(); ready_.notify_all();
        if (writer_.joinable()) writer_.join();
        if (reader_.joinable()) reader_.join();
        process_.terminate();
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
                    if (length > MAX_MESSAGE_BYTES) throw std::runtime_error("Language message exceeds limit");
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
                        std::lock_guard lock(mutex_); if (pendingRequests_.contains(id)) { replies_[id] = std::move(message); ready_.notify_all(); }
                    } else if (message.value("method", "") == "textDocument/publishDiagnostics") {
                        auto params = message.at("params"); auto uri = params.value("uri", "");
                        std::lock_guard lock(mutex_); diagnostics_[uri] = std::move(params); ready_.notify_all();
                    } else if (message.value("method", "") == "cxxModules/status") {
                        std::lock_guard lock(mutex_); status_ = message.at("params");
                    }
                }
                if (read.ended) break;
            }
        } catch (const std::exception&) { if (!stop.stop_requested()) fail_("Language server output is unavailable"); }
        closed_ = true; ready_.notify_all(); outboundReady_.notify_all();
    }
public:
    Language(const std::filesystem::path& tool, const std::filesystem::path& driver, const std::filesystem::path& root, const ProcessEnvironment& environment, std::stop_token stop = {}) {
        process_.start({path_to_utf8(tool), "serve"}, root, false, environment);
        writer_ = std::jthread([this](std::stop_token stop) { transmit_(stop); });
        reader_ = std::jthread([this](std::stop_token stop) { receive_(stop); });
        try {
            auto init = request("initialize", {{"processId", nullptr}, {"rootUri", make_uri(root)}, {"clientInfo", {{"name", "meditor"}, {"version", "0.0.1"}}},
                {"workspaceFolders", Json::array({{{"uri", make_uri(root)}, {"name", "workspace"}}})},
                {"initializationOptions", {{"mcpp", path_to_utf8(driver)}, {"toolEnvironment", "editor"}}},
                {"capabilities", {{"general", {{"positionEncodings", Json::array({"utf-16"})}}}, {"workspace", {{"configuration", true}}},
                    {"textDocument", {{"synchronization", {{"didSave", true}}}, {"documentSymbol", {{"hierarchicalDocumentSymbolSupport", true}}}, {"completion", {{"completionItem", {{"snippetSupport", false}}}}}}},
                    {"experimental", {{"cxxModules", {{"version", 1}, {"status", true}, {"graph", true}, {"contexts", true}}}}}}}}, 30000, stop);
            serverCapabilities_ = init.value("capabilities", Json::object());
            notify("initialized", Json::object());
        } catch (...) { shutdown_(); throw; }
    }
    ~Language() { shutdown_(); }
    Json request(std::string method, Json params, int timeout = 15000, std::stop_token stop = {}) {
        const auto id = next_++;
        { std::lock_guard lock(mutex_); pendingRequests_.insert(id); }
        try {
            send_({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
            Json response;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait_for(lock, stop, std::chrono::milliseconds(timeout), [&] { return replies_.contains(id) || closed_; })) {
                    pendingRequests_.erase(id); lock.unlock();
                    try { notify("$/cancelRequest", {{"id", id}}); } catch (const std::exception&) {}
                    throw CommandError(stop.stop_requested() ? "cancelled" : "timeout", stop.stop_requested() ? "Language request cancelled" : "Language request timed out");
                }
                pendingRequests_.erase(id);
                if (!replies_.contains(id)) throw CommandError("tool-unavailable", "Language server stopped");
                response = std::move(replies_.at(id)); replies_.erase(id);
            }
            if (response.contains("error")) throw CommandError("language-error", response["error"].value("message", "Language request failed"));
            return response.value("result", Json(nullptr));
        } catch (...) { std::lock_guard lock(mutex_); pendingRequests_.erase(id); replies_.erase(id); throw; }
    }
    void notify(std::string method, Json params) { send_({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}); }
    Json diagnostics_for(const std::string& uri, int version, std::chrono::steady_clock::time_point deadline, std::stop_token stop) {
        std::unique_lock lock(mutex_);
        const auto ready = [&] { return closed_ || (diagnostics_.contains(uri) && (!diagnostics_[uri].contains("version") || !diagnostics_[uri]["version"].is_number_integer() || diagnostics_[uri]["version"].get<std::int64_t>() >= version)); };
        if (!ready_.wait_until(lock, stop, deadline, ready)) throw CommandError(stop.stop_requested() ? "cancelled" : "timeout", "Diagnostics wait ended");
        if (closed_) throw CommandError("tool-unavailable", "Language service stopped");
        const auto& value = diagnostics_.at(uri);
        return {{"diagnostics", Json::array({value})}, {"versionVerified", value.contains("version") && value["version"] == version}, {"lspVersion", version}};
    }
    Json diagnostics() { std::lock_guard lock(mutex_); Json values = Json::array(); for (auto& [uri, item] : diagnostics_) values.push_back(item); return values; }
    int sync_document(const std::string& uri, std::uint64_t revision, const std::string& text, int proposed, std::stop_token stop = {}) {
        std::lock_guard lock(documentMutex_);
        if (stop.stop_requested()) throw CommandError("cancelled", "Language synchronization cancelled");
        return sync_document_(uri, revision, text, proposed);
    }
    int save_document(const std::string& uri, std::uint64_t revision, const std::string& text, int proposed) {
        std::unique_lock lock(documentMutex_, std::try_to_lock);
        if (!lock.owns_lock()) throw CommandError("language-busy", "Language document synchronization is busy");
        if (!openDocuments_.contains(uri)) return 0;
        const auto version = sync_document_(uri, revision, text, proposed);
        notify("textDocument/didSave", {{"textDocument", {{"uri", uri}}}}); return version;
    }
    static Json limits() { return {{"notificationDelivery", "queued"}, {"maxMessageBytes", MAX_MESSAGE_BYTES}, {"maxQueuedBytes", MAX_QUEUED_BYTES}, {"maxQueuedMessages", MAX_QUEUED_MESSAGES}, {"writeTimeoutMs", 5000}}; }
    bool available() const { return !closed_; }
    Json status() {
        Json result;
        { std::lock_guard lock(mutex_); result = {{"state", closed_ ? "stopped" : "running"}, {"backend", status_}, {"error", failure_.empty() ? Json(nullptr) : Json(failure_)}}; }
        { std::lock_guard lock(writerMutex_); result["queuedMessages"] = outbound_.size() + (writing_ ? 1 : 0); result["queuedBytes"] = pendingBytes_; }
        return result;
    }
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
using QueryClock = std::chrono::steady_clock;
struct LanguageStartup {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::shared_ptr<Language> client;
    Json error;
    bool finished {};
    std::jthread worker;
    ~LanguageStartup() { worker.request_stop(); if (worker.joinable()) worker.join(); }
    std::shared_ptr<Language> get(QueryClock::time_point deadline, std::stop_token stop = {}) {
        std::unique_lock lock(mutex);
        if (!changed.wait_until(lock, stop, deadline, [&] { return finished; })) throw CommandError(stop.stop_requested() ? "cancelled" : "timeout", "Language startup wait ended");
        if (!client) throw CommandError(error.value("code", "tool-unavailable"), "Language service could not start");
        return client;
    }
};
struct LanguageSnapshot { std::string path, uri, text; std::uint64_t revision {}; int version {}; };
struct QueryBudget { std::atomic<std::size_t> outputBytes {}; };
struct LanguageQuery {
    std::mutex mutex;
    std::string state {"running"}, document, operation;
    std::uint64_t revision {}, generation {}, sequence {};
    std::map<std::string, std::uint64_t> context;
    Json result, error;
    language::NavigationSet navigation;
    std::size_t inputBytes {}, outputBytes {};
    std::shared_ptr<QueryBudget> budget;
    std::jthread worker;
    ~LanguageQuery() { worker.request_stop(); if (worker.joinable()) worker.join(); if (outputBytes) budget->outputBytes.fetch_sub(outputBytes); }
};
struct Record {
    std::string path;
    Document document;
    TextFormat format;
    void refresh_format() {
        auto next = inspect_text_format(document.text());
        if (!next.lineBreaks) next.lineEnding = format.lineEnding;
        format = next;
    }
    bool existed {};
    std::uint64_t synced {};
    int lspVersion {};
    std::string recoveryBase;
    Record(std::string name, std::string text, bool exists) : path(std::move(name)), document(std::move(text)), format(inspect_text_format(document.text())), existed(exists) {}
};
}
struct Application::Impl {
    static constexpr std::size_t MAX_ACTIVE_QUERIES {8}, MAX_RETAINED_QUERIES {16};
    std::recursive_mutex executeMutex;
    static constexpr std::size_t MAX_QUERY_SNAPSHOT_BYTES {64 * 1024 * 1024}, MAX_QUERY_RESULT_BYTES {64 * 1024 * 1024};
    std::filesystem::path root;
    std::filesystem::path tools;
    std::string session {"s" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())};
    std::map<std::string, std::unique_ptr<Record>> documents;
    std::map<std::string, std::unique_ptr<Task>> tasks;
    std::shared_ptr<Language> language;
    std::shared_ptr<LanguageStartup> startup;
    std::shared_ptr<QueryBudget> queryBudget {std::make_shared<QueryBudget>()};
    std::map<std::string, std::unique_ptr<LanguageQuery>> queries;
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<RecoveryStore> recovery;
    std::vector<std::string> recoveredDocuments, recoveryConflicts;
    RuntimeDirectories runtime;
    bool trusted {};
    std::uint64_t workspaceGeneration {1};
    std::uint64_t nextDocument {1}, nextTask {1}, nextQuery {1};
    std::filesystem::path resolve(std::string value) const {
        auto path = std::filesystem::u8path(value);
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
        throw CommandError("tool-unavailable", "The bundled " + name + " tool is unavailable; use meditor --cli doctor");
    }
    Record& record(const Json& params) {
        auto id = params.at("document").get<std::string>();
        if (!documents.contains(id)) throw CommandError("not-found", "Document is not open in this session");
        return *documents.at(id);
    }
    Json summary(const std::string& id, const Record& record, bool includeText = false) const {
        Json result = {{"document", id}, {"path", record.path}, {"revision", record.document.revision()}, {"dirty", record.document.dirty()}, {"bytes", record.document.size()}};
        result["format"] = {{"encoding", "utf-8"}, {"bom", record.format.utf8Bom}, {"lineEnding", record.format.lineEnding == LineEnding::CrLf ? "crlf" : "lf"}, {"mixedLineEndings", record.format.mixedLineEndings}, {"loneCarriageReturns", record.format.loneCarriageReturns}, {"lineBreaks", record.format.lineBreaks}};
        if (includeText) result["text"] = record.document.text();
        return result;
    }
    Json open(const Json& params) {
        auto path = resolve(params.at("path").get<std::string>()); auto name = path_to_utf8(path.lexically_relative(root));
        for (const auto& [id, record] : documents) if (record->path == name) return summary(id, *record);
        bool exists = std::filesystem::exists(path);
        if (!exists && !params.value("create", false)) throw CommandError("not-found", "File does not exist");
        auto id = "d" + std::to_string(nextDocument++);
        auto record = std::make_unique<Record>(name, exists ? read_file(path, MAX_DOCUMENT_BYTES) : std::string(), exists);
        record->recoveryBase = recovery_digest(record->document.text());
        auto result = summary(id, *record); documents.emplace(id, std::move(record)); return result;
    }
    void require_trust() const { if (!trusted) throw CommandError("workspace-untrusted", "Explicitly trust this workspace before executing tools"); }
    void sync(Record& record) {
        require_trust();
        if (language && !language->available()) {
            language.reset(); startup.reset(); for (auto& [id, open] : documents) open->synced = 0;
        }
        if (!language && startup) { try { language = startup->get(QueryClock::now() + std::chrono::seconds(30)); } catch (...) { startup.reset(); throw; } }
        if (!language) {
            runtime.prepare_tools();
            language = std::make_shared<Language>(tool("mcppls"), tool("mcpp"), root, runtime.tool_environment());
        }
        record.lspVersion = language->sync_document(make_uri(resolve(record.path)), record.document.revision(), record.document.text(), record.lspVersion + 1);
        record.synced = record.document.revision();
    }
    void stop_language() {
        for (auto& [id, query] : queries) query->worker.request_stop();
        queries.clear(); startup.reset(); language.reset();
    }
    void adopt_language() {
        if (startup && !language) { std::lock_guard lock(startup->mutex); if (startup->finished && startup->client) language = startup->client; }
    }
    std::shared_ptr<LanguageStartup> start_language() {
        adopt_language();
        if (language && language->available()) {
            auto ready = std::make_shared<LanguageStartup>(); ready->client = language; ready->finished = true; return ready;
        }
        if (startup) { std::lock_guard lock(startup->mutex); if (!startup->finished) return startup; }
        language.reset(); startup.reset();
        runtime.prepare_tools();
        const auto server = tool("mcppls"), driver = tool("mcpp"), directory = root;
        auto environment = runtime.tool_environment(); auto next = std::make_shared<LanguageStartup>(); auto raw = next.get();
        next->worker = std::jthread([raw, server, driver, directory, environment = std::move(environment)](std::stop_token stop) {
            std::shared_ptr<Language> client; Json error;
            try { client = std::make_shared<Language>(server, driver, directory, environment, stop); }
            catch (const CommandError& failed) { error = {{"code", failed.code}}; }
            catch (const std::exception&) { error = {{"code", "tool-unavailable"}}; }
            { std::lock_guard lock(raw->mutex); raw->client = std::move(client); raw->error = std::move(error); raw->finished = true; }
            raw->changed.notify_all();
        });
        startup = next; return next;
    }
    Json language_state() {
        if (language) return clean(language->status(), root);
        if (!startup) return nullptr;
        std::lock_guard lock(startup->mutex);
        return {{"state", startup->finished ? "failed" : "starting"}, {"error", startup->error}};
    }
    bool query_stale(const LanguageQuery& query) const {
        bool stale = query.generation != workspaceGeneration || query.context.size() != documents.size();
        for (const auto& [document, revision] : query.context) if (!documents.contains(document) || documents.at(document)->document.revision() != revision) stale = true;
        return stale;
    }
    Json query_status(const std::string& id, bool includeResult = true) {
        if (!queries.contains(id)) throw CommandError("not-found", "Language query does not exist");
        auto& query = *queries.at(id); std::lock_guard lock(query.mutex);
        const bool stale = query_stale(query);
        Json result {{"query", id}, {"state", query.state}, {"operation", query.operation}, {"document", query.document}, {"revision", query.revision}, {"generation", query.generation}, {"stale", stale},
            {"cancellationRequested", query.worker.get_stop_token().stop_requested()}, {"error", query.error}};
        if (includeResult) { result["result"] = query.result; result["locations"] = query.navigation.locations; result["locationsTruncated"] = query.navigation.truncated; }
        return result;
    }
    Json navigate(const Json& params) {
        const auto id = params.at("query").get<std::string>();
        if (!queries.contains(id)) throw CommandError("not-found", "Language query does not exist");
        auto& query = *queries.at(id); std::lock_guard lock(query.mutex);
        if (query.state != "succeeded" || query_stale(query)) throw CommandError("revision-conflict", "Language locations are out of date");
        const auto index = params.at("location").get<std::int64_t>();
        if (index < 0 || static_cast<std::uint64_t>(index) >= query.navigation.locations.size()) throw CommandError("invalid-params", "Unknown language location");
        const auto& location = query.navigation.locations.at(static_cast<std::size_t>(index));
        if (!location.value("available", false)) throw CommandError("capability-unavailable", "This language location is unavailable");
        const auto path = location.at("path").get<std::string>();
        const auto resolved = resolve(path);
        if (path_to_utf8(resolved.lexically_relative(root)) != path) throw CommandError("revision-conflict", "Location target changed");
        const auto& captured = query.navigation.texts.at(path);
        for (const auto& [document, record] : documents) if (record->path == path) {
            if (record->document.text() != captured) throw CommandError("revision-conflict", "Location target changed");
            return {{"target", summary(document, *record)}, {"range", location.at("range")}, {"opened", false}};
        }
        if (!std::filesystem::is_regular_file(resolved) || read_file(resolved, MAX_DOCUMENT_BYTES) != captured) throw CommandError("revision-conflict", "Location target changed on disk");
        const auto document = "d" + std::to_string(nextDocument);
        auto target = std::make_unique<Record>(path, captured, true);
        auto context = query.context; context[document] = target->document.revision();
        Json result {{"target", summary(document, *target)}, {"range", location.at("range")}, {"opened", true}};
        documents.emplace(document, std::move(target)); query.context.swap(context); ++nextDocument;
        return result;
    }
    static const std::map<std::string, std::string>& query_methods() {
        static const std::map<std::string, std::string> methods {
            {"completion", "textDocument/completion"}, {"hover", "textDocument/hover"}, {"definition", "textDocument/definition"},
            {"references", "textDocument/references"}, {"symbols", "textDocument/documentSymbol"}, {"signature", "textDocument/signatureHelp"},
            {"semanticTokens", "textDocument/semanticTokens/range"},
            {"diagnostics", ""}, {"modules.graph", "cxxModules/graph"}, {"modules.info", "cxxModules/moduleInfo"}, {"modules.contexts", "cxxModules/contexts"}};
        return methods;
    }
    Json submit_query(const Json& params) {
        const auto& methods = query_methods();
        const auto operation = params.at("operation").get<std::string>();
        const bool navigationOperation = operation == "definition" || operation == "references";
        if (!methods.contains(operation)) throw CommandError("invalid-params", "Unknown asynchronous language operation");
        auto& target = record(params); const auto document = params.at("document").get<std::string>();
        const auto revision = params.at("revision").get<std::uint64_t>();
        if (target.document.revision() != revision) throw CommandError("revision-conflict", "Document changed before the language query");
        if (syntax::detect(target.path) != syntax::Language::Cpp) throw CommandError("capability-unavailable", "Language queries require a C++ document");
        const auto timeout = params.value("timeoutMs", 15000);
        if (timeout < 100 || timeout > 120000) throw CommandError("invalid-params", "Language timeout must be between 100 and 120000 milliseconds");
        const bool positionRequired = operation == "completion" || operation == "hover" || operation == "definition" || operation == "references" || operation == "signature";
        if (positionRequired) {
            const auto& position = params.at("position");
            if (!position.is_object() || position.size() != 2 || !position.contains("line") || !position.contains("character") || !position["line"].is_number_integer() || !position["character"].is_number_integer()) throw CommandError("invalid-params", "A UTF-16 position is required");
            try { target.document.offset_at({position["line"].get<std::size_t>(), position["character"].get<std::size_t>()}); }
            catch (const std::exception&) { throw CommandError("invalid-params", "Invalid UTF-16 position"); }
        } else if (params.contains("position")) throw CommandError("invalid-params", "This language operation has no position");
        if (operation == "semanticTokens") {
            const auto& range = params.at("range");
            const auto valid = range.is_object() && range.size() == 2 && range.contains("startLine") && range.contains("endLine") && range["startLine"].is_number_integer() && range["endLine"].is_number_integer() && range["endLine"].get<std::int64_t>() >= range["startLine"].get<std::int64_t>();
            if (!valid) throw CommandError("invalid-params", "A startLine/endLine range is required");
        } else if (params.contains("range")) throw CommandError("invalid-params", "This language operation has no range");
        if (operation == "modules.info") { if (params.at("name").get<std::string>().empty()) throw CommandError("invalid-params", "A module name is required"); }
        else if (params.contains("name")) throw CommandError("invalid-params", "This language operation has no module name");
        require_trust();
        std::size_t active {}, pendingBytes {};
        for (const auto& [id, query] : queries) { std::lock_guard lock(query->mutex); if (query->state == "running") { ++active; pendingBytes += query->inputBytes; } }
        if (active >= MAX_ACTIVE_QUERIES) throw CommandError("language-busy", "Too many active language queries");
        std::size_t bytes {};
        for (const auto& [id, item] : documents) if (id == document || navigationOperation || (operation.starts_with("modules.") && syntax::detect(item->path) == syntax::Language::Cpp)) {
            if (item->document.size() > MAX_QUERY_SNAPSHOT_BYTES - bytes) throw CommandError("language-busy", "Language snapshot budget exceeded");
            bytes += item->document.size();
        }
        if (bytes > MAX_QUERY_SNAPSHOT_BYTES - pendingBytes) throw CommandError("language-busy", "Language snapshot budget exceeded");
        if (queries.size() >= MAX_RETAINED_QUERIES) {
            auto completed = queries.end();
            for (auto it = queries.begin(); it != queries.end(); ++it) { std::lock_guard lock(it->second->mutex); if (it->second->state != "running" && (completed == queries.end() || it->second->sequence < completed->second->sequence)) completed = it; }
            if (completed != queries.end()) queries.erase(completed);
        }
        auto query = std::make_unique<LanguageQuery>(); query->document = document; query->revision = revision; query->generation = workspaceGeneration;
        query->sequence = nextQuery; query->operation = operation; query->inputBytes = bytes; query->budget = queryBudget;
        std::vector<LanguageSnapshot> snapshots; const auto uri = make_uri(resolve(target.path));
        for (auto& [id, item] : documents) {
            query->context[id] = item->document.revision();
            if (id == document || navigationOperation || (operation.starts_with("modules.") && syntax::detect(item->path) == syntax::Language::Cpp))
                snapshots.push_back({item->path, make_uri(resolve(item->path)), item->document.text(), item->document.revision(), syntax::detect(item->path) == syntax::Language::Cpp ? ++item->lspVersion : 0});
        }
        auto connection = start_language(); auto raw = query.get(); const auto directory = root; const auto method = methods.at(operation);
        const auto deadline = QueryClock::now() + std::chrono::milliseconds(timeout);
        const auto id = "l" + std::to_string(nextQuery++);
        query->worker = std::jthread([raw, connection, snapshots = std::move(snapshots), method, params, uri, directory, deadline](std::stop_token stop) mutable {
            Json result, error; std::string state = "failed"; language::NavigationSet navigation;
            try {
                auto client = connection->get(deadline, stop); int version {};
                for (const auto& snapshot : snapshots) {
                    if (stop.stop_requested()) throw CommandError("cancelled", "Language query cancelled");
                    if (!snapshot.version) continue;
                    const auto current = client->sync_document(snapshot.uri, snapshot.revision, snapshot.text, snapshot.version, stop);
                    if (snapshot.uri == uri) version = current;
                }
                const auto operation = params.at("operation").get<std::string>();
                if (operation == "diagnostics") result = client->diagnostics_for(uri, version, deadline, stop);
                else {
                    if (operation.starts_with("modules.") && !client->modules_supported()) throw CommandError("capability-unavailable", "Server does not advertise Modules extensions");
                    Json request = {{"textDocument", {{"uri", uri}}}};
                    if (params.contains("position")) request["position"] = params["position"];
                    if (params.contains("range")) request["range"] = {{"start", {{"line", params["range"]["startLine"]}, {"character", 0}}}, {"end", {{"line", params["range"]["endLine"]}, {"character", 0}}}};
                    if (operation == "references") request["context"] = {{"includeDeclaration", true}};
                    if (operation == "modules.graph") request = Json::object();
                    if (operation == "modules.info") request = {{"name", params.at("name")}};
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - QueryClock::now()).count();
                    if (remaining <= 0) throw CommandError("timeout", "Language query timed out");
                    result = client->request(method, request, static_cast<int>(remaining), stop);
                }
                if (stop.stop_requested()) throw CommandError("cancelled", "Language query cancelled");
                if (operation == "definition" || operation == "references") {
                    std::map<std::string, std::string_view> opened;
                    for (const auto& snapshot : snapshots) opened.emplace(snapshot.path, snapshot.text);
                    navigation = language::collect_locations(result, directory, opened, stop);
                    if (QueryClock::now() >= deadline) throw CommandError("timeout", "Navigation capture timed out");
                }
                result = clean(std::move(result), directory);
                const auto size = result.dump().size() + navigation.locations.dump().size() + navigation.textBytes; auto current = raw->budget->outputBytes.load();
                do { if (size > MAX_QUERY_RESULT_BYTES - current) throw CommandError("language-busy", "Retained language results exceed the budget"); }
                while (!raw->budget->outputBytes.compare_exchange_weak(current, current + size));
                raw->outputBytes = size; state = "succeeded";
            } catch (const CommandError& failed) {
                const auto detail = std::string_view(failed.what());
                auto message = detail.size() > 4096 ? std::string("Language service details exceeded the diagnostic limit") : redact(std::string(detail), directory);
                if (message.size() > 1024) { std::size_t end = 1024; while (end && (static_cast<unsigned char>(message[end]) & 0xc0) == 0x80) --end; message.resize(end); message += "..."; }
                error = {{"code", failed.code}, {"message", message}}; state = failed.code == "cancelled" ? "cancelled" : "failed";
            }
            catch (const std::exception&) { error = {{"code", "operation-failed"}, {"message", "Language query failed"}}; }
            connection.reset(); snapshots.clear();
            std::lock_guard lock(raw->mutex);
            if (stop.stop_requested()) {
                state = "cancelled"; error = {{"code", "cancelled"}, {"message", "Language query cancelled"}};
            }
            if (state != "succeeded") { result = nullptr; navigation = {}; if (raw->outputBytes) raw->budget->outputBytes.fetch_sub(raw->outputBytes); raw->outputBytes = 0; }
            raw->result = std::move(result); raw->navigation = std::move(navigation); raw->error = std::move(error); raw->state = std::move(state);
        });
        queries.emplace(id, std::move(query)); return query_status(id);
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
        runtime.prepare_tools();
        auto environment = runtime.tool_environment();
        auto id = "t" + std::to_string(nextTask++); auto task = std::make_unique<Task>(); auto raw = task.get(); auto cwd = root;
        task->worker = std::jthread([raw, cwd, arguments = std::move(arguments), environment = std::move(environment), timeout](std::stop_token stop) {
            int code = 125; std::string state = "failed";
            try {
                Process process; process.start(arguments, cwd, true, environment); process.close_input(); auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
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
    Json workspace_files(const std::filesystem::path& directory) const {
        Json result = Json::array(); std::error_code error;
        auto has_entry = []<class Iterator>(const Iterator& iterator) {
            if constexpr (requires { iterator != std::default_sentinel; }) return iterator != std::default_sentinel;
            else return iterator != Iterator{};
        };
        for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error); has_entry(it) && result.size() < 3000; it.increment(error)) {
            if (error) { error.clear(); continue; }
            auto name = path_to_utf8(it->path().filename());
            if (it->is_directory(error) && (name.starts_with('.') || name == "target" || name == "node_modules" || name == "build")) { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file(error) && !it->is_symlink(error)) result.push_back(path_to_utf8(it->path().lexically_relative(directory)));
        }
        return {{"files", result}, {"truncated", result.size() >= 3000}};
    }
    Json can_leave() const {
        Json unsaved = Json::array(), running = Json::array();
        for (const auto& [id, record] : documents) if (record->document.dirty()) unsaved.push_back(id);
        for (const auto& [id, task] : tasks) { std::lock_guard lock(task->mutex); if (task->state == "running") running.push_back(id); }
        return {{"canLeave", unsaved.empty() && running.empty()}, {"generation", workspaceGeneration}, {"unsavedDocuments", unsaved}, {"runningTasks", running}};
    }
    Json switch_workspace(const Json& params) {
        if (params.at("generation").get<std::uint64_t>() != workspaceGeneration) throw CommandError("workspace-conflict", "Workspace changed; read the current generation before switching");
        auto value = params.at("path").get<std::string>();
        if (value.empty() || value.find('\0') != value.npos) throw CommandError("invalid-path", "Choose a workspace directory");
        const auto candidate = std::filesystem::weakly_canonical(root / std::filesystem::u8path(value));
        if (!std::filesystem::exists(candidate)) throw CommandError("not-found", "Workspace directory does not exist");
        if (!std::filesystem::is_directory(candidate)) throw CommandError("invalid-path", "Workspace path is not a directory");
        if (std::filesystem::equivalent(candidate, root)) return {{"changed", false}, {"generation", workspaceGeneration}, {"name", path_to_utf8(root.filename())}};
        const auto guard = can_leave();
        if (!guard["unsavedDocuments"].empty()) throw CommandError("unsaved-documents", "Save modified documents before switching workspaces");
        if (!guard["runningTasks"].empty()) throw CommandError("task-busy", "Wait for or cancel active tasks before leaving this workspace");
        // Prepare everything that can fail before destroying the old context.
        std::filesystem::directory_iterator readable(candidate);
        auto nextRoot = candidate; const auto name = path_to_utf8(candidate.filename());
        Json result {{"changed", true}, {"generation", workspaceGeneration + 1}, {"name", name}, {"listing", workspace_files(candidate)}};
        stop_language(); tasks.clear(); documents.clear();
        root.swap(nextRoot); trusted = false; ++workspaceGeneration;
        return result;
    }
    Json dispatch(const std::string& command, const Json& params) {
        adopt_language();
        if (command == "language.query") return submit_query(params);
        if (command == "language.navigate") return navigate(params);
        if (command == "language.release") {
            const auto id = params.at("query").get<std::string>();
            if (!queries.contains(id)) throw CommandError("not-found", "Language query does not exist");
            { std::lock_guard lock(queries.at(id)->mutex); if (queries.at(id)->state == "running") throw CommandError("language-busy", "Cancel and finish the query before releasing it"); }
            queries.erase(id); return {{"released", true}};
        }
        if (command == "language.result" || command == "language.cancel") {
            auto id = params.at("query").get<std::string>();
            if (!queries.contains(id)) throw CommandError("not-found", "Language query does not exist");
            if (command == "language.cancel") { std::lock_guard lock(queries.at(id)->mutex); if (queries.at(id)->state == "running") queries.at(id)->worker.request_stop(); }
            return query_status(id, params.value("includeResult", true));
        }
        if (command == "workspace.canLeave") return can_leave();
        if (command == "workspace.switch") return switch_workspace(params);
        if (command == "about") return about::describe(tools);
        if (command == "licenses") return about::licenses(tools);
        if (command == "settings.get") return settings->get();
        if (command == "settings.update") {
            if (!params.at("revision").is_number_unsigned() && params.at("revision").get<std::int64_t>() < 0)
                throw CommandError("invalid-params", "Settings revision must be nonnegative");
            return settings->update(params.at("revision").get<std::uint64_t>(), params.at("changes"));
        }
        if (command == "workspace.status") {
            Json docs = Json::array(); for (const auto& [id, record] : documents) docs.push_back(summary(id, *record));
            Json recovered = Json::array(); for (const auto& path : recoveredDocuments) recovered.push_back(path);
            Json conflicts = Json::array(); for (const auto& path : recoveryConflicts) conflicts.push_back(path);
            return {{"name", path_to_utf8(root.filename())}, {"generation", workspaceGeneration}, {"trusted", trusted}, {"documents", docs}, {"language", language_state()}, {"recovery", {{"recovered", recovered}, {"conflicts", conflicts}}}};
        }
        if (command == "workspace.trust") { trusted = params.at("allow").get<bool>(); if (!trusted) { for (auto& [id, task] : tasks) task->worker.request_stop(); stop_language(); for (auto& [id, record] : documents) record->synced = 0; } return {{"trusted", trusted}}; }
        if (command == "workspace.files") return workspace_files(root);
        if (command == "document.open") return open(params);
        if (command.starts_with("document.")) {
            auto& record = this->record(params); auto id = params.at("document").get<std::string>();
            if (command == "document.read") return summary(id, record, true);
            if (command == "document.status") return summary(id, record);
            if (command == "document.highlight") {
                const auto start = params.value("startLine", 0LL), count = params.value("maxLines", 100LL);
                if (start < 0 || count < 1 || count > 1000) throw CommandError("invalid-params", "Invalid highlight line range");
                const auto text = record.document.text(); std::vector<std::string_view> lines;
                for (std::size_t byte = 0;;) {
                    auto end = text.find('\n', byte); auto length = end == text.npos ? text.size() - byte : end - byte;
                    auto line = std::string_view(text).substr(byte, length); if (line.ends_with('\r')) line.remove_suffix(1);
                    if (!byte && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
                    lines.push_back(line); if (end == text.npos) break; byte = end + 1;
                }
                const auto language = syntax::detect(record.path); syntax::Cache cache; cache.update(lines, language);
                Json tokens = Json::array(); bool truncated {}; std::size_t processed {};
                auto first = std::min(static_cast<std::uint64_t>(start), static_cast<std::uint64_t>(lines.size()));
                for (auto row = first; row < lines.size() && row - first < static_cast<std::uint64_t>(count); ++row) {
                    for (auto token : cache.tokens(row)) {
                        if (tokens.size() == 4096) { truncated = true; break; }
                        const auto bomOffset = !row && text.starts_with("\xef\xbb\xbf") ? 3U : 0U;
                        tokens.push_back({{"line", row}, {"start", token.start + bomOffset}, {"end", token.end + bomOffset}, {"role", syntax::role_name(token.role)}});
                    }
                    if (truncated) break;
                    ++processed;
                }
                return {{"document", id}, {"revision", record.document.revision()}, {"language", syntax::language_name(language)}, {"layer", "lexical"}, {"offsetEncoding", "utf-8-bytes-per-line"}, {"limited", cache.limited()}, {"truncated", truncated}, {"startLine", first}, {"completeLines", processed}, {"totalLines", lines.size()}, {"tokens", tokens}};
            }
            if (command == "document.find" || command == "document.replace") {
                const auto revision = params.at("revision").get<std::uint64_t>();
                if (revision != record.document.revision()) throw CommandError("revision-conflict", "Document changed; read the current revision before searching");
                const auto query = params.at("query").get<std::string>();
                const auto space = params.value("coordinateSpace", std::string("raw"));
                const auto direction = params.value("direction", std::string("forward"));
                if (query.empty() || query.size() > 4096 || !valid_utf8(query) || (space != "raw" && space != "display") || (direction != "forward" && direction != "backward"))
                    throw CommandError("invalid-params", "Invalid literal search parameters");
                const auto raw = record.document.text(); TextPresentation display(raw);
                const auto& text = space == "display" ? display.text() : raw;
                const auto start = params.value("start", static_cast<std::int64_t>(direction == "backward" ? text.size() : 0));
                const auto limit = params.value("limit", 1000LL);
                if (start < 0 || static_cast<std::uint64_t>(start) > text.size() || limit < 1 || limit > 10000 ||
                    (static_cast<std::size_t>(start) < text.size() && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80))
                    throw CommandError("invalid-params", "Invalid search range or result limit");
                if (command == "document.find") {
                    const auto result = find_text(text, query, start, limit, direction == "backward"); Json matches = Json::array();
                    for (const auto& match : result.matches) matches.push_back({{"start", match.start}, {"end", match.end}, {"utf16Start", match.utf16Start}, {"utf16End", match.utf16End}, {"rawStart", space == "display" ? display.raw_offset(match.start) : match.start}, {"rawEnd", space == "display" ? display.raw_offset(match.end) : match.end}});
                    return {{"document", id}, {"revision", revision}, {"coordinateSpace", space}, {"offsetEncoding", "utf-8-bytes"}, {"total", result.total}, {"truncated", result.truncated}, {"matches", matches}};
                }
                const auto replacement = params.at("replacement").get<std::string>();
                if (!valid_utf8(replacement)) throw CommandError("invalid-params", "Replacement must be valid UTF-8");
                std::size_t count {}; std::optional<std::size_t> nextStart;
                try {
                    if (params.value("all", false)) {
                        if (params.contains("start") || params.contains("direction")) throw CommandError("invalid-params", "Replace all searches the entire document");
                        auto replaced = space == "display" ? replace_all_display(display, query, replacement, record.format.lineEnding) : replace_all_text(raw, query, replacement);
                        count = replaced.count;
                        if (count && replaced.text != raw) record.document.apply({{0, raw.size(), std::move(replaced.text)}}, revision);
                    } else {
                        const auto found = find_text(text, query, start, 1, direction == "backward");
                        if (!found.matches.empty()) {
                            const auto& match = found.matches.front(); auto inserted = replacement;
                            if (space == "display") {
                                for (std::size_t i = 0; i < inserted.size(); ++i) if (inserted[i] == '\r' && (i + 1 == inserted.size() || inserted[i + 1] != '\n')) inserted[i] = '\n';
                                inserted = normalize_line_endings(inserted, record.format.lineEnding);
                            }
                            const auto a = space == "display" ? display.raw_offset(match.start) : match.start, b = space == "display" ? display.raw_offset(match.end) : match.end;
                            if (inserted.size() > MAX_DOCUMENT_BYTES || raw.size() - (b - a) > MAX_DOCUMENT_BYTES - inserted.size()) throw std::length_error("Replacement exceeds the document size limit");
                            if (space == "raw") nextStart = a + inserted.size();
                            else {
                                auto logical = replacement;
                                for (std::size_t i = 0; i < logical.size(); ++i) if (logical[i] == '\r' && (i + 1 == logical.size() || logical[i + 1] != '\n')) logical[i] = '\n';
                                logical = normalize_line_endings(logical, LineEnding::Lf);
                                nextStart = match.start + logical.size();
                                if (!match.start && !record.format.utf8Bom && logical.starts_with("\xef\xbb\xbf")) *nextStart -= 3;
                            }
                            count = 1; if (std::string_view(raw).substr(a, b - a) != inserted) record.document.apply({{a, b, std::move(inserted)}}, revision);
                        }
                    }
                } catch (const std::length_error&) { throw CommandError("document-too-large", "Replacement exceeds the document size limit"); }
                record.refresh_format(); auto result = summary(id, record); result["replacements"] = count; if (nextStart) result["nextStart"] = *nextStart; return result;
            }
            auto revision = params.at("revision").get<std::uint64_t>();
            if (record.document.revision() != revision) throw CommandError("revision-conflict", "Document changed; read the current revision before editing");
            Json saveNotification;
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
                try { record.document.apply(std::move(edits), revision, params.value("historyGroup", std::string{})); }
                catch (const std::length_error&) { throw CommandError("document-too-large", "Document exceeds the editing limit"); }
            } else if (command == "document.undo") record.document.undo(revision);
            else if (command == "document.redo") record.document.redo(revision);
            else if (command == "document.save") {
                auto path = resolve(record.path);
                if (record.existed != std::filesystem::exists(path) || (record.existed && read_file(path, MAX_DOCUMENT_BYTES) != record.document.saved_text())) throw CommandError("disk-conflict", "The file changed on disk; preserve both versions before saving");
                auto text = record.document.text(); write_file_atomic(path, text); record.document.mark_saved(text); record.existed = true;
                record.recoveryBase = recovery_digest(text);
                if (recovery) recovery->discard(record.path);
                saveNotification = {{"state", (language || startup) ? "not-open" : "not-running"}};
                if (language) {
                    try {
                        if (!language->available()) throw CommandError("tool-unavailable", "Language server is unavailable");
                        const auto version = language->save_document(make_uri(path), record.document.revision(), text, record.lspVersion + 1);
                        if (version) { record.lspVersion = version; record.synced = record.document.revision(); saveNotification = {{"state", "queued"}}; }
                    }
                    catch (const CommandError& error) { saveNotification = {{"state", "unavailable"}, {"code", error.code}}; }
                    catch (const std::exception&) { saveNotification = {{"state", "unavailable"}, {"code", "tool-unavailable"}}; }
                }
            } else throw CommandError("unknown-command", "Unknown document command");
            record.refresh_format();
            if (recovery) {
                if (record.document.dirty()) recovery->remember(record.path, record.document.text(), record.document.revision(), record.recoveryBase);
                else recovery->discard(record.path);
            }
            auto result = summary(id, record); if (!saveNotification.is_null()) result["languageSaveNotification"] = saveNotification; return result;
        }
        if (command == "doctor") {
            const auto toolHome = path_to_utf8(runtime.toolRoot);
            Json result = {{"version", "0.0.1"}, {"tools", Json::object()}, {"runtime", {{"mode", runtime.portable ? "portable" : "installed"}, {"managedToolHomes", true}, {"toolHomeContainsSpaces", toolHome.find(' ') != std::string::npos}}}};
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
            std::vector<std::string> args {path_to_utf8(tool("mcpp"))};
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
        if (command == "language.stop") { stop_language(); for (auto& [id, record] : documents) record->synced = 0; return {{"stopped", true}}; }
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
Application::Application(std::filesystem::path workspace, std::filesystem::path toolRoot, std::filesystem::path settingsFile, const RuntimeOptions& runtime) : impl_(std::make_unique<Impl>()) {
    impl_->root = std::filesystem::weakly_canonical(workspace);
    if (!std::filesystem::is_directory(impl_->root)) throw std::runtime_error("Workspace directory does not exist");
    impl_->runtime = RuntimeDirectories::resolve(runtime);
    const auto binary = runtime.binaryDirectory.empty() ? executable_directory() : runtime.binaryDirectory;
    impl_->tools = toolRoot.empty() ? binary / "tools" : std::filesystem::absolute(toolRoot);
    if (settingsFile.empty()) settingsFile = impl_->runtime.settings_file();
    else if (impl_->runtime.portable) {
        const auto relative = std::filesystem::weakly_canonical(std::filesystem::absolute(settingsFile)).lexically_relative(impl_->runtime.dataRoot);
        if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("Portable settings must stay within the application data directory");
    }
    impl_->settings = std::make_unique<SettingsStore>(std::move(settingsFile));
    impl_->recovery = std::make_unique<RecoveryStore>(impl_->runtime.dataRoot, impl_->root);
    auto report = impl_->recovery->collect([&](const std::filesystem::path& path) {
        try { return recovery_digest(read_file(impl_->root / path, MAX_DOCUMENT_BYTES)); }
        catch (...) { return recovery_digest(std::string()); }
    });
    for (auto& entry : report.recoverable) {
        try {
            const auto path = impl_->resolve(entry.documentPath);
            const bool exists = std::filesystem::exists(path);
            auto record = std::make_unique<Record>(entry.documentPath, exists ? read_file(path, MAX_DOCUMENT_BYTES) : std::string(), exists);
            record->recoveryBase = entry.baseDigest;
            record->document.apply({{0, record->document.size(), entry.text}}, record->document.revision());
            record->refresh_format();
            impl_->documents.emplace("d" + std::to_string(impl_->nextDocument++), std::move(record));
            impl_->recoveredDocuments.push_back(entry.documentPath);
        } catch (...) { impl_->recoveryConflicts.push_back(entry.documentPath); }
    }
    for (auto& entry : report.conflicted) impl_->recoveryConflicts.push_back(entry.documentPath);
}
Application::~Application() = default;
std::string Application::session_id() const { return impl_->session; }
const Json& Application::metadata_() {
    static const Json metadata = []() -> Json {
        Json commands = Json::array();
        for (auto name : {"about", "licenses", "capabilities", "workspace.status", "workspace.canLeave", "workspace.switch", "workspace.files", "workspace.trust", "document.open", "document.read", "document.status", "document.highlight", "document.find", "document.replace", "document.edit", "document.undo", "document.redo", "document.save", "doctor", "build", "run", "test", "task.status", "task.wait", "task.cancel", "project.new", "package.search", "package.add", "package.remove", "toolchain.list", "toolchain.install", "environment.prepare", "language.query", "language.result", "language.cancel", "language.navigate", "language.release", "language.completion", "language.hover", "language.definition", "language.references", "language.symbols", "language.signature", "language.diagnostics", "language.stop", "modules.graph", "modules.info", "modules.contexts"}) commands.push_back(name);
        commands.push_back("settings.get"); commands.push_back("settings.update");
        Json operations = Json::array(); for (const auto& [name, method] : Impl::query_methods()) operations.push_back(name);
        Json schemas = Json::object();
        for (const auto& name : commands) {
            const auto command = name.get<std::string>();
            Json properties = Json::object(); Json required = Json::array();
            auto field = [&](std::string key, std::string type, bool needed = false) { properties[key] = {{"type", type}}; if (needed) required.push_back(key); };
            if (command == "settings.update") { field("revision", "integer", true); field("changes", "object", true); }
            else if (command == "language.query") { field("document", "string", true); field("revision", "integer", true); field("operation", "string", true); field("position", "object"); field("range", "object"); field("name", "string"); field("timeoutMs", "integer"); properties["operation"]["enum"] = operations; properties["timeoutMs"]["minimum"] = 100; properties["timeoutMs"]["maximum"] = 120000; }
            else if (command == "language.result" || command == "language.cancel" || command == "language.release") { field("query", "string", true); if (command == "language.result") field("includeResult", "boolean"); }
            else if (command == "language.navigate") { field("query", "string", true); field("location", "integer", true); }
            else if (command == "document.open") { field("path", "string", true); field("create", "boolean"); }
            else if (command.starts_with("document.") || command.starts_with("language.") || command.starts_with("modules.")) {
                if (command != "language.stop" && command != "language.diagnostics") field("document", "string", true);
                if (command == "document.edit" || command == "document.undo" || command == "document.redo" || command == "document.save" || command == "document.find" || command == "document.replace") field("revision", "integer", true);
                if (command == "document.edit") { field("edits", "array", true); field("historyGroup", "string"); }
                if (command == "document.find" || command == "document.replace") { field("query", "string", true); field("coordinateSpace", "string"); field("start", "integer"); field("direction", "string"); if (command == "document.find") field("limit", "integer"); else { field("replacement", "string", true); field("all", "boolean"); } }
                if (command == "document.highlight") { field("startLine", "integer"); field("maxLines", "integer"); }
                if (command == "modules.info") field("name", "string", true);
                if (command == "language.completion" || command == "language.hover" || command == "language.definition" || command == "language.references" || command == "language.signature") field("position", "object", true);
            } else if (command.starts_with("task.")) { field("task", "string", true); field("timeoutMs", "integer"); }
            else if (command == "workspace.switch") { field("path", "string", true); field("generation", "integer", true); }
            else if (command == "workspace.trust") field("allow", "boolean", true);
            else if (command == "project.new" || command.starts_with("package.")) field("name", "string", true);
            else if (command == "toolchain.install") { field("family", "string", true); field("version", "string", true); }
            if (command == "build" || command == "run" || command == "test") { field("target", "string"); field("profile", "string"); field("features", "string"); }
            if (command == "build" || command == "run" || command == "test" || command == "project.new" || command.starts_with("package.") || command.starts_with("toolchain.") || command == "environment.prepare") field("timeoutMs", "integer");
            schemas[command] = {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
            if (command == "language.query") {
                schemas[command]["properties"]["position"] = {{"type", "object"}, {"required", Json::array({"line", "character"})}, {"additionalProperties", false},
                    {"properties", {{"line", {{"type", "integer"}, {"minimum", 0}}}, {"character", {{"type", "integer"}, {"minimum", 0}}}}}};
                schemas[command]["allOf"] = Json::array({
                    {{"if", {{"properties", {{"operation", {{"enum", Json::array({"completion", "hover", "definition", "references", "signature"})}}}}}}}, {"then", {{"required", Json::array({"position"})}}}, {"else", {{"not", {{"required", Json::array({"position"})}}}}}},
                    {{"if", {{"properties", {{"operation", {{"const", "modules.info"}}}}}}}, {"then", {{"required", Json::array({"name"})}}}, {"else", {{"not", {{"required", Json::array({"name"})}}}}}}
                });
            }
        }
        return {{"version", "0.0.1"}, {"protocolVersion", 1}, {"positionEncoding", "utf-16"}, {"editOffsets", "utf-8-bytes"}, {"sessionTransport", "stdio-jsonl"}, {"maxDocumentBytes", MAX_DOCUMENT_BYTES}, {"languageTransport", Language::limits()}, {"asyncLanguage", {{"operations", operations}, {"minTimeoutMs", 100}, {"maxTimeoutMs", 120000}, {"maxActive", Impl::MAX_ACTIVE_QUERIES}, {"maxRetained", Impl::MAX_RETAINED_QUERIES}, {"snapshotBytes", Impl::MAX_QUERY_SNAPSHOT_BYTES}, {"resultBytes", Impl::MAX_QUERY_RESULT_BYTES}, {"resultBytesIncludeNavigationSnapshots", true}, {"navigation", {{"maxLocations", 200}, {"maxTargets", 32}, {"targetSnapshotBytes", Impl::MAX_QUERY_RESULT_BYTES}}}, {"revisionBound", true}}}, {"commands", commands}, {"commandSchemas", schemas}};
    }();
    return metadata;
}
Json Application::capabilities() const { return metadata_(); }

Json Application::execute(const Json& request) {
    const std::lock_guard<std::recursive_mutex> serialized(impl_->executeMutex);
    Json response = {{"schemaVersion", 1}, {"kind", "meditor.result"}, {"kindVersion", 1}, {"session", session_id()}, {"requestId", request.is_object() ? request.value("id", Json(nullptr)) : Json(nullptr)}, {"effects", Json::array()}};
    try {
        if (!request.is_object()) throw CommandError("invalid-params", "Request must be a JSON object");
        if (request.contains("workspaceGeneration") && (!request["workspaceGeneration"].is_number_integer() || request["workspaceGeneration"].get<std::uint64_t>() != impl_->workspaceGeneration))
            throw CommandError("workspace-conflict", "Request belongs to an older workspace generation");
        auto command = request.at("command").get<std::string>(); auto params = request.value("params", Json::object());
        if (!params.is_object()) throw CommandError("invalid-params", "Parameters must be an object");
        const auto& schemas = metadata_().at("commandSchemas");
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
        if (command == "document.replace" && response["data"]["revision"].get<std::uint64_t>() != params["revision"].get<std::uint64_t>()) response["effects"].push_back("document-change");
        if (command == "workspace.switch" && response["data"].value("changed", false)) response["effects"].push_back("workspace-change");
        if (command == "document.save") response["effects"].push_back("file-write");
        if (command == "language.navigate" && response["data"].value("opened", false)) response["effects"].push_back("document-open");
        if (command == "settings.update") response["effects"].push_back("settings-change");
        if (command == "language.query" || (response["data"].is_object() && response["data"].value("state", "") == "running" && response["data"].contains("task") && !command.starts_with("task."))) response["effects"].push_back("tool-execution");
    } catch (const CommandError& error) { response["ok"] = false; response["error"] = {{"code", error.code}, {"message", redact(error.what(), impl_->root)}}; }
    catch (const FileReadOnlyError& error) { response["ok"] = false; response["error"] = {{"code", "file-read-only"}, {"message", error.what()}}; }
    catch (const SettingsError& error) { response["ok"] = false; response["error"] = {{"code", error.code}, {"message", error.what()}}; }
    catch (const nlohmann::json::exception&) { response["ok"] = false; response["error"] = {{"code", "invalid-params"}, {"message", "Missing or invalid command parameters"}}; }
    catch (const std::exception& error) { response["ok"] = false; response["error"] = {{"code", "operation-failed"}, {"message", redact(error.what(), impl_->root)}}; }
    response["workspaceGeneration"] = impl_->workspaceGeneration;
    return response;
}
}

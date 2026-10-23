// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.ui.workbench;
import std;
import huxerui;
import meditor.application;
import meditor.session;
import meditor.document;
import meditor.platform;
import meditor.ui.editor;
import meditor.ui.theme;
import meditor.shortcuts;

namespace meditor::ui {
using namespace huxerui;
namespace {
std::filesystem::path initialWorkspace;
std::filesystem::path initialTools;
std::filesystem::path initialSettings;
meditor::RuntimeOptions initialRuntime;
std::string initialDocument;
std::string_view language_title(std::string_view operation) {
    if (operation == "diagnostics") return "language.problems";
    if (operation == "symbols") return "language.outline";
    return operation == "definition" ? "language.definition" : "language.references";
}
struct LanguageRow {
    std::string label;
    Position start, end;
    bool navigable {};
    int severity {};
    std::optional<std::size_t> location;
    std::string stateKey;
};
std::string language_label(std::string text, std::size_t limit = 2048) {
    if (text.size() > limit) {
        while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80) --limit;
        text.resize(limit); text += "…";
    }
    for (auto& character : text) if (static_cast<unsigned char>(character) < 32) character = ' ';
    return text;
}
bool language_range(const Json& value, LanguageRow& row) {
    try {
        auto position = [](const Json& item) {
            const auto line = item.at("line").get<std::int64_t>(), character = item.at("character").get<std::int64_t>();
            if (!item.at("line").is_number_integer() || !item.at("character").is_number_integer() || line < 0 || character < 0 || line > static_cast<std::int64_t>(MAX_DOCUMENT_BYTES) || character > static_cast<std::int64_t>(MAX_DOCUMENT_BYTES))
                throw std::runtime_error("Invalid language position");
            return Position{static_cast<std::size_t>(line), static_cast<std::size_t>(character)};
        };
        row.start = position(value.at("start")); row.end = position(value.at("end"));
        return std::tie(row.start.line, row.start.character) <= std::tie(row.end.line, row.end.character);
    } catch (const std::exception&) { return false; }
}
struct WorkspaceModel {
    std::shared_ptr<meditor::Application> application;
    std::unique_ptr<meditor::SessionServer> session;
    std::shared_ptr<meditor::Application> preferences;
    std::filesystem::path settingsFile;
    meditor::RuntimeOptions runtime;
    std::vector<std::string> files;
    std::map<std::string, std::shared_ptr<EditorState>> editors;
    std::vector<std::string> tabs;
    std::string active;
    std::string name;
    i18n::Localizer messages;
    std::string systemLanguage {"en"}, languagePreference {"auto"};
    std::string statusKey {"status.ready"}, statusDetail;
    void notice(std::string key) { statusKey = std::move(key); statusDetail.clear(); }
    void detail(std::string text) { statusKey.clear(); statusDetail = std::move(text); }
    std::string status_text() const { return statusKey.empty() ? statusDetail : messages.text(statusKey); }
    void report_error(const Json& error) {
        auto key = "error." + error.value("code", "operation-failed");
        notice(messages.contains(key) ? key : "error.operation-failed");
    }
    void resolve_language() { messages = i18n::Localizer(i18n::select_language(languagePreference, systemLanguage)); }
    std::string output;
    bool selected {};
    bool busy {};
    bool choosing {};
    std::string activeTask, taskKind, taskStateKey {"status.ready"}, bottomPanel;
    std::uint64_t completionSequence {0};
    std::function<void()> completionDriver;
    std::uint64_t generation {};
    bool dark {true};
    bool reducedMotion {};
    bool settingsOpen {};
    bool aboutOpen {};
    bool searchOpen {};
    std::string searchQuery, searchReplacement, searchErrorKey;
    std::optional<std::size_t> searchReplaced;
    std::size_t searchTotal {};
    bool searchWrapped {};
    std::string languageOperation, languageQuery, languageResultQuery, languageDocument, languageState {"language.empty"};
    std::uint64_t languageRevision {}, languageSequence {};
    bool languageVerified {}, languageTruncated {};
    std::vector<LanguageRow> languageRows;
    void cancel_language() {
        if (application && !languageQuery.empty()) application->execute({{"command", "language.cancel"}, {"params", {{"query", languageQuery}}}});
        if (application && !languageResultQuery.empty()) application->execute({{"command", "language.release"}, {"params", {{"query", languageResultQuery}}}});
        languageQuery.clear(); languageResultQuery.clear(); ++languageSequence;
    }
    void close_language() { cancel_language(); languageOperation.clear(); languageRows.clear(); if (bottomPanel == "language") bottomPanel.clear(); }
    bool language_navigation() const { return languageOperation == "definition" || languageOperation == "references"; }
    bool language_current() const {
        if (!languageResultQuery.empty()) {
            const auto status = application->execute({{"command", "language.result"}, {"params", {{"query", languageResultQuery}, {"includeResult", false}}}});
            if (!status.value("ok", false) || status["data"].value("stale", true)) return false;
            if (language_navigation()) return true;
        }
        return active == languageDocument && editors.contains(active) && editors.at(active)->revision() == languageRevision;
    }
    void adopt_language_result(const Json& status) {
        const auto& result = status.at("result");
        languageResultQuery = status.at("query").get<std::string>();
        languageRows.clear(); languageTruncated = false; languageVerified = languageOperation != "diagnostics";
        auto add = [&](const Json& value, std::string label, bool navigate, int severity = 0) {
            LanguageRow row; row.label = language_label(std::move(label));
            row.severity = severity;
            row.navigable = language_range(value, row) && navigate;
            languageRows.push_back(std::move(row));
        };
        if (language_navigation()) {
            languageTruncated = status.value("locationsTruncated", false);
            for (const auto& location : status.at("locations")) {
                LanguageRow row; row.label = language_label(location.value("path", "")); row.location = languageRows.size();
                row.navigable = location.value("available", false) && language_range(location.value("range", Json::object()), row);
                if (!row.navigable) {
                    const auto reason = location.value("reason", "");
                    row.stateKey = reason == "outside-workspace" ? "language.externalLocation" : reason == "navigation-limit" ? "language.navigationLimited" : "language.unavailableLocation";
                }
                languageRows.push_back(std::move(row));
            }
        } else if (languageOperation == "diagnostics") {
            languageVerified = result.at("versionVerified").get<bool>();
            for (const auto& publication : result.at("diagnostics")) for (const auto& item : publication.at("diagnostics")) {
                if (languageRows.size() == 200) { languageTruncated = true; break; }
                add(item.value("range", Json::object()), item.at("message").get<std::string>(), languageVerified, item.value("severity", 0));
            }
        } else {
            if (result.is_null()) { languageState = "language.empty"; return; }
            if (!result.is_array()) throw std::runtime_error("Invalid symbol result");
            std::function<void(const Json&, std::size_t)> collect = [&](const Json& items, std::size_t depth) {
                for (const auto& item : items) {
                    if (languageRows.size() == 200 || depth > 32) { languageTruncated = true; return; }
                    // SymbolInformation has a URI and must wait for workspace-owned URI resolution.
                    add(item.value("selectionRange", item.value("range", Json::object())), std::string(depth * 2, ' ') + item.at("name").get<std::string>(), !item.contains("location"));
                    if (item.contains("children")) { if (!item["children"].is_array()) throw std::runtime_error("Invalid symbol children"); collect(item["children"], depth + 1); }
                }
            };
            collect(result, 0);
        }
        languageState = languageRows.empty() ? "language.empty" : "language.ready";
    }
    Json aboutInfo;
    std::string licenseId;
    std::uint64_t settingsRevision {};
    bool navigation {true};
    std::optional<FileReference> grant;
    void adopt_settings(const Json& state) {
        settingsRevision = state.at("revision").get<std::uint64_t>();
        languagePreference = state.at("values").at("language").get<std::string>();
        resolve_language();
        dark = state.at("values").at("theme") == "dark";
        reducedMotion = state.at("values").at("reducedMotion").get<bool>();
    }
    bool load(std::filesystem::path root, std::optional<std::uint64_t> expected = {}) {
        Json response;
        auto next = application;
        if (next) {
            response = next->execute({{"command", "workspace.switch"}, {"params", {{"path", meditor::path_to_utf8(root)}, {"generation", expected.value_or(generation)}}}});
            if (!response.value("ok", false)) { report_error(response["error"]); return false; }
            if (!response["data"].value("changed", false)) return true;
        } else {
            next = std::make_shared<meditor::Application>(root, initialTools, settingsFile, runtime);
            response = next->execute({{"command", "workspace.files"}});
            if (!response.value("ok", false)) { report_error(response["error"]); return false; }
        }
        const auto& listing = application ? response["data"]["listing"] : response["data"];
        std::vector<std::string> nextFiles;
        for (const auto& file : listing["files"]) nextFiles.push_back(file.get<std::string>());
        files.swap(nextFiles);
        name = application ? response["data"]["name"].get<std::string>() : meditor::path_to_utf8(root.filename());
        generation = response["workspaceGeneration"].get<std::uint64_t>();
        close_language(); session.reset(); application = std::move(next); editors.clear(); tabs.clear(); active.clear(); selected = true;
        try {
            session = std::make_unique<meditor::SessionServer>(*application, meditor::RuntimeDirectories::resolve(runtime).dataRoot, root);
        } catch (const std::exception&) { session.reset(); }
        busy = false; activeTask.clear(); taskKind.clear(); output.clear(); bottomPanel.clear(); searchOpen = false;
        searchTotal = 0; searchReplaced.reset(); searchErrorKey.clear();
        notice("status.workspaceOpened");
        auto state = application->execute({{"command", "workspace.status"}});
        if (state.value("ok", false)) {
            const auto& recovery = state["data"]["recovery"];
            if (!recovery["recovered"].empty()) notice("status.recoveredDocuments");
            else if (!recovery["conflicts"].empty()) notice("status.recoveryConflicts");
        }
        return true;
    }
};
}
void configure(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view argument = argv[i];
        if (argument == "--workspace" && i + 1 < argc) initialWorkspace = std::filesystem::u8path(argv[++i]);
        else if (argument == "--tool-root" && i + 1 < argc) initialTools = std::filesystem::u8path(argv[++i]);
        else if (argument == "--settings-file" && i + 1 < argc) initialSettings = std::filesystem::u8path(argv[++i]);
        else if (argument == "--data-root" && i + 1 < argc) initialRuntime.dataRoot = std::filesystem::u8path(argv[++i]);
        else if (argument == "--portable") initialRuntime.portable = true;
        else if (argument == "--open" && i + 1 < argc) initialDocument = argv[++i];
    }
}
View workbench() { return workbench(WorkspaceServices{}); }
View workbench(WorkspaceServices services) {
    return Scope([services = std::move(services)]() -> View {
        auto model = UseState(std::make_shared<WorkspaceModel>());
        auto epoch = UseState(0);
        auto findValue = UseState(TextEditingValue::FromText(""));
        auto replaceValue = UseState(TextEditingValue::FromText(""));
        auto path = UseState(TextEditingValue::FromText(""));
        auto tasks = UseTaskScope();
        auto picker = UseService<FilePicker>();
        auto window = UseWindow();
        auto host = UseApplication();
        auto transition = UseSceneTransition();
        auto compact = UseViewportClass() == ViewportClass::Compact;
        auto refresh = [epoch] { epoch += 1; };
        auto data = model.Get(); (void)epoch.Get();
        data->systemLanguage = UseEnvironment<Locale>().LanguageTag();
        data->resolve_language();
        auto t = [data](std::string_view id) { return data->messages.text(id); };
        window.OnCloseRequest([data, refresh] {
            if (!data->application) return false;
            auto state = data->application->execute({{"command", "workspace.canLeave"}});
            if (!state.value("ok", false)) { data->report_error(state["error"]); refresh(); return true; }
            if (!state["data"]["canLeave"].get<bool>()) {
                data->notice(state["data"]["unsavedDocuments"].empty() ? "error.task-busy" : "status.saveBeforeClose");
                refresh(); return true;
            }
            return false;
        });
        auto run = [data, refresh](std::string command, Json params = Json::object()) {
            if (!data->application) { data->notice("status.openWorkspace"); refresh(); return Json(); }
            auto result = data->application->execute({{"command", command}, {"params", std::move(params)}});
            if (!result.value("ok", false)) data->report_error(result["error"]);
            refresh(); return result;
        };
        auto driveCompletion = [data, tasks, refresh] {
            if (data->active.empty() || !data->application || !data->editors.contains(data->active)) return;
            const auto editor = data->editors.at(data->active);
            if (editor->language_id() != "cpp") return;
            const auto snapshot = editor->completion_snapshot();
            if (snapshot.desired == data->completionSequence) return;
            const auto previous = data->completionSequence;
            data->completionSequence = snapshot.desired;
            const auto position = editor->lsp_position();
            auto reply = data->application->execute({{"command", "language.query"}, {"params", {
                {"document", data->active}, {"revision", editor->revision()}, {"operation", "completion"},
                {"position", {{"line", position.line}, {"character", position.character}}}, {"timeoutMs", 8000}}}});
            if (!reply.value("ok", false)) { data->completionSequence = previous; editor->dismiss_completion(); refresh(); return; }
            const auto id = reply["data"]["query"].get<std::string>();
            const auto application = data->application; const auto generation = data->generation;
            const auto document = data->active; const auto revision = editor->revision();
            const auto desired = snapshot.desired;
            try {
            tasks.Launch([=]() -> Task<void> {
                for (;;) {
                    co_await Delay(std::chrono::milliseconds(40));
                    if (data->application != application || data->generation != generation) { editor->dismiss_completion(); refresh(); co_return; }
                    if (data->editors.find(document) == data->editors.end()) co_return;
                    if (data->editors.at(document)->completion_snapshot().desired != desired) co_return; // superseded by newer typing
                    const auto result = application->execute({{"command", "language.result"}, {"params", {{"query", id}}}});
                    if (!result.value("ok", false)) { editor->dismiss_completion(); refresh(); co_return; }
                    const auto state = result["data"].value("state", "failed");
                    if (state == "running") continue;
                    if (state == "succeeded" && !result["data"].value("stale", true)) {
                        std::vector<EditorState::CompletionItem> items;
                        const auto& payload = result["data"].contains("result") && result["data"]["result"].contains("items") ? result["data"]["result"]["items"] : Json::array();
                        for (const auto& item : payload) {
                            EditorState::CompletionItem entry;
                            entry.label = item.value("label", "");
                            if (entry.label.empty()) continue;
                            entry.insert = item.value("insertText", entry.label);
                            items.push_back(std::move(entry));
                        }
                        editor->set_completions(std::move(items), revision);
                    } else editor->dismiss_completion();
                    refresh(); co_return;
                }
            });
            } catch (...) { data->completionSequence = previous; } // Launch is only legal from event contexts; the next change retries.
        };
        data->completionDriver = driveCompletion;
        auto open = [data, refresh, run](std::string name) {
            auto result = run("document.open", {{"path", name}});
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["document"].get<std::string>();
            if (data->active != id && !data->languageOperation.empty()) { data->cancel_language(); data->languageRows.clear(); data->languageState = "language.stale"; }
            if (!data->editors.contains(id)) {
                data->editors[id] = std::make_shared<EditorState>(data->application, id, [data, refresh] { refresh(); if (data->completionDriver) data->completionDriver(); });
                data->tabs.push_back(id);
            }
            data->active = id; data->detail(name); refresh();
        };
        Lifecycle([data, refresh, open, host] {
            try {
                data->settingsFile = initialSettings;
                data->runtime = initialRuntime;
#if defined(__ANDROID__)
                const auto& directories = host.Directories();
                if (data->runtime.dataRoot.empty()) data->runtime.dataRoot = std::filesystem::u8path(directories.data_directory.Path());
                if (data->runtime.binaryDirectory.empty() && directories.executable_directory)
                    data->runtime.binaryDirectory = std::filesystem::u8path(directories.executable_directory->Path());
                if (data->settingsFile.empty()) data->settingsFile = std::filesystem::u8path(host.Directories().data_directory.Child("settings.json").Path());
#endif
                data->preferences = std::make_shared<meditor::Application>(std::filesystem::current_path(), initialTools, data->settingsFile, data->runtime);
                auto settings = data->preferences->execute({{"command", "settings.get"}});
                if (settings.value("ok", false)) {
                    data->adopt_settings(settings["data"]);
                } else data->report_error(settings["error"]);
            } catch (...) { data->notice("status.settingsUnavailable"); }
            if (!initialWorkspace.empty()) {
                try {
                    data->load(initialWorkspace);
                    if (!initialDocument.empty()) open(initialDocument);
                } catch (...) { data->notice("status.openFailed"); }
                refresh();
            }
            refresh();
        });
        auto refreshSearch = [data, refresh] {
            if (data->active.empty()) return;
            auto editor = data->editors.at(data->active); editor->refresh();
            data->searchTotal = 0; data->searchWrapped = false; data->searchErrorKey.clear();
            if (data->searchQuery.empty()) { editor->clear_search_results(); refresh(); return; }
            auto result = data->application->execute({{"command", "document.find"}, {"params", {{"document", data->active}, {"revision", editor->revision()}, {"query", data->searchQuery}, {"coordinateSpace", "display"}}}});
            if (!result.value("ok", false)) { data->searchErrorKey = result["error"].value("code", "") == "invalid-params" ? "search.invalidQuery" : "error.operation-failed"; editor->clear_search_results(); refresh(); return; }
            data->searchTotal = result["data"]["total"].get<std::size_t>(); std::vector<TextRange> ranges;
            for (const auto& match : result["data"]["matches"]) ranges.push_back({match["utf16Start"].get<TextOffset>(), match["utf16End"].get<TextOffset>()});
            editor->set_search_results(std::move(ranges), result["data"]["revision"].get<std::uint64_t>()); refresh();
        };
        auto findMatch = [data, refresh, refreshSearch](int direction, std::optional<std::size_t> cursor = {}) {
            if (data->active.empty()) return;
            refreshSearch(); if (data->searchQuery.empty() || !data->searchErrorKey.empty()) return; auto editor = data->editors.at(data->active); const auto selection = editor->selection_bytes();
            Json params {{"document", data->active}, {"revision", editor->revision()}, {"query", data->searchQuery}, {"coordinateSpace", "display"}, {"limit", 1}};
            if (direction) { params["direction"] = direction < 0 ? "backward" : "forward"; params["start"] = cursor.value_or(direction < 0 ? selection.first : selection.second); }
            auto result = data->application->execute({{"command", "document.find"}, {"params", params}});
            if (result.value("ok", false) && result["data"]["matches"].empty() && direction) {
                params.erase("start"); result = data->application->execute({{"command", "document.find"}, {"params", params}});
                data->searchWrapped = result.value("ok", false) && !result["data"]["matches"].empty();
            }
            if (!result.value("ok", false)) data->searchErrorKey = "error.operation-failed";
            else if (!result["data"]["matches"].empty()) { const auto& match = result["data"]["matches"][0]; editor->select_match({match["utf16Start"].get<TextOffset>(), match["utf16End"].get<TextOffset>()}, result["data"]["revision"].get<std::uint64_t>()); }
            refresh();
        };
        auto closeSearch = [data, refresh] { data->searchOpen = false; for (auto& [id, editor] : data->editors) editor->clear_search_results(); refresh(); };
        auto openSearch = [data, refresh, refreshSearch, compact] {
            if (data->active.empty() || data->settingsOpen || data->aboutOpen) return;
            data->searchOpen = true; if (compact) data->navigation = false; data->searchReplaced.reset(); refreshSearch(); refresh();
        };
        auto replaceMatches = [data, refresh, refreshSearch, findMatch](bool all) {
            if (data->active.empty() || data->searchQuery.empty()) return;
            auto editor = data->editors.at(data->active); editor->refresh();
            if (editor->text_format().loneCarriageReturns) { data->searchErrorKey = "editor.unsupportedLineEndings"; refresh(); return; }
            data->searchReplaced.reset(); data->searchErrorKey.clear();
            Json params {{"document", data->active}, {"revision", editor->revision()}, {"query", data->searchQuery}, {"replacement", data->searchReplacement}, {"coordinateSpace", "display"}, {"all", all}};
            if (!all) params["start"] = editor->selection_bytes().first;
            auto result = data->application->execute({{"command", "document.replace"}, {"params", params}});
            if (!result.value("ok", false)) { auto key = "error." + result["error"].value("code", "operation-failed"); data->searchErrorKey = data->messages.contains(key) ? key : "error.operation-failed"; }
            else { editor->refresh(); if (!all && result["data"].contains("nextStart")) { const auto next = result["data"]["nextStart"].get<std::size_t>(); editor->select_byte_range(next, next, editor->revision()); } data->searchReplaced = result["data"]["replacements"].get<std::size_t>(); refreshSearch(); if (!all && result["data"].contains("nextStart")) findMatch(1, result["data"]["nextStart"].get<std::size_t>()); }
            refresh();
        };
        auto updateSettings = [data, refresh](Json changes) {
            if (!data->preferences) { data->notice("status.settingsUnavailable"); refresh(); return; }
            auto result = data->preferences->execute({{"command", "settings.update"}, {"params", {{"revision", data->settingsRevision}, {"changes", std::move(changes)}}}});
            if (!result.value("ok", false)) {
                data->report_error(result["error"]);
                if (result["error"].value("code", "") == "settings-conflict")
                    result = data->preferences->execute({{"command", "settings.get"}});
            }
            if (result.value("ok", false)) {
                data->adopt_settings(result["data"]);
            }
            refresh();
        };
        auto showSettings = [data, refresh] {
            const auto fromAbout = data->aboutOpen;
            data->aboutOpen = false; data->licenseId.clear();
            data->settingsOpen = fromAbout || !data->settingsOpen;
            if (data->settingsOpen && data->preferences) {
                auto result = data->preferences->execute({{"command", "settings.get"}});
                if (result.value("ok", false)) data->adopt_settings(result["data"]);
                else data->report_error(result["error"]);
            }
            refresh();
        };
        auto showAbout = [data, refresh] {
            if (data->aboutOpen) { data->aboutOpen = false; data->licenseId.clear(); refresh(); return; }
            if (!data->preferences) { data->notice("status.settingsUnavailable"); refresh(); return; }
            const auto response = data->preferences->execute({{"command", "about"}});
            if (response.value("ok", false)) { data->aboutInfo = response["data"]; data->aboutOpen = true; data->licenseId.clear(); }
            else data->report_error(response["error"]);
            refresh();
        };
        auto toggleTheme = [data, updateSettings, transition] {
            if (data->reducedMotion) { updateSettings({{"theme", data->dark ? "light" : "dark"}}); return; }
            transition.Run(TransitionSpec{FadeTransition{}, TweenSpec{0.12}}, [data, updateSettings] {
                updateSettings({{"theme", data->dark ? "light" : "dark"}});
            });
        };
        auto save = [data, run, refresh] {
            if (data->active.empty()) return;
            auto current = run("document.read", {{"document", data->active}});
            if (!current.value("ok", false)) { refresh(); return; }
            if (current["data"]["format"].value("loneCarriageReturns", false)) { data->notice("editor.unsupportedLineEndings"); refresh(); return; }
            auto result = run("document.save", {{"document", data->active}, {"revision", current["data"]["revision"]}});
            if (result.value("ok", false)) data->notice(result["data"].value("languageSaveNotification", Json::object()).value("state", "") == "unavailable" ? "status.savedLanguageUnavailable" : "status.saved");
            refresh();
        };
        auto executeTask = [data, tasks, run, refresh](std::string command) {
            if (data->busy) return;
            auto result = run(command);
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["task"].get<std::string>(); data->activeTask = id;
            auto application = data->application; auto generation = data->generation;
            data->busy = true; data->taskKind = command; data->taskStateKey = "status.taskRunning";
            if (!data->active.empty()) data->editors.at(data->active)->request_focus();
            data->output.clear(); data->bottomPanel = "output"; data->notice("status.taskRunning"); refresh();
            tasks.Launch([=]() -> Task<void> {
                while (true) {
                    co_await Delay(std::chrono::milliseconds(100));
                    if (data->application != application || data->generation != generation) co_return;
                    auto result = application->execute({{"command", "task.status"}, {"workspaceGeneration", generation}, {"params", {{"task", id}}}});
                    if (!result.value("ok", false)) { data->report_error(result["error"]); data->taskStateKey = data->statusKey; break; }
                    const auto output = result["data"].value("output", "");
                    if (data->output != output) { data->output = output; refresh(); }
                    if (result["data"].value("state", "") != "running") { auto key = "status.task." + result["data"].value("state", ""); data->taskStateKey = data->messages.contains(key) ? key : "status.taskFinished"; data->notice(data->taskStateKey); break; }
                }
                data->busy = false; data->activeTask.clear(); refresh();
            });
        };
        auto queryLanguage = [data, tasks, refresh, compact](std::string operation) {
            if (data->active.empty() || data->settingsOpen || data->aboutOpen || data->searchOpen || data->editors.at(data->active)->language_id() != "cpp") return;
            auto editor = data->editors.at(data->active); editor->refresh();
            data->cancel_language(); data->languageOperation = std::move(operation);
            data->bottomPanel = "language";
            data->languageDocument = data->active; data->languageRevision = editor->revision();
            data->languageRows.clear(); data->languageVerified = false; data->languageTruncated = false;
            if (compact) data->navigation = false;
            Json params {{"document", data->active}, {"revision", editor->revision()}, {"operation", data->languageOperation}};
            if (data->language_navigation()) { const auto position = editor->lsp_position(); params["position"] = {{"line", position.line}, {"character", position.character}}; }
            auto reply = data->application->execute({{"command", "language.query"}, {"params", std::move(params)}});
            if (!reply.value("ok", false)) {
                const auto key = "error." + reply["error"].value("code", "operation-failed");
                data->languageState = data->messages.contains(key) ? key : "error.operation-failed"; refresh(); return;
            }
            data->languageQuery = reply["data"]["query"].get<std::string>(); data->languageState = "language.running";
            const auto application = data->application; const auto generation = data->generation;
            const auto sequence = data->languageSequence; const auto id = data->languageQuery; refresh();
            tasks.Launch([=]() -> Task<void> {
                while (true) {
                    co_await Delay(std::chrono::milliseconds(100));
                    if (data->application != application || data->generation != generation || data->languageSequence != sequence) co_return;
                    if (!data->language_current()) { data->cancel_language(); data->languageState = "language.stale"; refresh(); co_return; }
                    const auto reply = application->execute({{"command", "language.result"}, {"params", {{"query", id}}}});
                    if (!reply.value("ok", false)) { data->languageQuery.clear(); data->languageState = "error.operation-failed"; refresh(); co_return; }
                    const auto& result = reply["data"]; const auto state = result.value("state", "failed");
                    if (state == "running") continue;
                    data->languageQuery.clear();
                    if (result.value("stale", true)) data->languageState = "language.stale";
                    else if (state == "cancelled") data->languageState = "language.cancelled";
                    else if (state != "succeeded") {
                        const auto key = "error." + result["error"].value("code", "operation-failed");
                        data->languageState = data->messages.contains(key) ? key : "error.operation-failed";
                    } else {
                        try { data->adopt_language_result(result); }
                        catch (const std::exception&) { data->languageRows.clear(); data->languageState = "language.invalidResult"; }
                    }
                    refresh(); co_return;
                }
            });
        };
        auto choose = [data, tasks, picker, refresh, services] {
            if (data->choosing) return;
            if (data->application) {
                auto state = data->application->execute({{"command", "workspace.canLeave"}});
                if (!state.value("ok", false)) { data->report_error(state["error"]); refresh(); return; }
                if (!state["data"]["canLeave"].get<bool>()) {
                    data->notice(state["data"]["unsavedDocuments"].empty() ? "error.task-busy" : "status.saveBeforeSwitch"); refresh(); return;
                }
            }
            auto generation = data->generation;
            data->choosing = true; refresh();
            tasks.Launch([=]() -> Task<void> {
                try {
                    std::optional<WorkspaceSelection> selection;
                    if (services.chooseWorkspace) selection = co_await services.chooseWorkspace();
                    else {
                        auto selected = co_await picker->OpenDirectoryAsync(true);
                        if (selected) {
                            auto local = selected->AsFile();
                            if (!local) { data->choosing = false; data->notice("status.providerRequired"); refresh(); co_return; }
                            selection = WorkspaceSelection{std::filesystem::u8path(local->Path()), *selected};
                        }
                    }
                    data->choosing = false;
                    if (selection && data->load(selection->path, generation)) data->grant = std::move(selection->grant);
                } catch (...) { data->choosing = false; data->notice("status.openFailed"); }
                refresh();
            });
        };
        const auto colors = palette(data->dark);
        const auto pane = colors.surface, background = colors.canvas, ink = colors.ink, muted = colors.muted;
        auto header = WindowTitleBar {
            Text("m", TextRole::Title).With(Padding(8), Background(colors.accent), Foreground(colors.onAccent), CornerRadius(9)),
            Text("meditor", TextRole::Title),
            compact ? View(Spacer().With(Grow{1})) : View(Text(data->selected ? data->name : t("app.tagline"), TextRole::Label).With(Grow{1}, Foreground(muted))),
            Button(compact ? t("action.files") : t("action.explorer")).Key("explorer-toggle").OnClick([data, refresh] { data->navigation = !data->navigation; refresh(); }),
            Button(data->dark ? t("action.light") : t("action.dark")).Key("theme-toggle").OnClick(toggleTheme),
        }.Key("window-title-bar").With(Padding(12), Spacing(12), CrossAlign(CrossAxisAlignment::Center));
        View body;
        if (!data->selected) {
            body = Column {
                Text(t("welcome.title"), TextRole::Title).With(FontSize(28)),
                Text(t("welcome.subtitle")).With(Foreground(muted)),
                Row {Button(t("workspace.open")).Key("workspace-open").With(Enabled(!data->choosing), Background(colors.accent), Foreground(colors.onAccent)).OnClick(choose)}.With(Spacing(10)),
                Text(t("welcome.description")).With(Foreground(muted)),
                Text(t("welcome.privacy"), TextRole::Label).With(Foreground(muted)),
            }.With(Grow{1}, Padding(40), Spacing(22), MainAlign(MainAxisAlignment::Center), CrossAlign(CrossAxisAlignment::Start));
        } else {
            auto sidebar = Column {
                Text(t("workspace.heading"), TextRole::Label).With(Foreground(muted)),
                Text(data->name, TextRole::Title),
                TextField(path).Label(t("workspace.openFile")).Placeholder("src/main.cpp").OnChanged([path](const TextEditingValue& value) { path = value; }).OnSubmitted([path, open] { open(path->text); }),
                ScrollView {
                    Column {ForEach(data->files, [data, open, colors](const std::string& name) {
                        const bool selected = !data->active.empty() && data->editors.at(data->active)->document_path() == name;
                        return Button(name).Key(name).With(Background(selected ? colors.selected : Color::Transparent()), Border{selected ? colors.accent : Color::Transparent(), 1}, Semantics{.selected = selected}).OnClick([name, open] { open(name); });
                    })}.With(Spacing(2), CrossAlign(CrossAxisAlignment::Stretch))
                }.With(Grow{1}),
                Button(t("workspace.change")).Key("workspace-change").With(Enabled(!data->choosing)).OnClick(choose),
            }.With(Padding(16), Spacing(12), Background(pane), Frame{.width = compact ? 280.0F : 260.0F}, CrossAlign(CrossAxisAlignment::Stretch));
            View editor = Column {
                Text(t("workspace.chooseFile"), TextRole::Title),
                Text(t("workspace.ready")).With(Foreground(muted)),
            }.With(Grow{1}, Padding(32), Spacing(16), MainAlign(MainAxisAlignment::Center));
            if (!data->active.empty()) editor = code_editor(data->editors.at(data->active), data->dark);
            std::vector<StringVariant> labels; std::size_t selectedTab {};
            for (std::size_t i = 0; i < data->tabs.size(); ++i) {
                const auto& id = data->tabs[i];
                const auto record = data->application->execute({{"command", "document.status"}, {"params", {{"document", id}}}})["data"];
                auto name = meditor::path_to_utf8(std::filesystem::u8path(record.value("path", t("workspace.untitled"))).filename());
                if (record.value("dirty", false)) name += " •";
                labels.emplace_back(name); if (data->active == id) selectedTab = i;
            }
            View tabs;
            if (!labels.empty()) tabs = Tabs(std::move(labels), selectedTab).Key("workspace-tabs")
                .On<TabsEvents::Changed>([data, refresh](std::size_t index) {
                    if (index >= data->tabs.size()) return;
                    const auto id = data->tabs[index];
                    if (data->active != id && !data->languageOperation.empty()) { data->cancel_language(); data->languageRows.clear(); data->languageState = "language.stale"; }
                    data->active = id; refresh();
                });
            auto toolbar = Flow {
                Button(t("action.save")).Key("action-save").With(Enabled(!data->active.empty() && !data->editors.at(data->active)->text_format().loneCarriageReturns)).OnClick(save),
                Button(t("action.find")).Key("action-find").With(Enabled(!data->active.empty())).OnClick(openSearch),
                Button(t("language.problems")).Key("action-problems").With(Enabled(!data->active.empty() && data->editors.at(data->active)->language_id() == "cpp")).OnClick([queryLanguage] { queryLanguage("diagnostics"); }),
                Button(t("language.outline")).Key("action-outline").With(Enabled(!data->active.empty() && data->editors.at(data->active)->language_id() == "cpp")).OnClick([queryLanguage] { queryLanguage("symbols"); }),
                Button(t("language.definition")).Key("action-definition").With(Enabled(!data->active.empty() && data->editors.at(data->active)->language_id() == "cpp")).OnClick([queryLanguage] { queryLanguage("definition"); }),
                Button(t("language.references")).Key("action-references").With(Enabled(!data->active.empty() && data->editors.at(data->active)->language_id() == "cpp")).OnClick([queryLanguage] { queryLanguage("references"); }),
                Button(t("action.undo")).Key("action-undo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->undo(); }),
                Button(t("action.redo")).Key("action-redo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->redo(); }),
                Button(t("action.build")).Key("action-build").With(Enabled(!data->busy), Background(colors.accent), Foreground(colors.onAccent)).OnClick([executeTask] { executeTask("build"); }),
                Button(t("action.run")).Key("action-run").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("run"); }),
                Button(t("action.test")).Key("action-test").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("test"); }),
                Button(t("action.cancelTask")).Key("action-cancel-task").With(Enabled(data->busy)).OnClick([data, run] { if (!data->activeTask.empty()) run("task.cancel", {{"task", data->activeTask}}); }),
                Button(t("action.trust")).Key("action-trust").OnClick([run] { run("workspace.trust", {{"allow", true}}); }),
            }.With(Padding(8), Spacing(6));
            View languagePanel;
            if (!data->languageOperation.empty() && data->bottomPanel == "language") {
                const auto current = data->language_current();
                const auto state = current ? data->languageState : std::string("language.stale");
                std::vector<std::size_t> rows(data->languageRows.size()); std::iota(rows.begin(), rows.end(), 0);
                languagePanel = Column {
                    Flow {
                        !data->taskKind.empty() ? View(Button(t("task.output")).Key("panel-output").OnClick([data, refresh] { data->bottomPanel = "output"; refresh(); })) : View{},
                        Text(t(language_title(data->languageOperation)), TextRole::Title),
                        Button(t("language.refresh")).Key("language-refresh").With(Enabled(data->languageQuery.empty() && !data->active.empty() && data->editors.at(data->active)->language_id() == "cpp")).OnClick([data, queryLanguage] { queryLanguage(data->languageOperation); }),
                        Button(t("language.cancel")).Key("language-cancel").With(Enabled(!data->languageQuery.empty())).OnClick([data, refresh] { data->cancel_language(); data->languageRows.clear(); data->languageState = "language.cancelled"; refresh(); }),
                        Button(t("language.close")).Key("language-close").OnClick([data, refresh] { data->close_language(); if (!data->active.empty()) data->editors.at(data->active)->request_focus(); refresh(); }),
                    }.With(Spacing(8)),
                    Text(t(state), TextRole::Label).Key("language-state"),
                    current && data->languageOperation == "diagnostics" && (state == "language.ready" || state == "language.empty") && !data->languageVerified ? View(Text(t("language.unverified"), TextRole::Label).Key("language-unverified")) : View{},
                    data->languageTruncated ? View(Text(t(data->language_navigation() ? "language.navigationLimited" : "language.truncated"), TextRole::Label).Key("language-truncated")) : View{},
                    ScrollView {Column {ForEach(rows, [data, current, t, refresh](std::size_t index) {
                        const auto row = data->languageRows.at(index);
                        const auto position = data->messages.text("language.position", {{"line", std::to_string(row.start.line + 1)}, {"column", std::to_string(row.start.character + 1)}});
                        const std::array severityNames {t("language.severity.1"), t("language.severity.2"), t("language.severity.3"), t("language.severity.4")};
                        const auto severity = row.severity >= 1 && row.severity <= 4 ? severityNames[row.severity - 1] + ": " : "";
                        const auto label = row.label + (row.stateKey.empty() ? "" : (row.label.empty() ? "" : " · ") + t(row.stateKey));
                        return Button(severity + label + (row.navigable ? " · " + position : "")).Key("language-row-" + std::to_string(index))
                            .With(Enabled(current && row.navigable), Background(Color::Transparent())).OnClick([data, row, refresh] {
                                if (!data->language_current()) { data->languageState = "language.stale"; refresh(); return; }
                                if (row.location) {
                                    const auto reply = data->application->execute({{"command", "language.navigate"}, {"params", {{"query", data->languageResultQuery}, {"location", *row.location}}}});
                                    if (!reply.value("ok", false)) {
                                        const auto code = reply["error"].value("code", "operation-failed");
                                        if (code == "revision-conflict") { data->languageRows.clear(); data->languageState = "language.stale"; }
                                        else { const auto key = "error." + code; data->languageState = data->messages.contains(key) ? key : "error.operation-failed"; }
                                        refresh(); return;
                                    }
                                    const auto& target = reply["data"]["target"]; const auto id = target["document"].get<std::string>();
                                    if (!data->editors.contains(id)) {
                data->editors[id] = std::make_shared<EditorState>(data->application, id, [data, refresh] { refresh(); if (data->completionDriver) data->completionDriver(); });
                data->tabs.push_back(id);
            }
                                    data->active = id;
                                    const auto& range = reply["data"]["range"];
                                    const Position start {range["start"]["line"].get<std::size_t>(), range["start"]["character"].get<std::size_t>()}, end {range["end"]["line"].get<std::size_t>(), range["end"]["character"].get<std::size_t>()};
                                    auto editor = data->editors.at(id);
                                    if (!editor->select_lsp_range(start, end, target["revision"].get<std::uint64_t>())) data->languageState = "language.invalidPosition";
                                    else editor->focus_when_mounted();
                                    refresh(); return;
                                }
                                auto editor = data->editors.at(data->active);
                                if (!editor->select_lsp_range(row.start, row.end, data->languageRevision)) data->languageState = "language.invalidPosition";
                                else if (!editor->request_focus()) data->languageState = "language.focusUnavailable";
                                refresh();
                            });
                    })}.With(Spacing(4), CrossAlign(CrossAxisAlignment::Stretch))}.With(Grow{1}),
                }.Key("language-panel").With(Frame{.height = compact ? 240.0F : 260.0F}, Padding(12), Spacing(8), Background(pane), CrossAlign(CrossAxisAlignment::Stretch));
            }
            View outputPanel;
            if (data->bottomPanel == "output") {
                const auto actionKey = std::string("action.") + data->taskKind;
                outputPanel = Column {
                    Flow {
                        Text(t("task.output") + (data->taskKind.empty() ? "" : " · " + t(actionKey)), TextRole::Title),
                        data->languageOperation.empty() ? View{} : View(Button(t(language_title(data->languageOperation))).Key("panel-language").OnClick([data, refresh] { data->bottomPanel = "language"; refresh(); })),
                        Button(t("task.closeOutput")).Key("output-close").OnClick([data, refresh] { data->bottomPanel.clear(); if (!data->active.empty()) data->editors.at(data->active)->request_focus(); refresh(); }),
                    }.With(Spacing(8)),
                    Text(t(data->taskStateKey), TextRole::Label).Key("task-state").With(Foreground(muted)),
                    data->busy ? View(ProgressBar().Key("task-progress").With(Frame{.height = 4})) : View{},
                    ScrollView {SelectionArea {Text(data->output.empty() ? t(data->busy ? "task.waitingOutput" : "task.noOutput") : data->output).Style({.font = Font::Monospace(12)}).Key("task-output")}}.With(Grow{1}),
                }.Key("output-panel").With(Frame{.height = compact ? 220.0F : 240.0F}, Padding(12), Spacing(8), Background(pane), CrossAlign(CrossAxisAlignment::Stretch));
            }
            auto center = Column {toolbar, tabs,
                !data->active.empty() && data->editors.at(data->active)->text_format().loneCarriageReturns ? View(Text(t("editor.unsupportedLineEndings"), TextRole::Label).Key("format-read-only").With(Padding(8))) : View{},
                !data->active.empty() && data->editors.at(data->active)->highlighting_limited() ? View(Text(t("editor.highlightLimited"), TextRole::Label).Key("highlight-limited").With(Padding(8))) : View{},
                std::move(editor),
                std::move(languagePanel),
                std::move(outputPanel),
            }.Key("workspace-editor-pane").With(Grow{1}, CrossAlign(CrossAxisAlignment::Stretch));
            body = Row {data->navigation ? View(sidebar) : View{}, (!compact || !data->navigation) ? View(center) : View{}}.With(Grow{1}, CrossAlign(CrossAxisAlignment::Stretch));
        }
        if (data->settingsOpen) {
            body = ScrollView {Column {
                Text(t("settings.title"), TextRole::Title).With(FontSize(24)),
                Text(t("settings.appearance"), TextRole::Title),
                Text(data->dark ? t("settings.themeDark") : t("settings.themeLight")),
                Button(data->dark ? t("settings.useLight") : t("settings.useDark")).OnClick(toggleTheme),
                Text(data->reducedMotion ? t("settings.motionReduced") : t("settings.motionStandard")),
                Button(data->reducedMotion ? t("settings.allowMotion") : t("settings.reduceMotion")).Key("motion-setting").OnClick([data, updateSettings] { updateSettings({{"reducedMotion", !data->reducedMotion}}); }),
                Text(t("settings.language"), TextRole::Title),
                Text(data->messages.text("settings.languageCurrent", {{"language", t(data->messages.language() == "zh" ? "settings.languageZh" : "settings.languageEn")}})).Key("current-language"),
                Button(t("settings.languageAuto")).Key("language-auto").With(Enabled(data->languagePreference != "auto")).OnClick([updateSettings] { updateSettings({{"language", "auto"}}); }),
                Button(t("settings.languageEn")).Key("language-en").With(Enabled(data->languagePreference != "en")).OnClick([updateSettings] { updateSettings({{"language", "en"}}); }),
                Button(t("settings.languageZh")).Key("language-zh").With(Enabled(data->languagePreference != "zh")).OnClick([updateSettings] { updateSettings({{"language", "zh"}}); }),
                Text(t("settings.local"), TextRole::Label).With(Foreground(muted)),
                Button(t("action.back")).Key("settings-back").OnClick([data, refresh] { data->settingsOpen = false; refresh(); }),
            }.With(Padding(compact ? 20 : 32), Spacing(16), CrossAlign(CrossAxisAlignment::Start))}.Key("settings-panel").With(Grow{1})
                .On<ViewEvents::BackRequested>([data, refresh] { data->settingsOpen = false; refresh(); });
        }
        if (data->aboutOpen && data->aboutInfo.is_object()) {
            const auto& info = data->aboutInfo;
            const auto& product = info.at("product");
            auto closeAbout = [data, refresh] { data->aboutOpen = false; data->licenseId.clear(); refresh(); };
            auto components = info.at("libraries").get<std::vector<Json>>();
            for (const auto& tool : info.at("tools")) components.push_back(tool);
            std::string licenseText;
            if (data->licenseId == "meditor") licenseText = product.at("licenseText").get<std::string>();
            else for (const auto& item : components) if (item.at("id") == data->licenseId && item.at("licenseText").is_string()) licenseText = item.at("licenseText").get<std::string>();
            if (!data->licenseId.empty()) {
                body = Column {
                    Row {Text(data->licenseId, TextRole::Title).With(Grow{1}), Button(t("about.back")).Key("license-back").OnClick([data, refresh] { data->licenseId.clear(); refresh(); })}.With(Spacing(12)),
                    ScrollView {SelectionArea {Text(licenseText).Style({.font = Font::Monospace(12)}).Key("license-text")}}.With(Grow{1}),
                }.Key("license-panel").With(Grow{1}, Padding(compact ? 20 : 32), Spacing(16), CrossAlign(CrossAxisAlignment::Stretch))
                    .On<ViewEvents::BackRequested>([data, refresh] { data->licenseId.clear(); refresh(); });
            } else {
                body = ScrollView {Stack {Column {
                    Text(t("about.title"), TextRole::Title).With(FontSize(24)),
                    Text(product.at("name").get<std::string>() + " " + product.at("version").get<std::string>()).Key("about-version"),
                    Text(t(product.at("descriptionId").get<std::string>())),
                    Text(t("about.contributors") + ": " + product.at("contributors").get<std::string>()),
                    SelectionArea {Text(t("about.contact") + ": " + product.at("contact").get<std::string>())},
                    Row {Button(product.at("license").get<std::string>()).Key("license-meditor").OnClick([data, refresh] { data->licenseId = "meditor"; refresh(); })},
                    Text(t("about.source"), TextRole::Title),
                    SelectionArea {Text(product.at("sourceUrl").is_string() ? product.at("sourceUrl").get<std::string>() : product.at("repository").get<std::string>()).With(FontSize(12))},
                    product.at("sourceCommit").is_null() ? View(Text(t("about.sourcePending"))) : View{},
                    Text(t("about.dependencies"), TextRole::Title),
                    Text(t("about.scope")).Key("about-scope").With(Foreground(muted)),
                    Column {ForEach(components, [data, t, refresh](const Json& item) {
                        const auto id = item.at("id").get<std::string>();
                        const auto version = item.at("version").is_string() ? item.at("version").get<std::string>() : t("about.unknown");
                        const auto complete = item.at("noticeStatus") == "present";
                        const auto role = item.at("role").get<std::string>();
                        const auto roleLabel = role == "gui-framework" ? t("about.role.gui-framework") : role == "bundled-tool" ? t("about.role.bundled-tool") : t("about.role.runtime-library");
                        const auto state = item.value("state", "");
                        const auto stateLabel = state == "missing" ? t("about.state.missing") : state == "recorded" ? t("about.state.recorded") : t("about.state.unverified");
                        return Column {
                            Text(id, TextRole::Title),
                            Text(t("about.version") + ": " + version).With(FontSize(12)),
                            Text(roleLabel),
                            Text(item.at("license").get<std::string>()),
                            item.contains("state") ? View(Text(stateLabel)) : View{},
                            SelectionArea {Text(item.at("repository").get<std::string>()).With(FontSize(12))},
                            complete ? View{} : View(Text(t("about.noticePending"))),
                            Button(t("about.readLicense")).Key("license-" + id).With(Enabled(complete)).OnClick([data, id, refresh] { data->licenseId = id; refresh(); }),
                        }.Key("about-component-" + id).With(Spacing(8));
                    })}.With(Spacing(24)),
                    Row {Button(t("action.back")).Key("about-back").OnClick(closeAbout)},
                }.With(Padding(compact ? 20 : 32), Spacing(16), Frame{.max_width = 800}, CrossAlign(CrossAxisAlignment::Stretch)).Key("about-content")}.With(Align(HorizontalAlignment::Center, VerticalAlignment::Start))}.Key("about-panel").With(Grow{1})
                    .On<ViewEvents::BackRequested>(closeAbout);
            }
        }
        const auto language = data->active.empty() ? std::string("cpp") : data->editors.at(data->active)->language_id();
        const auto languageLabel = language == "cpp" ? std::string("C++23 · Modules") :
            language == "markdown" ? std::string("Markdown") : language == "json" ? std::string("JSON") :
            language == "jsonc" ? std::string("JSONC") : language == "toml" ? std::string("TOML") : t("editor.plainText");
        std::string formatLabel;
        if (!data->active.empty()) {
            const auto format = data->editors.at(data->active)->text_format();
            formatLabel = format.utf8Bom ? " · UTF-8 BOM" : " · UTF-8";
            formatLabel += format.lineEnding == LineEnding::CrLf ? " · CRLF" : " · LF";
            if (format.mixedLineEndings) formatLabel += " · " + t("editor.mixedLineEndings");
        }
        auto page = Column {header, Divider(), body,
            Row {Text(data->status_text(), TextRole::Label).Key("status-text").With(Grow{1}),
                compact ? View{} : View(Text(languageLabel + formatLabel, TextRole::Label).Key("document-language").With(Foreground(muted))),
                data->selected ? View(Button(t("task.output")).Key("output-toggle").With(Enabled(!data->taskKind.empty()), Border{data->busy ? colors.accent : Color::Transparent(), 1}).OnClick([data, refresh] { data->bottomPanel = data->bottomPanel == "output" ? "" : "output"; refresh(); })) : View{},
                Button(t("about.entry")).Key("about-entry").OnClick(showAbout),
                Button(t("settings.title")).Key("settings-entry").OnClick(showSettings),
                !data->active.empty() && data->editors.contains(data->active) && data->editors.at(data->active)->completion_visible()
                    ? View(Spacer().Key("completion-active").With(Frame{.width = 0.0F, .height = 0.0F})) : View{}
            }.Key("status-bar").With(Padding(10), Spacing(8), Background(pane))
        }.With(Background(background), Foreground(ink), CrossAlign(CrossAxisAlignment::Stretch))
            .On<ViewEvents::KeyIntercept>([save, data, refresh, openSearch, queryLanguage](const KeyEvent& event) {
                const auto shortcut = shortcuts::resolve(event, shortcuts::Context::Workbench);
                if (!shortcut) return false;
                if (*shortcut == shortcuts::Command::Dismiss && data->aboutOpen) {
                    if (!data->licenseId.empty()) data->licenseId.clear(); else data->aboutOpen = false;
                    refresh(); return true;
                }
                if (*shortcut == shortcuts::Command::Dismiss && data->settingsOpen) { data->settingsOpen = false; refresh(); return true; }
                if (*shortcut == shortcuts::Command::Dismiss && !data->searchOpen && data->bottomPanel == "language" && !data->languageOperation.empty()) {
                    data->close_language(); if (!data->active.empty()) data->editors.at(data->active)->request_focus(); refresh(); return true;
                }
                if (*shortcut == shortcuts::Command::Dismiss && !data->searchOpen && data->bottomPanel == "output") { data->bottomPanel.clear(); if (!data->active.empty()) data->editors.at(data->active)->request_focus(); refresh(); return true; }
                if (*shortcut == shortcuts::Command::Outline || *shortcut == shortcuts::Command::Problems) { queryLanguage(*shortcut == shortcuts::Command::Outline ? "symbols" : "diagnostics"); return true; }
                if (*shortcut == shortcuts::Command::Definition || *shortcut == shortcuts::Command::References) { queryLanguage(*shortcut == shortcuts::Command::References ? "references" : "definition"); return true; }
                if (*shortcut == shortcuts::Command::Find) { openSearch(); return true; }
                if (*shortcut == shortcuts::Command::Save) { save(); return true; } return false;
            });
        page = std::move(page).With(Dialog{
            .visible = data->searchOpen,
            .content = [data, findValue, replaceValue, t, findMatch, replaceMatches, closeSearch, compact, pane, ink]() -> View {
                const bool writable = !data->active.empty() && !data->editors.at(data->active)->text_format().loneCarriageReturns;
                return Column {
                    Text(t("search.title"), TextRole::Title),
                    TextField(findValue).Key("find-query").Label(t("search.query")).OnChanged([data, findValue, findMatch](const TextEditingValue& value) { findValue = value; data->searchQuery = value.text; data->searchReplaced.reset(); findMatch(0); }).OnSubmitted([findMatch] { findMatch(1); }),
                    TextField(replaceValue).Key("find-replacement").Label(t("search.replacement")).OnChanged([data, replaceValue](const TextEditingValue& value) { replaceValue = value; data->searchReplacement = value.text; }),
                    Text(data->searchQuery.empty() ? t("search.empty") : data->messages.text("search.matches", {{"count", std::to_string(data->searchTotal)}})).Key("find-count"),
                    data->searchReplaced ? View(Text(data->messages.text("search.replaced", {{"count", std::to_string(*data->searchReplaced)}})).Key("find-feedback")) : View{},
                    data->searchErrorKey.empty() ? View{} : View(Text(t(data->searchErrorKey)).Key("find-error")),
                    data->searchWrapped ? View(Text(t("search.wrapped")).Key("find-wrapped")) : View{},
                    Flow {
                        Button(t("search.previous")).Key("find-previous").With(Enabled(data->searchTotal > 0)).OnClick([findMatch] { findMatch(-1); }),
                        Button(t("search.next")).Key("find-next").With(Enabled(data->searchTotal > 0)).OnClick([findMatch] { findMatch(1); }),
                        Button(t("search.replace")).Key("find-replace").With(Enabled(writable && data->searchTotal > 0)).OnClick([replaceMatches] { replaceMatches(false); }),
                        Button(t("search.replaceAll")).Key("find-replace-all").With(Enabled(writable && data->searchTotal > 0)).OnClick([replaceMatches] { replaceMatches(true); }),
                        Button(t("search.close")).Key("find-close").OnClick(closeSearch),
                    }.With(Spacing(8)),
                }.Key("find-panel").With(Frame{.width = compact ? 280.0F : 520.0F}, Padding(16), Background(pane), Foreground(ink), CornerRadius(12), Spacing(12), CrossAlign(CrossAxisAlignment::Stretch));
            },
            .dismiss_on_cancel = true,
            .on_dismiss_request = closeSearch,
        });
        return Theme{editor_theme(data->dark, compact, data->reducedMotion || UseTheme().motion.reduced_motion), std::move(page)};
    });
}
}

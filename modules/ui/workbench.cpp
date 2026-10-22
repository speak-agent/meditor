// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.ui.workbench;
import std;
import huxerui;
import meditor.application;
import meditor.document;
import meditor.platform;
import meditor.ui.editor;

namespace meditor::ui {
using namespace huxerui;
namespace {
std::filesystem::path initialWorkspace;
std::filesystem::path initialTools;
std::filesystem::path initialSettings;
meditor::RuntimeOptions initialRuntime;
std::string initialDocument;
struct WorkspaceModel {
    std::shared_ptr<meditor::Application> application;
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
    std::string activeTask;
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
        application = std::move(next); editors.clear(); tabs.clear(); active.clear(); selected = true;
        busy = false; activeTask.clear(); output.clear(); searchOpen = false;
        searchTotal = 0; searchReplaced.reset(); searchErrorKey.clear();
        notice("status.workspaceOpened"); return true;
    }
};
}
void configure(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view argument = argv[i];
        if (argument == "--workspace" && i + 1 < argc) initialWorkspace = argv[++i];
        else if (argument == "--tool-root" && i + 1 < argc) initialTools = argv[++i];
        else if (argument == "--settings-file" && i + 1 < argc) initialSettings = argv[++i];
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
        auto open = [data, refresh, run](std::string name) {
            auto result = run("document.open", {{"path", name}});
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["document"].get<std::string>();
            if (!data->editors.contains(id)) { data->editors[id] = std::make_shared<EditorState>(data->application, id, refresh); data->tabs.push_back(id); }
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
            if (result.value("ok", false)) data->notice("status.saved");
            refresh();
        };
        auto executeTask = [data, tasks, run, refresh](std::string command) {
            if (data->busy) return;
            auto result = run(command);
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["task"].get<std::string>(); data->activeTask = id;
            auto application = data->application; auto generation = data->generation; data->busy = true; data->notice("status.taskRunning"); refresh();
            tasks.Launch([=]() -> Task<void> {
                while (true) {
                    co_await Delay(std::chrono::milliseconds(100));
                    if (data->application != application || data->generation != generation) co_return;
                    auto result = application->execute({{"command", "task.status"}, {"workspaceGeneration", generation}, {"params", {{"task", id}}}});
                    if (!result.value("ok", false)) { data->report_error(result["error"]); break; }
                    data->output = result["data"].value("output", "");
                    if (result["data"].value("state", "") != "running") { auto key = "status.task." + result["data"].value("state", ""); data->notice(data->messages.contains(key) ? key : "status.taskFinished"); break; }
                }
                data->busy = false; data->activeTask.clear(); refresh();
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
        auto pane = data->dark ? Color::Rgb(23, 29, 40) : Color::Rgb(243, 246, 251);
        auto background = data->dark ? Color::Rgb(15, 20, 29) : Color::Rgb(250, 252, 255);
        auto ink = data->dark ? Color::Rgb(215, 225, 240) : Color::Rgb(35, 47, 65);
        auto muted = data->dark ? Color::Rgb(142, 158, 182) : Color::Rgb(96, 110, 132);
        auto header = WindowTitleBar {
            Text("m", TextRole::Title).With(Padding(8), Background(Color::Rgb(72, 107, 239)), Foreground(Color::White()), CornerRadius(9)),
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
                Row {Button(t("workspace.open")).Key("workspace-open").With(Enabled(!data->choosing)).OnClick(choose)}.With(Spacing(10)),
                Text(t("welcome.description")).With(Foreground(muted)),
                Text(t("welcome.privacy"), TextRole::Label).With(Foreground(muted)),
            }.With(Grow{1}, Padding(40), Spacing(22), MainAlign(MainAxisAlignment::Center), CrossAlign(CrossAxisAlignment::Start));
        } else {
            auto sidebar = Column {
                Text(t("workspace.heading"), TextRole::Label).With(Foreground(muted)),
                Text(data->name, TextRole::Title),
                TextField(path).Label(t("workspace.openFile")).Placeholder("src/main.cpp").OnChanged([path](const TextEditingValue& value) { path = value; }).OnSubmitted([path, open] { open(path->text); }),
                ScrollView {
                    Column {ForEach(data->files, [open](const std::string& name) {
                        return Button(name).Key(name).OnClick([name, open] { open(name); });
                    })}.With(Spacing(2), CrossAlign(CrossAxisAlignment::Stretch))
                }.With(Grow{1}),
                Button(t("workspace.change")).Key("workspace-change").With(Enabled(!data->choosing)).OnClick(choose),
            }.With(Padding(16), Spacing(12), Background(pane), Frame{.width = compact ? 280.0F : 260.0F}, CrossAlign(CrossAxisAlignment::Stretch));
            View editor = Column {
                Text(t("workspace.chooseFile"), TextRole::Title),
                Text(t("workspace.ready")).With(Foreground(muted)),
            }.With(Grow{1}, Padding(32), Spacing(16), MainAlign(MainAxisAlignment::Center));
            if (!data->active.empty()) editor = code_editor(data->editors.at(data->active), data->dark);
            auto tabs = Flow {ForEach(data->tabs, [data, run, refresh, pane, t](const std::string& id) {
                auto record = data->application->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"];
                auto name = std::filesystem::path(record.value("path", t("workspace.untitled"))).filename().string();
                if (record.value("dirty", false)) name += " •";
                return Button(name).Key(id).With(Background(data->active == id ? Color::Rgb(54, 69, 98) : pane)).OnClick([data, id, refresh] { data->active = id; refresh(); });
            })}.With(Padding(8), Spacing(6), Background(pane));
            auto toolbar = Flow {
                Button(t("action.save")).Key("action-save").With(Enabled(!data->active.empty() && !data->editors.at(data->active)->text_format().loneCarriageReturns)).OnClick(save),
                Button(t("action.find")).Key("action-find").With(Enabled(!data->active.empty())).OnClick(openSearch),
                Button(t("action.undo")).Key("action-undo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->undo(); }),
                Button(t("action.redo")).Key("action-redo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->redo(); }),
                Button(t("action.build")).Key("action-build").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("build"); }),
                Button(t("action.run")).Key("action-run").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("run"); }),
                Button(t("action.test")).Key("action-test").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("test"); }),
                Button(t("action.cancelTask")).Key("action-cancel-task").With(Enabled(data->busy)).OnClick([data, run] { if (!data->activeTask.empty()) run("task.cancel", {{"task", data->activeTask}}); }),
                Button(t("action.trust")).Key("action-trust").OnClick([run] { run("workspace.trust", {{"allow", true}}); }),
            }.With(Padding(8), Spacing(6));
            auto center = Column {toolbar, tabs,
                !data->active.empty() && data->editors.at(data->active)->text_format().loneCarriageReturns ? View(Text(t("editor.unsupportedLineEndings"), TextRole::Label).Key("format-read-only").With(Padding(8))) : View{},
                !data->active.empty() && data->editors.at(data->active)->highlighting_limited() ? View(Text(t("editor.highlightLimited"), TextRole::Label).Key("highlight-limited").With(Padding(8))) : View{},
                std::move(editor),
                data->output.empty() ? View{} : View(ScrollView {SelectionArea {Text(data->output).Style({.font = Font::Monospace(12)})}}.With(Frame{.height = 160}, Padding(12), Background(pane)))
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
                Button(t("about.entry")).Key("about-entry").OnClick(showAbout),
                Button(t("settings.title")).Key("settings-entry").OnClick(showSettings)
            }.Key("status-bar").With(Padding(10), Spacing(8), Background(pane))
        }.With(Background(background), Foreground(ink), CrossAlign(CrossAxisAlignment::Stretch))
            .On<ViewEvents::KeyIntercept>([save, data, refresh, openSearch](const KeyEvent& event) {
                if (event.type == KeyEventType::Down && event.key == Key::Escape && data->aboutOpen) {
                    if (!data->licenseId.empty()) data->licenseId.clear(); else data->aboutOpen = false;
                    refresh(); return true;
                }
                if (event.type == KeyEventType::Down && event.key == Key::Escape && data->settingsOpen) { data->settingsOpen = false; refresh(); return true; }
                if (event.type == KeyEventType::Down && (event.modifiers.control || event.modifiers.meta) && event.key == Key::F) { openSearch(); return true; }
                if (event.type == KeyEventType::Down && (event.modifiers.control || event.modifiers.meta) && event.key == Key::S) { save(); return true; } return false;
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
        auto theme = data->dark ? FlatDarkThemeSpec() : FlatLightThemeSpec();
        theme.motion.reduced_motion = data->reducedMotion || UseTheme().motion.reduced_motion;
        return FlatTheme(theme, std::move(page));
    });
}
}

module meditor.ui.workbench;
import std;
import huxerui;
import meditor.application;
import meditor.ui.editor;

namespace meditor::ui {
using namespace huxerui;
namespace {
std::filesystem::path initialWorkspace;
std::filesystem::path initialTools;
std::filesystem::path initialSettings;
std::string initialDocument;
struct WorkspaceModel {
    std::shared_ptr<meditor::Application> application;
    std::shared_ptr<meditor::Application> preferences;
    std::filesystem::path settingsFile;
    std::vector<std::string> files;
    std::map<std::string, std::shared_ptr<EditorState>> editors;
    std::vector<std::string> tabs;
    std::string active;
    std::string name;
    std::string status {"Ready to create something"};
    std::string output;
    bool selected {};
    bool busy {};
    bool dark {true};
    bool reducedMotion {};
    bool settingsOpen {};
    std::uint64_t settingsRevision {};
    bool navigation {true};
    std::optional<FileReference> grant;
    void adopt_settings(const Json& state) {
        settingsRevision = state.at("revision").get<std::uint64_t>();
        dark = state.at("values").at("theme") == "dark";
        reducedMotion = state.at("values").at("reducedMotion").get<bool>();
    }
    void load(std::filesystem::path root) {
        auto next = std::make_shared<meditor::Application>(root, initialTools, settingsFile);
        auto response = next->execute({{"command", "workspace.files"}});
        files.clear(); if (response.value("ok", false)) for (const auto& file : response["data"]["files"]) files.push_back(file.get<std::string>());
        name = root.filename().string(); application = std::move(next); editors.clear(); tabs.clear(); active.clear(); selected = true;
        status = "Workspace opened · execution requires trust";
    }
};
}
void configure(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view argument = argv[i];
        if (argument == "--workspace" && i + 1 < argc) initialWorkspace = argv[++i];
        else if (argument == "--tool-root" && i + 1 < argc) initialTools = argv[++i];
        else if (argument == "--settings-file" && i + 1 < argc) initialSettings = argv[++i];
        else if (argument == "--open" && i + 1 < argc) initialDocument = argv[++i];
    }
}
View workbench() {
    return Scope([]() -> View {
        auto model = UseState(std::make_shared<WorkspaceModel>());
        auto epoch = UseState(0);
        auto path = UseState(TextEditingValue::FromText(""));
        auto tasks = UseTaskScope();
        auto picker = UseService<FilePicker>();
        auto window = UseWindow();
        auto host = UseApplication();
        auto transition = UseSceneTransition();
        auto compact = UseViewportClass() == ViewportClass::Compact;
        auto refresh = [epoch] { epoch += 1; };
        auto data = model.Get(); (void)epoch.Get();
        window.OnCloseRequest([data, refresh] {
            if (!data->application) return false;
            auto state = data->application->execute({{"command", "workspace.status"}});
            for (const auto& document : state["data"]["documents"]) {
                if (document.value("dirty", false)) {
                    data->status = "Save modified documents before closing";
                    refresh(); return true;
                }
            }
            return false;
        });
        auto run = [data, refresh](std::string command, Json params = Json::object()) {
            if (!data->application) { data->status = "Open a workspace first"; refresh(); return Json(); }
            auto result = data->application->execute({{"command", command}, {"params", std::move(params)}});
            if (!result.value("ok", false)) data->status = result["error"].value("message", "Operation failed");
            refresh(); return result;
        };
        auto open = [data, refresh, run](std::string name) {
            auto result = run("document.open", {{"path", name}});
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["document"].get<std::string>();
            if (!data->editors.contains(id)) { data->editors[id] = std::make_shared<EditorState>(data->application, id, refresh); data->tabs.push_back(id); }
            data->active = id; data->status = name; refresh();
        };
        Lifecycle([data, refresh, open, host] {
            try {
                data->settingsFile = initialSettings;
#if defined(__ANDROID__)
                if (data->settingsFile.empty()) data->settingsFile = std::filesystem::u8path(host.Directories().data_directory.Child("settings.json").Path());
#endif
                data->preferences = std::make_shared<meditor::Application>(std::filesystem::current_path(), initialTools, data->settingsFile);
                auto settings = data->preferences->execute({{"command", "settings.get"}});
                if (settings.value("ok", false)) {
                    data->adopt_settings(settings["data"]);
                } else data->status = settings["error"].value("message", "Settings unavailable");
            } catch (...) { data->status = "Settings unavailable"; }
            if (!initialWorkspace.empty()) {
                try {
                    data->load(initialWorkspace);
                    if (!initialDocument.empty()) open(initialDocument);
                } catch (...) { data->status = "Could not open the workspace"; }
                refresh();
            }
            refresh();
        });
        auto updateSettings = [data, refresh](Json changes) {
            if (!data->preferences) { data->status = "Settings unavailable"; refresh(); return; }
            auto result = data->preferences->execute({{"command", "settings.update"}, {"params", {{"revision", data->settingsRevision}, {"changes", std::move(changes)}}}});
            if (!result.value("ok", false)) {
                data->status = result["error"].value("message", "Could not save settings");
                if (result["error"].value("code", "") == "settings-conflict")
                    result = data->preferences->execute({{"command", "settings.get"}});
            }
            if (result.value("ok", false)) {
                data->adopt_settings(result["data"]);
            }
            refresh();
        };
        auto showSettings = [data, refresh] {
            data->settingsOpen = !data->settingsOpen;
            if (data->settingsOpen && data->preferences) {
                auto result = data->preferences->execute({{"command", "settings.get"}});
                if (result.value("ok", false)) data->adopt_settings(result["data"]);
                else data->status = result["error"].value("message", "Could not load settings");
            }
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
            auto result = run("document.save", {{"document", data->active}, {"revision", current["data"]["revision"]}});
            if (result.value("ok", false)) data->status = "Saved";
            refresh();
        };
        auto executeTask = [data, tasks, run, refresh](std::string command) {
            if (data->busy) return;
            auto result = run(command);
            if (!result.is_object() || !result.value("ok", false)) return;
            auto id = result["data"]["task"].get<std::string>(); data->busy = true; data->status = "Task running"; refresh();
            tasks.Launch([=]() -> Task<void> {
                while (true) {
                    co_await Delay(std::chrono::milliseconds(100));
                    auto result = run("task.status", {{"task", id}});
                    if (!result.value("ok", false)) break;
                    data->output = result["data"].value("output", "");
                    if (result["data"].value("state", "") != "running") { data->status = result["data"].value("state", "Task finished"); break; }
                }
                data->busy = false; refresh();
            });
        };
        auto choose = [data, tasks, picker, refresh] {
            if (data->application) {
                auto state = data->application->execute({{"command", "workspace.status"}});
                for (const auto& doc : state["data"]["documents"]) if (doc.value("dirty", false)) { data->status = "Save modified documents before switching workspaces"; refresh(); return; }
            }
            tasks.Launch([=]() -> Task<void> {
                auto selected = co_await picker->OpenDirectoryAsync(true);
                if (!selected) co_return;
                auto local = selected->AsFile();
                if (!local) { data->status = "This location requires a provider workspace adapter"; refresh(); co_return; }
                try { data->load(local->Path()); data->grant = *selected; } catch (...) { data->status = "Could not open the selected workspace"; }
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
            compact ? View(Spacer().With(Grow{1})) : View(Text(data->selected ? data->name : "C++ Modules, naturally.", TextRole::Label).With(Grow{1}, Foreground(muted))),
            Button(compact ? "Files" : "Explorer").OnClick([data, refresh] { data->navigation = !data->navigation; refresh(); }),
            Button(data->dark ? "Light" : "Dark").OnClick(toggleTheme),
        }.Key("window-title-bar").With(Padding(12), Spacing(12), CrossAlign(CrossAxisAlignment::Center));
        View body;
        if (!data->selected) {
            body = Column {
                Text("A clear space for modern C++", TextRole::Title).With(FontSize(28)),
                Text("Native Modules. One workspace. Your tools, together.").With(Foreground(muted)),
                Row {Button("Open workspace").OnClick(choose)}.With(Spacing(10)),
                Text("Open an mcpp project to edit modules, explore imports and build with confidence.").With(Foreground(muted)),
                Text("Private by default · no telemetry · local tools", TextRole::Label).With(Foreground(muted)),
            }.With(Grow{1}, Padding(40), Spacing(22), MainAlign(MainAxisAlignment::Center), CrossAlign(CrossAxisAlignment::Start));
        } else {
            auto sidebar = Column {
                Text("WORKSPACE", TextRole::Label).With(Foreground(muted)),
                Text(data->name, TextRole::Title),
                TextField(path).Label("Open file").Placeholder("src/main.cpp").OnChanged([path](const TextEditingValue& value) { path = value; }).OnSubmitted([path, open] { open(path->text); }),
                ScrollView {
                    Column {ForEach(data->files, [open](const std::string& name) {
                        return Button(name).Key(name).OnClick([name, open] { open(name); });
                    })}.With(Spacing(2), CrossAlign(CrossAxisAlignment::Stretch))
                }.With(Grow{1}),
                Button("Change workspace").OnClick(choose),
            }.With(Padding(16), Spacing(12), Background(pane), Frame{.width = compact ? 280.0F : 260.0F}, CrossAlign(CrossAxisAlignment::Stretch));
            View editor = Column {
                Text("Choose a source file", TextRole::Title),
                Text("Your workspace is ready. Open a module from the explorer.").With(Foreground(muted)),
            }.With(Grow{1}, Padding(32), Spacing(16), MainAlign(MainAxisAlignment::Center));
            if (!data->active.empty()) editor = code_editor(data->editors.at(data->active));
            auto tabs = Flow {ForEach(data->tabs, [data, run, refresh, pane](const std::string& id) {
                auto record = data->application->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"];
                auto name = std::filesystem::path(record.value("path", "Untitled")).filename().string();
                if (record.value("dirty", false)) name += " •";
                return Button(name).Key(id).With(Background(data->active == id ? Color::Rgb(54, 69, 98) : pane)).OnClick([data, id, refresh] { data->active = id; refresh(); });
            })}.With(Padding(8), Spacing(6), Background(pane));
            auto toolbar = Flow {
                Button("Save").OnClick(save),
                Button("Undo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->undo(); }),
                Button("Redo").OnClick([data] { if (!data->active.empty()) data->editors.at(data->active)->redo(); }),
                Button("Build").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("build"); }),
                Button("Run").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("run"); }),
                Button("Test").With(Enabled(!data->busy)).OnClick([executeTask] { executeTask("test"); }),
                Button("Trust workspace").OnClick([run] { run("workspace.trust", {{"allow", true}}); }),
            }.With(Padding(8), Spacing(6));
            auto center = Column {toolbar, tabs, std::move(editor),
                data->output.empty() ? View{} : View(ScrollView {SelectionArea {Text(data->output).Style({.font = Font::Monospace(12)})}}.With(Frame{.height = 160}, Padding(12), Background(pane)))
            }.With(Grow{1}, CrossAlign(CrossAxisAlignment::Stretch));
            body = Row {data->navigation ? View(sidebar) : View{}, (!compact || !data->navigation) ? View(center) : View{}}.With(Grow{1}, CrossAlign(CrossAxisAlignment::Stretch));
        }
        if (data->settingsOpen) {
            body = ScrollView {Column {
                Text("Settings", TextRole::Title).With(FontSize(24)),
                Text("Appearance", TextRole::Title),
                Text(data->dark ? "Theme: Dark" : "Theme: Light"),
                Button(data->dark ? "Use light theme" : "Use dark theme").OnClick(toggleTheme),
                Text(data->reducedMotion ? "Motion: Reduced" : "Motion: Standard"),
                Button(data->reducedMotion ? "Allow motion" : "Reduce motion").Key("motion-setting").OnClick([data, updateSettings] { updateSettings({{"reducedMotion", !data->reducedMotion}}); }),
                Text("Local preferences · meditor settings commands", TextRole::Label).With(Foreground(muted)),
                Button("Back").OnClick([data, refresh] { data->settingsOpen = false; refresh(); }),
            }.With(Padding(compact ? 20 : 32), Spacing(16), CrossAlign(CrossAxisAlignment::Start))}.Key("settings-panel").With(Grow{1});
        }
        auto page = Column {header, Divider(), body,
            Row {Text(data->status, TextRole::Label).With(Grow{1}),
                compact ? View{} : View(Text("C++23 · Modules", TextRole::Label).With(Foreground(muted))),
                Button("Settings").Key("settings-entry").OnClick(showSettings)
            }.Key("status-bar").With(Padding(10), Spacing(8), Background(pane))
        }.With(Background(background), Foreground(ink), CrossAlign(CrossAxisAlignment::Stretch))
            .On<ViewEvents::KeyIntercept>([save, data, refresh](const KeyEvent& event) {
                if (event.type == KeyEventType::Down && event.key == Key::Escape && data->settingsOpen) { data->settingsOpen = false; refresh(); return true; }
                if (event.type == KeyEventType::Down && (event.modifiers.control || event.modifiers.meta) && event.key == Key::S) { save(); return true; } return false;
            });
        auto theme = data->dark ? FlatDarkThemeSpec() : FlatLightThemeSpec();
        theme.motion.reduced_motion = data->reducedMotion || UseTheme().motion.reduced_motion;
        return FlatTheme(theme, std::move(page));
    });
}
}

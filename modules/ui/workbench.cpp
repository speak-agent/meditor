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
std::string initialDocument;
struct WorkspaceModel {
    std::shared_ptr<meditor::Application> application;
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
    bool navigation {true};
    std::optional<FileReference> grant;
    void load(std::filesystem::path root) {
        auto next = std::make_shared<meditor::Application>(root, initialTools);
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
        Lifecycle([data, refresh, open] {
            if (!initialWorkspace.empty()) {
                try {
                    data->load(initialWorkspace);
                    if (!initialDocument.empty()) open(initialDocument);
                } catch (...) { data->status = "Could not open the workspace"; }
                refresh();
            }
        });
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
            Button(data->dark ? "Light" : "Dark").OnClick([data, refresh, transition] {
                transition.Run(TransitionSpec{FadeTransition{}, TweenSpec{0.12}}, [data, refresh] { data->dark = !data->dark; refresh(); });
            }),
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
        auto page = Column {header, Divider(), body,
            Row {Text(data->status, TextRole::Label).With(Grow{1}), Text("C++23 · Modules", TextRole::Label).With(Foreground(muted))}.Key("status-bar").With(Padding(10), Background(pane))
        }.With(Background(background), Foreground(ink), CrossAlign(CrossAxisAlignment::Stretch))
            .On<ViewEvents::KeyIntercept>([save](const KeyEvent& event) {
                if (event.type == KeyEventType::Down && (event.modifiers.control || event.modifiers.meta) && event.key == Key::S) { save(); return true; } return false;
            });
        auto theme = data->dark ? FlatDarkThemeSpec() : FlatLightThemeSpec();
        theme.motion.reduced_motion = UseTheme().motion.reduced_motion;
        return FlatTheme(theme, std::move(page));
    });
}
}

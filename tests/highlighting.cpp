// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import huxerui.testing;
import meditor.application;
import meditor.ui.editor;
import meditor.ui.workbench;

namespace {
std::shared_ptr<meditor::ui::EditorState> gEditor;
bool gDark {true};
huxerui::View editor_view() { return meditor::ui::code_editor(gEditor, gDark); }
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Highlight rendering contract failed at line " + std::to_string(at.line()));
}
}
int main() {
    using namespace huxerui;
    using namespace huxerui::testing;
    auto directory = std::filesystem::temp_directory_path() / ("meditor-highlight-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {directory};
    auto core = std::make_shared<meditor::Application>(directory);
    auto opened = core->execute({{"command", "document.open"}, {"params", {{"path", "test.cppm"}, {"create", true}}}});
    auto id = opened["data"]["document"].get<std::string>();
    auto state = std::make_shared<meditor::ui::EditorState>(core, id, [] {});
    const std::string source = "export module 世界; // 🙂\nexport int n = 42;";
    state->insert(source);
    std::string darkStyle;
    for (bool dark : {true, false}) for (auto viewport : {Size{390, 844}, Size{1200, 800}}) {
        gEditor = state; gDark = dark;
        huxerui::Application application(editor_view, {.show_debug_overlay = false});
        UiTest ui(application, {.viewport = viewport});
        auto snapshot = ui.CaptureSnapshot().ToString();
        auto begin = snapshot.find("styles {"); require(begin != snapshot.npos);
        begin = snapshot.find("style {", begin); require(begin != snapshot.npos);
        auto end = snapshot.find("style {", begin + 7);
        auto firstStyle = snapshot.substr(begin, end - begin);
        require(firstStyle.find("start=0") != firstStyle.npos && firstStyle.find("end=6") != firstStyle.npos);
        require(firstStyle.find("foreground {") != firstStyle.npos);
        if (dark) darkStyle = firstStyle; else require(darkStyle != firstStyle);
        ui.Find(UiSelector::Key("code-editor")).Tap();
        ui.PressKey(Key::A, {.control = true});
        auto session = ui.ActiveTextInput(); require(session.has_value());
        require(session->selection.Range() == TextRange{0, 42});
        // Attributes must not alter source text or turn selection into a text edit.
        require(core->execute({{"command", "document.read"}, {"params", {{"document", id}}}})["data"]["text"] == source);
    }
    struct Sample { std::string path, text, role; int start, end; };
    for (const auto& sample : std::vector<Sample>{
        {"README.md", "# 标题 🙂\n```cpp\nexport module example;\n```", "heading", 0, 7},
        {"settings.json", R"({"键": "🙂", "active": true})", "key", 1, 4},
        {"mcpp.toml", "title = \"你好🙂\" # comment", "key", 0, 5},
    }) {
        auto reply = core->execute({{"command", "document.open"}, {"params", {{"path", sample.path}, {"create", true}}}});
        auto sampleId = reply["data"]["document"].get<std::string>();
        gEditor = std::make_shared<meditor::ui::EditorState>(core, sampleId, [] {}); gEditor->insert(sample.text);
        const auto highlighted = core->execute({{"command", "document.highlight"}, {"params", {{"document", sampleId}}}});
        require(highlighted["data"]["tokens"][0]["role"] == sample.role);
        require(gEditor->language_id() == highlighted["data"]["language"].get<std::string>());
        for (bool dark : {true, false}) for (auto viewport : {Size{390, 844}, Size{1200, 800}}) {
            gDark = dark; huxerui::Application application(editor_view, {.show_debug_overlay = false});
            UiTest ui(application, {.viewport = viewport});
            const auto snapshot = ui.CaptureSnapshot().ToString(); auto begin = snapshot.find("styles {");
            require(begin != snapshot.npos); begin = snapshot.find("style {", begin);
            auto firstStyle = snapshot.substr(begin, snapshot.find("style {", begin + 7) - begin);
            require(firstStyle.find("start=" + std::to_string(sample.start)) != firstStyle.npos);
            require(firstStyle.find("end=" + std::to_string(sample.end)) != firstStyle.npos);
            ui.Find(UiSelector::Key("code-editor")).Tap(); ui.PressKey(Key::A, {.control = true});
            ui.PressKey(Key::Backspace); require(ui.ActiveTextInput().has_value());
            gEditor->undo(); ui.Pump();
            require(core->execute({{"command", "document.read"}, {"params", {{"document", sampleId}}}})["data"]["text"] == sample.text);
        }
    }
    // Long-line fallback state is available to the workbench without mutating text.
    core->execute({{"command", "document.edit"}, {"params", {{"document", id}, {"revision", 2}, {"edits", meditor::Json::array({{{"start", 0}, {"end", source.size()}, {"text", std::string(16385, 'x')}}})}}}});
    state->refresh(); require(state->highlighting_limited());
    gEditor.reset();
    { std::ofstream file(directory / "test.cppm", std::ios::binary); file << std::string(16385, 'x'); }
    std::vector<std::string> arguments {"meditor", "--workspace", directory.string(), "--open", "test.cppm", "--data-root", (directory / "runtime").string()};
    std::vector<char*> pointers; for (auto& argument : arguments) pointers.push_back(argument.data());
    meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
    huxerui::Application workbench(meditor::ui::workbench, {.show_debug_overlay = false});
    UiTest ui(workbench, {.viewport = {390, 844}}); ui.Pump();
    ui.Find(UiSelector::Key("explorer-toggle")).Tap();
    require(ui.Find(UiSelector::Key("highlight-limited")).One().in_viewport);
    require(ui.Find(UiSelector::Key("code-editor")).One().in_viewport);
    std::println("Attributed syntax paint, dark/light palettes and Unicode selection at two sizes: passed");
}

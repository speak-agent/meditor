// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.application;
import meditor.platform;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Recovery flow failed at line " + std::to_string(location.line())); }
using meditor::Json;
Json run(meditor::Application& app, const std::string& command, Json params = Json::object()) {
    auto reply = app.execute({{"command", command}, {"params", std::move(params)}});
    if (!reply.value("ok", false)) throw std::runtime_error(command + " failed: " + reply.dump());
    return reply["data"];
}
}
int main() {
    const auto temporary = std::filesystem::temp_directory_path() / ("meditor-recovery-flow-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(temporary / "workspace");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {temporary};
    const auto workspace = temporary / "workspace", file = workspace / "main.cppm";
    const meditor::RuntimeOptions options {.dataRoot = temporary / "data"};
    meditor::write_file_atomic(file, "export module main;\n");

    { // A process edits and dies without saving.
        meditor::Application crashed(workspace, {}, {}, options);
        auto document = run(crashed, "document.open", {{"path", "main.cppm"}})["document"].get<std::string>();
        run(crashed, "document.edit", {{"document", document}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "// recovered edit\n"}}})}});
    }
    { // The next process restores the unsaved buffer.
        meditor::Application restored(workspace, {}, {}, options);
        auto status = run(restored, "workspace.status");
        auto recovered = status["recovery"]["recovered"];
        require(recovered.size() == 1 && recovered[0] == "main.cppm");
        auto reopened = run(restored, "document.open", {{"path", "main.cppm"}});
        require(reopened["dirty"]);
        require(run(restored, "document.read", {{"document", reopened["document"]}})["text"].get<std::string>().starts_with("// recovered edit\n"));
        run(restored, "document.undo", {{"document", reopened["document"]}, {"revision", 2}});
        auto clean = run(restored, "document.open", {{"path", "main.cppm"}});
        require(!clean["dirty"] && meditor::read_file(file) == "export module main;\n");
    }
    { // An external change keeps both copies.
        meditor::Application crashed(workspace, {}, {}, options);
        auto document = run(crashed, "document.open", {{"path", "main.cppm"}})["document"].get<std::string>();
        run(crashed, "document.edit", {{"document", document}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "// lost edit\n"}}})}});
    }
    meditor::write_file_atomic(file, "externally replaced\n");
    {
        meditor::Application restored(workspace, {}, {}, options);
        auto status = run(restored, "workspace.status");
        require(status["recovery"]["conflicts"].size() == 1 && status["recovery"]["recovered"].empty());
        require(meditor::read_file(file) == "externally replaced\n");
        auto conflictDirectory = temporary / "data" / "recovery";
        bool archived {};
        for (const auto& item : std::filesystem::directory_iterator(conflictDirectory))
            for (const auto& entry : std::filesystem::directory_iterator(item.path()))
                if (meditor::path_to_utf8(entry.path().filename()).find(".conflict.json") != std::string::npos) archived = true;
        require(archived);
    }
    { // Saving clears the entry.
        meditor::Application editor(workspace, {}, {}, options);
        auto document = run(editor, "document.open", {{"path", "main.cppm"}})["document"].get<std::string>();
        run(editor, "document.edit", {{"document", document}, {"revision", 1}, {"edits", Json::array({{{"start", 0}, {"end", 0}, {"text", "// kept\n"}}})}});
        run(editor, "document.save", {{"document", document}, {"revision", 2}});
    }
    {
        meditor::Application after(workspace, {}, {}, options);
        auto status = run(after, "workspace.status");
        require(status["recovery"]["recovered"].empty() && status["recovery"]["conflicts"].empty()); // Archived conflicts report once, at detection.
        require(meditor::read_file(file).starts_with("// kept\nexternally replaced"));
    }
    std::println("Crash recovery: unsaved buffers restore, external changes keep both copies, saving clears entries: passed");
}

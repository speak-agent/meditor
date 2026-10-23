// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.ui.workbench;
import std;
import huxerui;
export namespace meditor::ui {
void configure(int argc, char** argv);
struct WorkspaceSelection {
    std::filesystem::path path;
    std::optional<huxerui::FileReference> grant;
};
struct WorkspaceServices {
    std::function<huxerui::Task<std::optional<WorkspaceSelection>>()> chooseWorkspace;
};
huxerui::View workbench(WorkspaceServices services);
huxerui::View workbench();
}

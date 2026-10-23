// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module app;
import huxerui;
import meditor.ui.workbench;
const huxerui::Application application{
    meditor::ui::workbench,
    {
        .window = {
            .title = "meditor",
            .initial_size = {1200.0F, 800.0F},
            .chrome_mode = huxerui::WindowChromeMode::Custom,
            .title_bar_height = 64.0F,
        },
        .show_debug_overlay = false,
    },
};

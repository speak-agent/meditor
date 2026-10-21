export module app;
import std;
import huxerui;
import meditor.ui.workbench;
const huxerui::Application application{
    meditor::ui::workbench,
    {.window = {.title = "meditor", .initial_size = {1200.0F, 800.0F}}},
};

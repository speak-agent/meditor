// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import huxerui;
import meditor.launch;
import meditor.cli;
import meditor.i18n;
import meditor.platform;
import meditor.ui.workbench;
import app;
int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> arguments;
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
        auto request = meditor::parse_launch(arguments, std::filesystem::current_path());
        std::vector<char*> pointers;
        for (auto& value : request.arguments) pointers.push_back(value.data());
        if (request.mode == meditor::LaunchMode::Cli) {
            meditor::prepare_cli_console();
            return meditor::cli::run(static_cast<int>(pointers.size()), pointers.data());
        }
        meditor::ui::configure(static_cast<int>(pointers.size()), pointers.data());
        return huxerui::RunApplication();
    } catch (const std::exception& error) {
        meditor::prepare_cli_console();
        std::cerr << meditor::i18n::Localizer(meditor::system_language_tag()).text("cli.invalidArguments") << '\n' << error.what() << '\n';
        return 2;
    } catch (...) {
        meditor::prepare_cli_console();
        std::cerr << meditor::i18n::Localizer(meditor::system_language_tag()).text("cli.invalidArguments") << '\n';
        return 2;
    }
}

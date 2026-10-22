// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.launch;
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) {
        std::cerr << "Launch routing contract failed at line " << at.line() << '\n';
        std::exit(1);
    }
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("meditor-launch-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "project");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    auto parse = [&](std::initializer_list<std::string_view> values) { return meditor::parse_launch(std::span(values.begin(), values.size()), root); };
    auto option = [](const auto& request, std::string_view name) {
        const auto found = std::ranges::find(request.arguments, name);
        return found == request.arguments.end() ? std::string{} : *std::next(found);
    };
    require(parse({}).mode == meditor::LaunchMode::Gui && parse({}).arguments.size() == 1);
    for (auto name : {"main.cpp", "build", "doctor", "serve"}) {
        const auto request = parse({name}); require(request.mode == meditor::LaunchMode::Gui);
        require(option(request, "--open") == name);
    }
    const auto directory = parse({"project"});
    require(directory.mode == meditor::LaunchMode::Gui && option(directory, "--open").empty());
    require(std::filesystem::u8path(option(directory, "--workspace")) == std::filesystem::weakly_canonical(root / "project"));
    require(option(parse({"--workspace", "project", "main.cpp"}), "--open") == "main.cpp");
    require(option(parse({"--gui", "--", "--cli"}), "--open") == "--cli");
    require(option(parse({"--", "-draft.cpp"}), "--open") == "-draft.cpp");
    require(option(parse({"hello 世界.cpp"}), "--open") == "hello 世界.cpp");
    require(parse({"--help"}).mode == meditor::LaunchMode::Cli && parse({"--version"}).mode == meditor::LaunchMode::Cli);
    const auto cli = parse({"--cli", "workspace", "status", "--format", "json"});
    require(cli.mode == meditor::LaunchMode::Cli && cli.arguments == std::vector<std::string>{"meditor", "workspace", "status", "--format", "json"});
    for (const auto& values : {std::vector<std::string_view>{"--gui", "--cli"}, {"one", "two"}, {"--workspace"}, {"--open", "one", "two"}, {"--workspace", "project", "../outside.cpp"}}) {
        bool rejected {}; try { meditor::parse_launch(values, root); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
    std::println("Single-app launch: GUI defaults/files/directories, explicit CLI, Unicode, literal paths and conflict rejection passed");
}

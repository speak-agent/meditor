// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.launch;
import std;
namespace meditor {
LaunchRequest parse_launch(std::span<const std::string_view> arguments, const std::filesystem::path& directory) {
    LaunchRequest request;
    if (!arguments.empty() && (arguments.front() == "--cli" || arguments.front() == "--help" || arguments.front() == "--version")) {
        request.mode = LaunchMode::Cli;
        for (auto value : arguments.subspan(arguments.front() == "--cli" ? 1 : 0)) request.arguments.emplace_back(value);
        return request;
    }
    std::optional<std::filesystem::path> workspace, file, positional;
    bool literal {};
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto argument = arguments[i];
        if (!literal && argument == "--") { literal = true; continue; }
        if (!literal && argument == "--gui" && i == 0) continue;
        if (!literal && argument == "--portable") { request.arguments.emplace_back(argument); continue; }
        if (!literal && (argument == "--workspace" || argument == "--open" || argument == "--tool-root" || argument == "--settings-file" || argument == "--data-root")) {
            if (++i == arguments.size()) throw std::invalid_argument("Missing option value");
            const auto value = arguments[i];
            if (value.empty()) throw std::invalid_argument("Empty option value");
            if (argument == "--workspace") {
                if (workspace) throw std::invalid_argument("Repeated workspace");
                workspace = std::filesystem::u8path(value);
            } else if (argument == "--open") {
                if (file) throw std::invalid_argument("Repeated file");
                file = std::filesystem::u8path(value);
            } else { request.arguments.emplace_back(argument); request.arguments.emplace_back(value); }
            continue;
        }
        if (!literal && argument.starts_with('-')) throw std::invalid_argument("Unknown GUI option");
        if (positional) throw std::invalid_argument("Only one launch path is supported");
        positional = std::filesystem::u8path(argument);
    }
    if (positional && file) throw std::invalid_argument("Conflicting launch paths");
    auto absolute = [&](const std::filesystem::path& path) { return std::filesystem::weakly_canonical(path.is_absolute() ? path : directory / path); };
    if (workspace) workspace = absolute(*workspace);
    if (positional) {
        const auto path = std::filesystem::weakly_canonical(positional->is_absolute() ? *positional : workspace.value_or(directory) / *positional);
        if (std::filesystem::is_directory(path)) {
            if (workspace) throw std::invalid_argument("Conflicting workspaces");
            workspace = path;
        } else {
            if (!workspace) workspace = path.parent_path();
            file = path.lexically_relative(*workspace);
            if (file->empty() || file->is_absolute() || file->generic_string().starts_with("../")) throw std::invalid_argument("File is outside the workspace");
        }
    }
    if (file && !workspace) workspace = absolute(directory);
    auto append = [&](std::string_view option, const std::filesystem::path& path) {
        const auto value = path.generic_u8string();
        request.arguments.emplace_back(option); request.arguments.emplace_back(reinterpret_cast<const char*>(value.data()), value.size());
    };
    if (workspace) append("--workspace", *workspace);
    if (file) append("--open", *file);
    return request;
}
}

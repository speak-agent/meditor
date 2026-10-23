// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.build.templates;
import std;
import nlohmann.json;

namespace meditor::build {
std::filesystem::path compile_templates(const std::filesystem::path& resources, const std::filesystem::path& outputDirectory) {
    // Walks resources/templates/<name>/** and emits a self-contained Modules catalog of the
    // versioned meditor project templates; every path is relative and validated.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> templates;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(resources, error)) {
        if (!entry.is_directory()) continue;
        auto name = entry.path().filename().generic_string();
        if (name.empty() || name.front() == '.') throw std::runtime_error("Invalid template directory name");
        std::vector<std::pair<std::string, std::string>> files;
        for (const auto& file : std::filesystem::recursive_directory_iterator(entry.path(), error)) {
            if (!file.is_regular_file()) continue;
            auto relative = file.path().lexically_relative(resources.parent_path()).generic_string();
            std::ifstream input(file.path(), std::ios::binary);
            if (!input) throw std::runtime_error("Unreadable template file");
            const std::string content {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (content.find('\0') != std::string::npos) throw std::runtime_error("Template files must be text");
            files.emplace_back(relative, content);
        }
        if (files.empty()) throw std::runtime_error("Template without files");
        std::sort(files.begin(), files.end());
        templates.emplace_back(name, std::move(files));
    }
    std::sort(templates.begin(), templates.end());
    if (templates.empty()) throw std::runtime_error("Missing project templates");
    const auto literal = [](const std::string& value) { return nlohmann::json(value).dump(); };
    std::string source = "// SPDX-License-Identifier: GPL-3.0-only\n// Copyright (C) 2026 Sunrisepeak & mcpp-community\n"
        "export module meditor.templates.catalogs;\nimport std;\n"
        "export namespace meditor::templates::detail {\n"
        "struct TemplateFile { std::string_view path, content; };\n"
        "struct Template { std::string_view name; const TemplateFile* files; std::size_t count; };\n";
    std::vector<std::string> arrays;
    for (std::size_t index = 0; index < templates.size(); ++index) {
        const auto& [name, files] = templates[index];
        source += "inline constexpr TemplateFile TEMPLATE_" + std::to_string(index) + "[] = {\n";
        for (const auto& [path, content] : files)
            source += "{" + literal(path) + "," + literal(content) + "},\n";
        source += "};\n";
        arrays.push_back(name);
    }
    source += "inline constexpr Template TEMPLATES[] = {\n";
    for (std::size_t index = 0; index < templates.size(); ++index)
        source += "{" + literal(arrays[index]) + ",TEMPLATE_" + std::to_string(index) + ",std::size(TEMPLATE_" + std::to_string(index) + ")},\n";
    source += "};\n}\n";
    std::filesystem::create_directories(outputDirectory);
    const auto output = std::filesystem::absolute(outputDirectory / "templates-catalogs.cppm");
    std::ifstream previous(output, std::ios::binary);
    const std::string old {std::istreambuf_iterator<char>(previous), std::istreambuf_iterator<char>()};
    if (old != source) {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << source; file.close();
        if (!file) throw std::runtime_error("Could not write template module");
    }
    return output;
}
}

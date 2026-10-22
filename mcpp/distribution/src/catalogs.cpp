// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.build.catalogs;
import std;
import nlohmann.json;

namespace meditor::build {
std::filesystem::path compile_catalogs(const std::filesystem::path& resources, const std::filesystem::path& outputDirectory) {
    auto load = [&](const char* language) {
        std::ifstream input(resources / (std::string(language) + ".json"), std::ios::binary);
        if (!input) throw std::runtime_error("Missing translation catalog");
        auto catalog = nlohmann::json::parse(input);
        if (!catalog.is_object() || catalog.empty()) throw std::runtime_error("Invalid translation catalog");
        for (const auto& item : catalog.items()) {
            if (item.key().empty() || !item.value().is_string() || item.value().get<std::string>().empty())
                throw std::runtime_error("Translation keys and values must be nonempty strings");
            for (const auto ch : item.key())
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_'))
                    throw std::runtime_error("Translation keys must use stable ASCII identifiers");
            for (const unsigned char ch : item.value().get<std::string>())
                if (ch < 32 && ch != '\n' && ch != '\t' && ch != '\r')
                    throw std::runtime_error("Translation text contains an unsupported control character");
        }
        return catalog;
    };
    const auto english = load("en"), chinese = load("zh");
    if (english.size() != chinese.size()) throw std::runtime_error("Translation catalogs have different keys");
    std::string source = "// SPDX-License-Identifier: GPL-3.0-only\n// Copyright (C) 2026 Sunrisepeak & mcpp-community\nexport module meditor.i18n.catalogs;\nimport std;\nexport namespace meditor::i18n::detail {\nstruct Entry { std::string_view id, english, chinese; };\ninline constexpr auto ENTRIES = std::to_array<Entry>({\n";
    for (const auto& item : english.items()) {
        if (!chinese.contains(item.key())) throw std::runtime_error("Translation catalogs have different keys");
        source += "{" + nlohmann::json(item.key()).dump() + "," + item.value().dump() + "," + chinese.at(item.key()).dump() + "},\n";
    }
    source += "});\n}\n";
    std::filesystem::create_directories(outputDirectory);
    const auto output = std::filesystem::absolute(outputDirectory / "i18n-catalogs.cppm");
    std::ifstream previous(output, std::ios::binary);
    const std::string old {std::istreambuf_iterator<char>(previous), std::istreambuf_iterator<char>()};
    if (old != source) {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << source; file.close();
        if (!file) throw std::runtime_error("Could not write translation module");
    }
    return output;
}
}

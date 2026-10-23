// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.about;
import std;
import nlohmann.json;
import meditor.about.resources;
import meditor.platform;

namespace meditor::about {
namespace {
using Json = nlohmann::json;
std::optional<std::string> notice_text(const std::filesystem::path& root, std::string_view relative) {
    try {
        const auto base = std::filesystem::weakly_canonical(root);
        const auto file = std::filesystem::weakly_canonical(base / relative);
        const auto within = file.lexically_relative(base);
        if (within.empty() || within == "." || within.is_absolute() || *within.begin() == "..") return {};
        return read_file(file, 1024 * 1024);
    } catch (...) { return {}; }
}
}
Json describe(const std::filesystem::path& tools) {
    static const auto embedded = Json::parse(detail::METADATA);
    auto result = embedded;
    Json receipt;
    bool recorded {};
    try {
        receipt = Json::parse(read_file(tools / "inventory.json", 32 * 1024 * 1024));
        recorded = receipt.at("schemaVersion") == 1 && receipt.at("target") == embedded.at("payloadTarget") &&
            !embedded.at("expectedSources").is_null() && receipt.at("sources") == embedded.at("expectedSources");
    } catch (...) {}
    Json entries = Json::array();
    for (const auto& definition : embedded.at("toolDefinitions").items()) {
        const auto name = definition.key(); auto entry = definition.value(); entry["id"] = name; entry["role"] = "bundled-tool";
        entry["expectedVersion"] = entry.at("version"); entry["version"] = nullptr;
#if defined(_WIN32)
        const auto executable = name + "/bin/" + name + ".exe";
#else
        const auto executable = name + "/bin/" + name;
#endif
        std::error_code error; const auto available = std::filesystem::is_regular_file(tools / executable, error);
        bool within {};
        try {
            const auto base = std::filesystem::weakly_canonical(tools);
            const auto relative = std::filesystem::weakly_canonical(tools / executable).lexically_relative(base);
            within = !relative.empty() && relative != "." && !relative.is_absolute() && *relative.begin() != "..";
        } catch (...) {}
        const auto matches = recorded && available && within;
        entry["state"] = !available ? "missing" : matches ? "recorded" : "unverified";
        entry["integrity"] = "not-rechecked";
        entry["licenseText"] = nullptr; entry["noticeStatus"] = "pending";
        if (matches) {
            entry["version"] = entry.at("expectedVersion");
            const auto relative = name == "mcpp" ? "mcpp/LICENSE" : "mcppls/licenses/mcppls-LICENSE.txt";
            if (const auto text = notice_text(tools, relative)) { entry["licenseText"] = *text; entry["noticeStatus"] = "present"; }
        }
        entries.push_back(std::move(entry));
    }
    result["tools"] = std::move(entries);
    result.erase("toolDefinitions"); result.erase("expectedSources");
    return result;
}
Json licenses(const std::filesystem::path& tools) {
    const auto metadata = describe(tools);
    auto components = metadata.at("libraries");
    auto product = metadata.at("product"); product["id"] = product.at("name"); product["role"] = "product";
    components.insert(components.begin(), std::move(product));
    for (const auto& tool : metadata.at("tools")) components.push_back(tool);
    return {{"schemaVersion", 1}, {"components", components}, {"complete", false}, {"releaseReady", false},
        {"scope", "direct-dependencies"}, {"gaps", Json::array({"transitive-runtime-notices", "source-distribution-materials", "nested-tool-inventories"})}};
}
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.build.metadata;
import std;
import nlohmann.json;

namespace meditor::build {
namespace {
using Json = nlohmann::json;
std::string read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 256 * 1024) throw std::runtime_error("Metadata input exceeds its limit");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Metadata input is unavailable");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::string locked(const std::string& text, std::string_view package, std::string_view field) {
    // Read only the generated lockfile's quoted package fields, never a general
    // TOML document. Unexpected/multiple sections and field spellings fail closed.
    const auto header = "[package.\"" + std::string(package) + "\"]";
    const auto start = text.find(header);
    if (start == std::string::npos || text.find(header, start + 1) != std::string::npos) throw std::runtime_error("Missing or ambiguous locked package");
    const auto end = text.find('\n' + std::string("["), start + header.size());
    const auto section = text.substr(start + header.size(), end == std::string::npos ? end : end - start - header.size());
    const std::regex pattern("(?:^|\\n)[ \\t]*" + std::string(field) + "[ \\t]*=[ \\t]*\"([^\"\\r\\n\\\\]*)\"[ \\t]*(?:\\r?\\n|$)");
    auto iterator = std::sregex_iterator(section.begin(), section.end(), pattern);
    if (iterator == std::sregex_iterator{}) throw std::runtime_error("Missing locked package field");
    const auto result = (*iterator)[1].str();
    if (++iterator != std::sregex_iterator{}) throw std::runtime_error("Ambiguous locked package field");
    return result;
}
bool commit_id(std::string_view commit) {
    return commit.size() == 40 && std::all_of(commit.begin(), commit.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}
}
std::filesystem::path compile_metadata(const std::filesystem::path& repository, const std::filesystem::path& outputDirectory,
        std::string_view version, std::string_view payloadTarget, std::string_view sourceCommit, bool appleRuntime) {
    if (!sourceCommit.empty() && !commit_id(sourceCommit)) throw std::runtime_error("Invalid product source revision");
    auto product = Json::parse(read(repository / "resources/product.json"));
    if (!product.is_object() || product.size() != 6 || product.value("schemaVersion", 0) != 1 || product.value("name", "") != "meditor" || product.value("license", "") != "GPL-3.0-only")
        throw std::runtime_error("Invalid product metadata schema");
    for (const auto& item : product.items())
        if (item.key() != "schemaVersion" && item.key() != "name" && item.key() != "license" && item.key() != "contributors" && item.key() != "contact" && item.key() != "repository")
            throw std::runtime_error("Unknown product metadata field");
    const auto license = read(repository / "LICENSE"), policy = read(repository / "LICENSING.md");
    for (const auto* key : {"license", "contributors", "contact"})
        if (license.find(product.at(key).get<std::string>()) == std::string::npos) throw std::runtime_error("Product metadata differs from LICENSE");
    if (product.at("repository") != "https://github.com/speak-agent/meditor") throw std::runtime_error("Unexpected product source repository");
    product["version"] = version; product["licenseText"] = license; product["licensingPolicy"] = policy;
    product["descriptionId"] = "about.description";
    product["description"] = Json::parse(read(repository / "resources/i18n/en.json")).at("about.description");
    product["sourceCommit"] = sourceCommit.empty() ? Json(nullptr) : Json(sourceCommit);
    product["sourceUrl"] = sourceCommit.empty() ? Json(nullptr) : Json(product.at("repository").get<std::string>() + "/tree/" + std::string(sourceCommit));
    const auto lock = read(repository / "mcpp.lock");
    const auto notices = Json::parse(read(repository / "resources/licenses/manifest.json"));
    if (!notices.is_object() || notices.size() != 2 || notices.value("schemaVersion", 0) != 1 || !notices.at("components").is_array())
        throw std::runtime_error("Unsupported direct notice inventory schema");
    Json libraries = Json::array();
    for (const auto& notice : notices.at("components")) {
        if (!notice.is_object() || notice.size() != 6) throw std::runtime_error("Invalid direct library notice schema");
        if (!std::regex_match(notice.at("sha256").get<std::string>(), std::regex("[0-9a-f]{64}"))) throw std::runtime_error("Invalid notice digest");
        for (const auto& field : notice.items())
            if (field.key() != "id" && field.key() != "license" && field.key() != "repository" && field.key() != "revision" && field.key() != "file" && field.key() != "sha256")
                throw std::runtime_error("Unknown notice field");
        const auto id = notice.at("id").get<std::string>();
        const auto filename = notice.at("file").get<std::string>();
        if (filename.empty() || filename.find_first_of("/\\:") != std::string::npos || filename == "." || filename == "..") throw std::runtime_error("Unsafe notice filename");
        const auto text = read(repository / "resources/licenses" / filename);
        const auto revision = notice.at("revision").get<std::string>();
        if (id == "HuxerUI") {
            if (notice.at("repository") != "https://github.com/Sunrisepeak/HuxerUI" || notice.at("license") != "MIT") throw std::runtime_error("Unexpected framework notice source");
            if (!commit_id(revision) || locked(lock, "huxerui.huxerui", "source") != "git+https://github.com/Sunrisepeak/HuxerUI.git#branch=meditor@" + revision)
                throw std::runtime_error("HuxerUI notice revision differs from the lockfile");
        } else if (id == "nlohmann.json") {
            if (notice.at("repository") != "https://github.com/nlohmann/json" || notice.at("license") != "MIT") throw std::runtime_error("Unexpected JSON notice source");
            if (locked(lock, "nlohmann.json", "version") != revision) throw std::runtime_error("JSON notice version differs from the lockfile");
        } else throw std::runtime_error("Unknown direct library notice");
        auto entry = notice; entry["version"] = revision; entry["licenseText"] = text;
        entry["role"] = id == "HuxerUI" ? "gui-framework" : "runtime-library";
        entry["noticeStatus"] = "present"; libraries.push_back(std::move(entry));
    }
    if (libraries.size() != 2 || libraries[0]["id"] == libraries[1]["id"]) throw std::runtime_error("Missing direct library notice");
    if (appleRuntime) libraries.push_back({{"id", "llvm.libcxx"}, {"version", nullptr}, {"role", "runtime-library"},
        {"license", "Apache-2.0 WITH LLVM-exception"}, {"repository", "https://github.com/llvm/llvm-project"}, {"noticeStatus", "pending"}, {"licenseText", nullptr}});
    const auto tools = Json::parse(read(repository / "packaging/tools.json"));
    if (!tools.is_object() || tools.size() != 3 || tools.value("schemaVersion", 0) != 1 || !tools.at("targets").is_object() || !tools.at("tools").is_object())
        throw std::runtime_error("Unsupported tool specification schema");
    if (tools.at("tools").size() != 2) throw std::runtime_error("Unknown bundled tool definition");
    for (const auto& item : tools.at("tools").items()) {
        const auto expected = item.key() == "mcpp" ? "https://github.com/mcpp-community/mcpp" : item.key() == "mcppls" ? "https://github.com/Sunrisepeak/mcpp-language-server" : "";
        if (expected == std::string_view{} || item.value().size() != 3 || item.value().at("repository") != expected || item.value().at("license") != "Apache-2.0" || !item.value().at("version").is_string())
            throw std::runtime_error("Invalid bundled tool definition");
        const auto toolVersion = item.value().at("version").get<std::string>();
        if (!std::regex_match(toolVersion, std::regex("[0-9]+(?:\\.[0-9]+){2,3}(?:-[a-z0-9.-]+)?"))) throw std::runtime_error("Invalid bundled tool version");
        const auto prefix = std::string(expected) + "/releases/download/v" + toolVersion + "/";
        for (const auto& target : tools.at("targets").items()) {
            if (!target.value().is_object() || target.value().size() != 2) throw std::runtime_error("Invalid target tool records");
            const auto& archive = target.value().at(item.key());
            if (archive.size() != 2 || !archive.at("url").get<std::string>().starts_with(prefix) ||
                !std::regex_match(archive.at("sha256").get<std::string>(), std::regex("[0-9a-f]{64}")))
                throw std::runtime_error("Tool version differs from its pinned archive record");
        }
    }
    Json metadata {{"schemaVersion", 1}, {"product", product}, {"libraries", libraries}, {"toolDefinitions", tools.at("tools")},
        {"payloadTarget", payloadTarget}, {"expectedSources", tools.at("targets").contains(std::string(payloadTarget)) ? tools.at("targets").at(std::string(payloadTarget)) : Json(nullptr)},
        {"declarationScope", "direct-dependencies"}, {"completeDistributionNotices", false}, {"releaseReady", false}};
    const auto source = "// SPDX-License-Identifier: GPL-3.0-only\n// Copyright (C) 2026 Sunrisepeak & mcpp-community\nexport module meditor.about.resources;\nimport std;\nexport namespace meditor::about::detail { inline constexpr std::string_view METADATA = " + Json(metadata.dump()).dump() + "; }\n";
    std::filesystem::create_directories(outputDirectory);
    const auto output = std::filesystem::absolute(outputDirectory / "about-metadata.cppm");
    std::ifstream oldFile(output, std::ios::binary); const std::string previous {std::istreambuf_iterator<char>(oldFile), std::istreambuf_iterator<char>()}; oldFile.close();
    if (previous != source) { std::ofstream file(output, std::ios::binary | std::ios::trunc); file << source; file.close(); if (!file) throw std::runtime_error("Cannot write product metadata"); }
    return output;
}
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.runtime;
import std;
import nlohmann.json;
import meditor.platform;

namespace meditor {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string(); return {value.begin(), value.end()};
}
std::filesystem::path portable_data(const std::filesystem::path& marker) {
    using Json = nlohmann::json;
    const auto descriptor = Json::parse(read_file(marker, 16 * 1024));
    if (!descriptor.is_object() || !descriptor.contains("schemaVersion") ||
        !descriptor["schemaVersion"].is_number_integer() || descriptor["schemaVersion"] != 1)
        throw std::runtime_error("Unsupported portable marker schema");
    for (const auto& field : descriptor.items())
        if (field.key() != "schemaVersion" && field.key() != "dataDirectory") throw std::runtime_error("Unknown portable marker field");
    const auto name = descriptor.value("dataDirectory", std::string("data"));
    const auto path = std::filesystem::u8path(name);
    if (name.empty() || name.find_first_of("\\:") != std::string::npos || name.find('\0') != std::string::npos || path.is_absolute() || path.has_root_name())
        throw std::runtime_error("Portable data directory must be a relative path");
    // The portable format uses '/' on every host. Validate its components
    // directly; some NDK std modules do not expose path::iterator comparison.
    std::size_t start {};
    for (;;) {
        const auto end = name.find('/', start);
        const auto part = std::string_view(name).substr(start, end == std::string::npos ? name.size() - start : end - start);
        if (part == ".." || part == ".") throw std::runtime_error("Portable data directory escapes its root");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return path;
}
bool inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && relative != "." && *relative.begin() != ".." && !relative.is_absolute();
}
void validate_storage(const RuntimeDirectories& directories) {
    if (directories.portable && !std::filesystem::is_directory(directories.installationRoot))
        throw std::runtime_error("Portable installation moved; reopen it from its new location");
    const auto root = std::filesystem::weakly_canonical(directories.dataRoot);
    if (root != directories.dataRoot || (directories.portable && !inside(directories.installationRoot, root)))
        throw std::runtime_error("Application data directory changed its storage boundary");
    if (!inside(root, std::filesystem::weakly_canonical(root / "settings.json")))
        throw std::runtime_error("Application settings escape their storage boundary");
    const auto tools = std::filesystem::weakly_canonical(directories.toolRoot);
    if (tools != directories.toolRoot || (directories.portable && tools != root))
        throw std::runtime_error("Application tool directory changed its storage boundary");
    for (const auto* name : {"mcpp", "cache/mcppls", "temp"}) {
        if (!inside(tools, std::filesystem::weakly_canonical(tools / name)))
            throw std::runtime_error("Application data entry escapes its storage boundary");
    }
}
}
RuntimeDirectories RuntimeDirectories::resolve(const RuntimeOptions& options) {
    auto binary = options.binaryDirectory.empty() ? executable_directory() : options.binaryDirectory;
    if (binary.empty() || !binary.is_absolute() || !std::filesystem::is_directory(binary))
        throw std::runtime_error("Application binary directory is unavailable");
    binary = std::filesystem::weakly_canonical(binary);
    std::vector<std::filesystem::path> markers;
    if (std::filesystem::exists(binary / "portable.json")) markers.push_back(binary / "portable.json");
    if (binary.filename() == "bin" && std::filesystem::exists(binary.parent_path() / "portable.json"))
        markers.push_back(binary.parent_path() / "portable.json");
    if (markers.size() > 1) throw std::runtime_error("Ambiguous portable markers; select one package layout");
    const auto relativeData = markers.empty() ? std::filesystem::path("data") : portable_data(markers.front());
    RuntimeDirectories result;
    result.installationRoot = markers.empty() ? binary : markers.front().parent_path();
    result.portable = options.portable || !markers.empty();
    if (!options.dataRoot.empty()) result.dataRoot = std::filesystem::weakly_canonical(std::filesystem::absolute(options.dataRoot));
    else if (result.portable) {
        result.dataRoot = std::filesystem::weakly_canonical(result.installationRoot / relativeData);
    } else {
        const auto directory = user_config_directory();
        if (directory.empty()) throw std::runtime_error("Application data directory requires a platform storage adapter");
        result.dataRoot = std::filesystem::weakly_canonical(directory / "meditor");
    }
    if (result.portable && !inside(result.installationRoot, result.dataRoot))
        throw std::runtime_error("Portable data must stay within its installation root");
    result.toolRoot = result.dataRoot;
    if (!result.portable && options.dataRoot.empty()) {
        const auto cache = user_cache_directory();
        if (cache.empty()) throw std::runtime_error("Application tools require a platform cache adapter");
        result.toolRoot = std::filesystem::weakly_canonical(cache / "meditor");
    }
    validate_storage(result);
    return result;
}
std::filesystem::path RuntimeDirectories::settings_file() const { return dataRoot / "settings.json"; }
ProcessEnvironment RuntimeDirectories::tool_environment() const {
    validate_storage(*this);
    ProcessEnvironment values {
        {"MCPP_HOME", utf8(toolRoot / "mcpp")},
        {"MCPPLS_CACHE_DIR", utf8(toolRoot / "cache/mcppls")},
        {"TMPDIR", utf8(toolRoot / "temp")},
        {"TEMP", utf8(toolRoot / "temp")},
        {"TMP", utf8(toolRoot / "temp")},
    };
    return values;
}
void RuntimeDirectories::prepare_tools() const {
    validate_storage(*this);
    for (const auto& directory : {toolRoot / "mcpp", toolRoot / "cache/mcppls", toolRoot / "temp"})
        std::filesystem::create_directories(directory);
}
}

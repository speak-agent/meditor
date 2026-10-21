module meditor.distribution;
import std;
import nlohmann.json;

namespace meditor::distribution {
std::string payload_target(std::string_view arch, std::string_view os, std::string_view environment) {
    if (os == "linux" && environment == "gnu") return std::string(arch) + "-linux-gnu";
    if (os == "macos") return std::string(arch) + "-macos";
    if (os == "windows") return std::string(arch) + "-windows-msvc";
    return {};
}
PayloadPlan plan(const std::filesystem::path& directory, std::string_view target, bool required) {
    PayloadPlan result;
    result.receipt = directory / "inventory.json";
    if (!std::filesystem::is_regular_file(result.receipt)) {
        if (required) throw std::runtime_error("Prepare the complete pinned tool payload before packaging this target");
        return {};
    }
    if (std::filesystem::file_size(result.receipt) > 32 * 1024 * 1024) throw std::runtime_error("Tool inventory exceeds its limit");
    std::ifstream input(result.receipt);
    auto receipt = nlohmann::json::parse(input);
    if (receipt.at("schemaVersion") != 1 || receipt.at("target") != target || !receipt.at("files").is_array()) throw std::runtime_error("Tool inventory does not match this target");
    if (receipt.at("files").size() > 100000) throw std::runtime_error("Tool inventory contains too many files");
    auto base = std::filesystem::weakly_canonical(directory);
    std::set<std::string> destinations;
    for (const auto& file : receipt.at("files")) {
        auto name = file.at("path").get<std::string>();
        std::filesystem::path relative(name);
        if (!name.starts_with("tools/") || name.find_first_of("\\:") != std::string::npos || std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; }) || relative.is_absolute()) throw std::runtime_error("Invalid tool inventory path");
        for (const auto& part : relative) if (part == ".." || part == ".") throw std::runtime_error("Tool inventory path escapes its payload");
        if (!destinations.insert(name).second) throw std::runtime_error("Duplicate tool inventory path");
        auto source = base / relative;
        auto resolved = std::filesystem::weakly_canonical(source).lexically_relative(base);
        if (resolved.empty() || *resolved.begin() == ".." || !std::filesystem::is_regular_file(source)) throw std::runtime_error("Tool inventory file is unavailable or outside its payload");
        if (file.contains("bytes") && std::filesystem::file_size(source) != file.at("bytes").get<std::uintmax_t>()) throw std::runtime_error("Tool payload file size changed");
        result.files.push_back({source, name});
    }
    auto suffix = target.find("windows") == std::string_view::npos ? "" : ".exe";
    for (std::string_view tool : {"mcpp", "mcppls"}) {
        auto executable = std::format("tools/{0}/bin/{0}{1}", tool, suffix);
        if (!destinations.contains(executable)) throw std::runtime_error("Tool inventory is missing a required executable");
    }
    result.files.push_back({result.receipt, "tools/inventory.json"});
    return result;
}
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.recovery;
import meditor.platform;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Recovery contract failed at line " + std::to_string(location.line())); }
std::string digest_placeholder(const std::string& data) {
    std::uint64_t a = 1469598103934665603ULL;
    for (const char character : data) a = (a ^ static_cast<unsigned char>(character)) * 1099511628211ULL;
    std::string digest;
    for (int shift = 60; shift >= 0; shift -= 4) digest += "0123456789abcdef"[(a >> shift) & 0xF];
    return digest;
}
}
int main() {
    const auto temporary = std::filesystem::temp_directory_path() / ("meditor-recovery-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(temporary / "workspace");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {temporary};
    const auto workspace = temporary / "workspace", dataRoot = temporary / "data";
    const auto file = workspace / std::filesystem::u8path("模块.cppm");
    std::filesystem::create_directories(workspace);
    meditor::write_file_atomic(file, "saved base");
    {
        meditor::RecoveryStore store(dataRoot, workspace);
        store.remember("模块.cppm", "saved base + unsaved edits", 7, digest_placeholder("saved base"));
        store.remember("created-only.cppm", "", 2, digest_placeholder(""));
        require(store.pending());
    } // A crashed process runs no cleanup.
    {
        meditor::RecoveryStore store(dataRoot, workspace);
        auto digest = [&](const std::filesystem::path& path) { try { return digest_placeholder(meditor::read_file(workspace / path)); } catch (...) { return digest_placeholder(""); } };
        auto report = store.collect(digest);
        require(report.conflicted.empty());
        require(report.recoverable.size() == 2);
        std::map<std::string, const meditor::RecoveryStore::Entry*> byPath;
        for (const auto& entry : report.recoverable) byPath[entry.documentPath] = &entry;
        require(byPath.contains("模块.cppm") && byPath.at("模块.cppm")->text == "saved base + unsaved edits" && byPath.at("模块.cppm")->revision == 7);
        require(byPath.contains("created-only.cppm") && byPath.at("created-only.cppm")->text.empty());
        store.discard("created-only.cppm");
        auto after = store.collect(digest);
        require(after.recoverable.size() == 1 && after.recoverable.front().documentPath == "模块.cppm");
    }
    {
        // External change underneath: both copies must survive.
        meditor::write_file_atomic(file, "changed externally");
        meditor::RecoveryStore store(dataRoot, workspace);
        auto digest = [&](const std::filesystem::path& path) { try { return digest_placeholder(meditor::read_file(workspace / path)); } catch (...) { return digest_placeholder(""); } };
        auto report = store.collect(digest);
        require(report.recoverable.empty() && report.conflicted.size() == 1 && report.conflicted.front().text == "saved base + unsaved edits");
        require(meditor::read_file(file) == "changed externally");
        bool archived {};
        for (const auto& item : std::filesystem::directory_iterator(meditor::RecoveryStore::directory_for(dataRoot, workspace)))
            if (meditor::path_to_utf8(item.path().filename()).find(".conflict.json") != std::string::npos) archived = true;
        require(archived);
    }
    {
        meditor::RecoveryStore store(dataRoot, workspace);
        auto digest = [&](const std::filesystem::path&) { return std::string(); };
        auto report = store.collect(digest);
        require(report.recoverable.empty() && report.conflicted.empty()); // Archived entries are never re-applied.
    }
    std::println("Recovery store: crash persistence, empty buffers, discard and both-copies conflicts: passed");
}

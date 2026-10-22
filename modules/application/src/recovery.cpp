// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.recovery;
import std;
import nlohmann.json;
import meditor.platform;

namespace meditor {
namespace {
using Json = nlohmann::json;
std::string workspace_key(const std::filesystem::path& workspace) {
    const auto canonical = std::filesystem::weakly_canonical(workspace).generic_u8string();
    std::string key;
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto character : canonical) { hash ^= static_cast<std::uint64_t>(character); hash *= 1099511628211ULL; }
    for (int shift = 60; shift >= 0; shift -= 4) key += "0123456789abcdef"[(hash >> shift) & 0xF];
    return key;
}
std::string digest_of(const std::string& data) {
    std::uint64_t a = 1469598103934665603ULL, b = 14695981039346656037ULL;
    for (const char character : data) { a = (a ^ static_cast<unsigned char>(character)) * 1099511628211ULL; b = (b + static_cast<unsigned char>(character)) * 3074457345618258791ULL; }
    std::string digest;
    for (const auto value : {a, b}) for (int shift = 60; shift >= 0; shift -= 4) digest += "0123456789abcdef"[(value >> shift) & 0xF];
    return digest;
}
std::filesystem::path as_path(const std::string& utf8) { return std::filesystem::u8path(utf8); }
}
std::string recovery_digest(const std::string& data) { return digest_of(data); }

struct RecoveryStore::Impl {
    std::filesystem::path directory;
    Impl(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace)
            : directory(dataRoot / "recovery" / workspace_key(workspace)) {}
    std::filesystem::path entry_path(const std::string& documentPath) const {
        std::string safe;
        for (const char character : documentPath)
            safe += (std::isalnum(static_cast<unsigned char>(character)) || character == '.' || character == '-' || character == '_') ? character : '_';
        return directory / (digest_of(documentPath) + "-" + safe.substr(0, 96) + ".json");
    }
};

std::filesystem::path RecoveryStore::directory_for(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace) {
    return dataRoot / "recovery" / workspace_key(workspace);
}

RecoveryStore::RecoveryStore(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace) : impl_(std::make_unique<Impl>(dataRoot, workspace)) {}
RecoveryStore::~RecoveryStore() = default;

void RecoveryStore::remember(const std::string& documentPath, const std::string& text, std::uint64_t revision, const std::string& baseDigest) {
    std::error_code ignored;
    std::filesystem::create_directories(impl_->directory, ignored); // Nothing exists until a buffer is actually unsaved.
    const Json record {
        {"schemaVersion", 1},
        {"document", documentPath},
        {"text", text},
        {"revision", revision},
        {"baseDigest", baseDigest},
    };
    write_file_atomic(impl_->entry_path(documentPath), record.dump());
}

RecoveryStore::Report RecoveryStore::collect(const std::function<std::string(const std::filesystem::path&)>& diskDigest) {
    Report report;
    std::error_code ignored;
    if (!std::filesystem::is_directory(impl_->directory, ignored)) return report;
    for (const auto& item : std::filesystem::directory_iterator(impl_->directory, ignored)) {
        if (ignored || !item.is_regular_file(ignored)) continue;
        const std::string name = path_to_utf8(item.path().filename());
        if (name.size() < 5 || !name.ends_with(".json") || name.ends_with(".conflict.json")) continue;
        Json record;
        try { record = Json::parse(read_file(item.path())); }
        catch (...) { continue; }
        if (record.value("schemaVersion", 0) != 1) continue;
        Entry entry { record.at("document").get<std::string>(), record.at("text").get<std::string>(), record.at("revision").get<std::uint64_t>(), record.at("baseDigest").get<std::string>() };
        const auto onDisk = diskDigest(as_path(entry.documentPath));
        if (onDisk == entry.baseDigest) report.recoverable.push_back(std::move(entry));
        else {
            // Keep both copies: archive the buffer beside the external change.
            std::filesystem::rename(item.path(), item.path().parent_path() / (path_to_utf8(item.path().stem()) + ".conflict.json"), ignored);
            if (ignored) report.recoverable.push_back(std::move(entry)); // Unarchivable entries stay recoverable rather than lost.
            else report.conflicted.push_back(std::move(entry));
        }
    }
    return report;
}

void RecoveryStore::discard(const std::string& documentPath) {
    std::error_code ignored;
    std::filesystem::remove(impl_->entry_path(documentPath), ignored);
}

bool RecoveryStore::pending() const {
    std::error_code ignored;
    if (!std::filesystem::is_directory(impl_->directory, ignored)) return false;
    for (const auto& item : std::filesystem::directory_iterator(impl_->directory, ignored)) {
        if (ignored) return false;
        if (item.is_regular_file(ignored) && !ignored) return true;
    }
    return false;
}
}

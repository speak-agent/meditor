// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.recovery;
import std;

export namespace meditor {
// Stable content digest shared by the store and its callers.
std::string recovery_digest(const std::string& data);

// Persists unsaved buffers under <dataRoot>/recovery/<workspace key> so a
// killed or crashed process loses at most one flush interval of typing.
// Entries survive process death; saving a document removes its entry, and a
// disk file that changed underneath keeps both copies (the entry is archived
// beside the external file instead of being applied over it).
class RecoveryStore {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    struct Entry { std::string documentPath; std::string text; std::uint64_t revision {}; std::string baseDigest; };
    struct Report { std::vector<Entry> recoverable; std::vector<Entry> conflicted; };
    static std::filesystem::path directory_for(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace);
    RecoveryStore(const std::filesystem::path& dataRoot, const std::filesystem::path& workspace);
    ~RecoveryStore();
    RecoveryStore(const RecoveryStore&) = delete;
    RecoveryStore& operator=(const RecoveryStore&) = delete;
    // Records the live buffer of one document, including an empty unsaved one.
    void remember(const std::string& documentPath, const std::string& text, std::uint64_t revision, const std::string& baseDigest);
    // Applies pending entries: bases that still match the disk file become
    // recoverable; diverging files keep both copies (entry archived, never
    // applied over external changes).
    Report collect(const std::function<std::string(const std::filesystem::path&)>& diskDigest);
    void discard(const std::string& documentPath);
    bool pending() const;
};
}

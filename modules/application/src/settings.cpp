module meditor.settings;
import std;
import nlohmann.json;
import meditor.platform;

namespace meditor {
namespace {
using Json = nlohmann::json;
Json defaults() { return {{"revision", 0}, {"values", {{"theme", "dark"}, {"reducedMotion", false}}}}; }
void validate(const Json& values, bool partial) {
    if (!values.is_object() || (!partial && values.size() != 2))
        throw SettingsError("invalid-settings", "Settings must contain theme and reducedMotion");
    for (const auto& item : values.items()) {
        if (item.key() == "theme") {
            if (!item.value().is_string() || (item.value() != "dark" && item.value() != "light"))
                throw SettingsError("invalid-settings", "Theme must be dark or light");
        } else if (item.key() == "reducedMotion") {
            if (!item.value().is_boolean()) throw SettingsError("invalid-settings", "reducedMotion must be a boolean");
        } else throw SettingsError("invalid-settings", "Unknown setting");
    }
}
}
SettingsError::SettingsError(std::string kind, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(kind)) {}
SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) {}
std::filesystem::path SettingsStore::file_path_() const {
    if (!file_.empty()) return file_;
    const auto base = user_config_directory();
    if (base.empty()) throw SettingsError("settings-unavailable", "A writable user settings location is unavailable");
    return base / "meditor/settings.json";
}
Json SettingsStore::read_() const {
    const auto file = file_path_();
    if (!std::filesystem::exists(file)) return defaults();
    try {
        auto stored = Json::parse(read_file(file, 64 * 1024));
        if (!stored.is_object() || stored.size() != 3 || stored.value("schemaVersion", 0) != 1 ||
            !stored.contains("revision") || !stored["revision"].is_number_unsigned() ||
            !stored.contains("values")) throw SettingsError("invalid-settings", "Unsupported settings file; original file was preserved");
        validate(stored.at("values"), false);
        stored.erase("schemaVersion");
        return stored;
    } catch (const nlohmann::json::exception&) {
        throw SettingsError("invalid-settings", "Unreadable settings file; original file was preserved");
    }
}
Json SettingsStore::get() const { return read_(); }
Json SettingsStore::update(std::uint64_t revision, const Json& changes) {
    validate(changes, true);
    const auto file = file_path_();
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto lockFile = file; lockFile += ".lock";
    FileLock lock(lockFile);
    auto current = read_();
    if (current.at("revision").get<std::uint64_t>() != revision)
        throw SettingsError("settings-conflict", "Settings changed; reload before updating");
    auto next = current;
    for (const auto& item : changes.items()) next["values"][item.key()] = item.value();
    if (next == current) return current;
    if (revision == std::numeric_limits<std::uint64_t>::max())
        throw SettingsError("settings-conflict", "Settings revision limit reached");
    next["revision"] = revision + 1;
    auto stored = next; stored["schemaVersion"] = 1;
    write_file_atomic(file, stored.dump(2) + "\n");
    return next;
}
}

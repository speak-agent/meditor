// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.i18n;
import meditor.settings;
import meditor.platform;

namespace {
void require(bool condition) { if (!condition) throw std::runtime_error("Localization contract failed"); }
template<class Function> void rejects(Function function) {
    bool failed {}; try { function(); } catch (const std::exception&) { failed = true; } require(failed);
}
}
int main() {
    using namespace meditor::i18n;
    for (const auto* tag : {"zh", "zh-CN", "zh_CN.UTF-8", "ZH_hans", "zh-Hans-CN@variant"}) require(normalize_language(tag) == "zh");
    for (const auto* tag : {"", "C", "POSIX", "en_US.UTF-8", "fr-FR", "zhang"}) require(normalize_language(tag) == "en");
    require(select_language("auto", "zh-CN") == "zh");
    require(select_language("en", "zh-CN") == "en");
    require(select_language("zh", "fr") == "zh");
    require(format_message("{{{name}}} {name}", {{"name", "世界🙂 {literal}"}}) == "{世界🙂 {literal}} 世界🙂 {literal}");
    rejects([] { format_message("{missing}"); });
    rejects([] { format_message("literal", {{"unused", "value"}}); });
    rejects([] { format_message("{"); }); rejects([] { format_message("}"); });
    rejects([] { format_message("{}", {{"", "value"}}); });
    Localizer fallback("zh", {{"known", "Hello {name}"}, {"empty", "English fallback"}}, {{"empty", ""}});
    require(fallback.text("known", {{"name", "🙂"}}) == "Hello 🙂");
    require(fallback.text("empty") == "English fallback");
    require(fallback.text("unknown.id") == "unknown.id" && !fallback.contains("unknown.id"));
    Localizer english("en"), chinese("zh");
    require(english.language() == "en" && chinese.language() == "zh");
    require(english.text("action.save") == "Save" && chinese.text("action.save") == "保存");
    require(english.message_ids() == chinese.message_ids());
    for (const auto& id : english.message_ids()) {
        MessageArguments arguments;
        if (id == "settings.languageCurrent") arguments["language"] = "English";
        if (id == "search.matches" || id == "search.replaced") arguments["count"] = "2";
        if (id == "language.position") { arguments["line"] = "2"; arguments["column"] = "12"; }
        require(!english.text(id, arguments).empty() && !chinese.text(id, arguments).empty());
    }
    require(english.text("language.position", {{"line", "2"}, {"column", "12"}}) == "Line 2, column 12");
    require(chinese.text("language.position", {{"line", "2"}, {"column", "12"}}) == "第 2 行，第 12 列");
    const auto root = std::filesystem::temp_directory_path() / ("meditor-i18n-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    const auto file = root / "settings.json";
    const std::string legacy = R"({"schemaVersion":1,"revision":7,"values":{"theme":"dark","reducedMotion":false}})";
    meditor::write_file_atomic(file, legacy);
    meditor::SettingsStore settings(file);
    const auto read = settings.get();
    require(read["values"]["language"] == "auto" && read["revision"] == 7);
    require(meditor::read_file(file) == legacy);
    require(settings.update(7, {{"language", "auto"}})["revision"] == 7);
    require(meditor::read_file(file) == legacy);
    const auto changed = settings.update(7, {{"language", "zh"}});
    require(changed["revision"] == 8 && changed["values"]["language"] == "zh");
    const auto checkpoint = meditor::read_file(file);
    rejects([&] { settings.update(8, {{"language", "unsupported"}}); });
    require(meditor::read_file(file) == checkpoint);
    require(english.text("action.save") == "Save"); // Independent contexts never share language state.
    std::println("Locale selection, catalogs, fallback, formatting and settings migration: passed");
}

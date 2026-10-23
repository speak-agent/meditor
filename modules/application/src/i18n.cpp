// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
module meditor.i18n;
import std;
import meditor.i18n.catalogs;

namespace meditor::i18n {
std::string normalize_language(std::string_view tag) {
    std::string normalized;
    for (const unsigned char character : tag) {
        if (character == '.' || character == '@') break;
        normalized += character == '_' ? '-' : character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character;
    }
    return normalized == "zh" || normalized.starts_with("zh-") ? "zh" : "en";
}
std::string select_language(std::string_view preference, std::string_view systemLanguage) {
    return normalize_language(preference.empty() || preference == "auto" ? systemLanguage : preference);
}
std::string format_message(std::string_view pattern, const MessageArguments& arguments) {
    std::string result;
    std::set<std::string, std::less<>> used;
    for (std::size_t i = 0; i < pattern.size();) {
        const auto ch = pattern[i++];
        if (ch != '{' && ch != '}') { result += ch; continue; }
        if (i < pattern.size() && pattern[i] == ch) { result += ch; ++i; continue; }
        if (ch == '}') throw std::invalid_argument("Unmatched message delimiter");
        const auto end = pattern.find('}', i);
        if (end == std::string_view::npos || end == i) throw std::invalid_argument("Invalid message argument");
        const auto name = pattern.substr(i, end - i);
        for (const auto character : name)
            if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') || character == '_'))
                throw std::invalid_argument("Invalid message argument name");
        const auto argument = arguments.find(name);
        if (argument == arguments.end()) throw std::invalid_argument("Missing message argument");
        result += argument->second; used.insert(argument->first); i = end + 1;
    }
    if (used.size() != arguments.size()) throw std::invalid_argument("Unused message argument");
    return result;
}
struct Localizer::Catalogs { MessageCatalog english, chinese; };
Localizer::Localizer(std::string_view language) : language_(normalize_language(language)) {
    static const auto builtIn = [] {
        auto catalogs = std::make_shared<Catalogs>();
        for (const auto& entry : detail::ENTRIES) {
            catalogs->english.emplace(entry.id, entry.english);
            catalogs->chinese.emplace(entry.id, entry.chinese);
        }
        return std::shared_ptr<const Catalogs>(std::move(catalogs));
    }();
    catalogs_ = builtIn;
}
Localizer::Localizer(std::string_view language, MessageCatalog english, MessageCatalog chinese)
    : catalogs_(std::make_shared<const Catalogs>(Catalogs{std::move(english), std::move(chinese)})), language_(normalize_language(language)) {}
std::string_view Localizer::language() const { return language_; }
bool Localizer::contains(std::string_view id) const { return catalogs_->english.contains(id); }
std::string Localizer::text(std::string_view id, const MessageArguments& arguments) const {
    const auto* catalog = &catalogs_->english;
    if (language_ == "zh") {
        const auto found = catalogs_->chinese.find(id);
        if (found != catalogs_->chinese.end() && !found->second.empty()) catalog = &catalogs_->chinese;
    }
    const auto found = catalog->find(id);
    return format_message(found == catalog->end() ? id : std::string_view(found->second), arguments);
}
std::vector<std::string> Localizer::message_ids() const {
    std::vector<std::string> result;
    for (const auto& entry : catalogs_->english) result.push_back(entry.first);
    return result;
}
}

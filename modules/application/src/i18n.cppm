// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
export module meditor.i18n;
import std;

export namespace meditor::i18n {
using MessageCatalog = std::map<std::string, std::string, std::less<>>;
using MessageArguments = std::map<std::string, std::string, std::less<>>;
std::string normalize_language(std::string_view tag);
std::string select_language(std::string_view preference, std::string_view systemLanguage);
std::string format_message(std::string_view pattern, const MessageArguments& arguments = {});
class Localizer {
    struct Catalogs;
    std::shared_ptr<const Catalogs> catalogs_;
    std::string language_;
public:
    explicit Localizer(std::string_view language = "en");
    Localizer(std::string_view language, MessageCatalog english, MessageCatalog chinese);
    std::string_view language() const;
    bool contains(std::string_view id) const;
    std::string text(std::string_view id, const MessageArguments& arguments = {}) const;
    std::vector<std::string> message_ids() const;
};
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import nlohmann.json;
import meditor.about;
import meditor.about.resources;
import meditor.platform;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("About metadata contract failed at line " + std::to_string(location.line()));
}
}
int main() {
    using Json = nlohmann::json;
    const auto root = std::filesystem::temp_directory_path() / ("meditor-about-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    auto missing = meditor::about::describe(root);
    require(missing["product"]["license"] == "GPL-3.0-only");
    require(missing["product"]["licenseText"].get<std::string>().contains("GNU GENERAL PUBLIC LICENSE"));
    require(missing["product"]["contributors"] == "Sunrisepeak & mcpp-community");
    require(missing["libraries"][0]["id"] == "HuxerUI" && missing["libraries"][0]["license"] == "MIT");
    require(missing["libraries"][1]["id"] == "nlohmann.json" && missing["libraries"][1]["version"] == "3.12.0");
    for (const auto& tool : missing["tools"]) require(tool["state"] == "missing" && tool["version"].is_null());
#if defined(_WIN32)
    const auto suffix = ".exe";
#else
    const auto suffix = "";
#endif
    std::filesystem::create_directories(root / "mcpp/bin");
    meditor::write_file_atomic(root / "mcpp/bin" / (std::string("mcpp") + suffix), "synthetic executable; never run");
    auto unverified = meditor::about::describe(root);
    for (const auto& tool : unverified["tools"]) if (tool["id"] == "mcpp") require(tool["state"] == "unverified" && tool["version"].is_null());
    const auto embedded = Json::parse(meditor::about::detail::METADATA);
    require(!embedded["expectedSources"].is_null()); // This test runs in the native CI matrix.
    Json receipt {{"schemaVersion", 1}, {"target", embedded["payloadTarget"]}, {"sources", embedded["expectedSources"]}};
    meditor::write_file_atomic(root / "inventory.json", receipt.dump());
    meditor::write_file_atomic(root / "mcpp/LICENSE", "synthetic tool notice");
    const auto recorded = meditor::about::describe(root);
    for (const auto& tool : recorded["tools"]) if (tool["id"] == "mcpp") {
        require(tool["state"] == "recorded" && tool["version"] == tool["expectedVersion"]);
        require(tool["licenseText"] == "synthetic tool notice" && tool["integrity"] == "not-rechecked");
    }
    receipt["sources"] = Json::object(); meditor::write_file_atomic(root / "inventory.json", receipt.dump());
    const auto changed = meditor::about::describe(root);
    for (const auto& tool : changed["tools"]) if (tool["id"] == "mcpp") require(tool["state"] == "unverified" && tool["version"].is_null());
    const auto declarations = meditor::about::licenses(root);
    require(!declarations.at("complete").get<bool>() && !declarations.at("releaseReady").get<bool>());
    require(declarations["components"][0]["licenseText"] == missing["product"]["licenseText"]);
    require(declarations.dump().find(root.filename().string()) == std::string::npos);
#if !defined(_WIN32)
    const auto confined = root / "confined";
    std::filesystem::create_directories(confined / "mcpp/bin");
    meditor::write_file_atomic(confined / "mcpp/bin/mcpp", "synthetic executable");
    receipt["sources"] = embedded["expectedSources"];
    meditor::write_file_atomic(confined / "inventory.json", receipt.dump());
    meditor::write_file_atomic(root / "outside-notice", "private synthetic notice sentinel");
    std::filesystem::create_symlink(root / "outside-notice", confined / "mcpp/LICENSE");
    auto escaped = meditor::about::describe(confined);
    for (const auto& tool : escaped["tools"]) if (tool["id"] == "mcpp") require(tool["licenseText"].is_null() && tool["noticeStatus"] == "pending");
    std::filesystem::remove(confined / "mcpp/bin/mcpp");
    std::filesystem::create_symlink(root / "outside-notice", confined / "mcpp/bin/mcpp");
    escaped = meditor::about::describe(confined);
    for (const auto& tool : escaped["tools"]) if (tool["id"] == "mcpp") require(tool["state"] == "unverified" && tool["version"].is_null());
    require(escaped.dump().find("private synthetic notice sentinel") == std::string::npos);
#endif
    std::println("Offline notices distinguish recorded, missing and unverified tools without executing them: passed");
}

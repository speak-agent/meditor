// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import nlohmann.json;
import meditor.build.metadata;

namespace {
using Json = nlohmann::json;
void require(bool value) { if (!value) throw std::runtime_error("Embedded metadata contract failed"); }
std::string read(const std::filesystem::path& file) { std::ifstream input(file, std::ios::binary); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}; }
void write(const std::filesystem::path& file, const std::string& value) { std::ofstream output(file, std::ios::binary); output << value; }
template<class Function> void rejects(Function function) { bool failed {}; try { function(); } catch (const std::exception&) { failed = true; } require(failed); }
}
int main() {
    auto repository = std::filesystem::current_path();
    while (!std::filesystem::is_regular_file(repository / "resources/product.json")) {
        const auto parent = repository.parent_path();
        require(!parent.empty() && parent != repository); repository = parent;
    }
    const auto root = std::filesystem::temp_directory_path() / ("meditor-build-metadata-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "resources/licenses"); std::filesystem::create_directories(root / "packaging");
    std::filesystem::create_directories(root / "resources/i18n");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    for (const auto* name : {"LICENSE", "LICENSING.md", "mcpp.lock", "resources/product.json", "resources/i18n/en.json", "resources/licenses/manifest.json", "resources/licenses/HuxerUI-LICENSE.txt", "resources/licenses/nlohmann-json-LICENSE.txt", "packaging/tools.json"})
        std::filesystem::copy_file(repository / name, root / name);
    const std::string revision(40, '1');
    const auto output = meditor::build::compile_metadata(root, root / "generated", "0.0.1", "x86_64-linux-gnu", revision);
    const auto source = read(output);
    const auto start = source.find("METADATA = ") + 11, end = source.rfind("; }");
    const auto data = Json::parse(Json::parse(source.substr(start, end - start)).get<std::string>());
    require(data["product"]["sourceCommit"] == revision);
    require(data["product"]["sourceUrl"] == "https://github.com/speak-agent/meditor/tree/" + revision);
    require(data.dump().find(root.filename().string()) == std::string::npos);
    require(data["libraries"].size() == 2 && !data["completeDistributionNotices"].get<bool>());
    rejects([&] { meditor::build::compile_metadata(root, root / "generated", "0.0.1", "x86_64-linux-gnu", "not-a-commit"); });
    auto product = Json::parse(read(root / "resources/product.json")); product["developmentPath"] = "private fixture";
    write(root / "resources/product.json", product.dump());
    rejects([&] { meditor::build::compile_metadata(root, root / "generated", "0.0.1", "x86_64-linux-gnu"); });
    std::filesystem::copy_file(repository / "resources/product.json", root / "resources/product.json", std::filesystem::copy_options::overwrite_existing);
    auto notices = Json::parse(read(root / "resources/licenses/manifest.json")); notices["components"][0]["revision"] = std::string(40, '2');
    write(root / "resources/licenses/manifest.json", notices.dump());
    rejects([&] { meditor::build::compile_metadata(root, root / "generated", "0.0.1", "x86_64-linux-gnu"); });
    std::filesystem::copy_file(repository / "resources/licenses/manifest.json", root / "resources/licenses/manifest.json", std::filesystem::copy_options::overwrite_existing);
    auto tools = Json::parse(read(root / "packaging/tools.json")); tools["tools"]["mcpp"]["version"] = "999.0.0";
    write(root / "packaging/tools.json", tools.dump());
    rejects([&] { meditor::build::compile_metadata(root, root / "generated", "0.0.1", "x86_64-linux-gnu"); });
    require(read(output) == source);
    std::println("Metadata generation rejects stale notices and private/unknown fields while preserving valid output: passed");
}

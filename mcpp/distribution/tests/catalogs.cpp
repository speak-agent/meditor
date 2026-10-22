// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.build.catalogs;

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Catalog generation contract failed"); }
void write(const std::filesystem::path& file, std::string_view content) { std::ofstream output(file, std::ios::binary); output << content; }
template<class Function> void rejects(Function function) { bool failed {}; try { function(); } catch (const std::exception&) { failed = true; } require(failed); }
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("meditor-catalogs-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup {root};
    write(root / "en.json", R"({"greeting":"Hello \"world\"\n"})");
    write(root / "zh.json", R"({"greeting":"你好世界🙂"})");
    const auto output = meditor::build::compile_catalogs(root, root / "generated");
    std::ifstream file(output, std::ios::binary);
    const std::string source {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}; file.close();
    require(source.contains("export module meditor.i18n.catalogs;"));
    require(source.contains("你好世界🙂") && source.contains(R"(Hello \"world\"\n)"));
    std::filesystem::last_write_time(output, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
    const auto checkpoint = std::filesystem::last_write_time(output);
    meditor::build::compile_catalogs(root, root / "generated");
    require(std::filesystem::last_write_time(output) == checkpoint);
    for (const auto* invalid : {"{}", R"({"different":"value"})", R"({"greeting":""})", R"({"greeting":false})", R"({"greeting":"hidden\u0000text"})", R"({"bad{key}":"text"})", "not json"}) {
        write(root / "zh.json", invalid);
        rejects([&] { meditor::build::compile_catalogs(root, root / "generated"); });
    }
    std::println("Embedded catalogs preserve UTF-8, reject incomplete tables and avoid unchanged rewrites: passed");
}

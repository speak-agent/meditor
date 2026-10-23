// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
// Insertion benchmark smoke (R10): large-file open/edit/save/find timings through the real
// application, earlier than release candidates. Assertions are loose regression guards; the
// printed P50/P95/max series is the measurement. The §10.4 GUI budgets are measured separately.
import std;
import meditor.application;
import meditor.platform;

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Performance smoke contract failed"); }
std::vector<double> percentiles(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto pick = [&](double fraction) { return samples[std::min(samples.size() - 1, static_cast<std::size_t>(samples.size() * fraction))]; };
    return {pick(0.5), pick(0.95), samples.back()};
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("meditor-perf-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup {root};
    // ~10k lines of real-shaped module code with mixed identifier widths.
    std::string large;
    large.reserve(420 * 1024);
    for (int i = 0; i < 10000; ++i)
        large += "export int measured_value_" + std::to_string(i) + "() { return " + std::to_string(i) + " + computed_helper(" + std::to_string(i % 97) + "); }\n";
    meditor::write_file_atomic(root / "large.cppm", large);
    meditor::Application app(root, {}, {}, {.dataRoot = root / "runtime"});
    const auto trusted = app.execute({{"command", "workspace.trust"}, {"params", {{"allow", true}}}});
    require(trusted.value("ok", false));

    const auto stamp = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    auto command = [&](const std::string& name, const meditor::Json& params) { meditor::Json request = meditor::Json::object(); request["command"] = name; request["params"] = params; return app.execute(request); };

    const auto openStart = stamp();
    const auto opened = command("document.open", {{"path", "large.cppm"}});
    const auto openMs = stamp() - openStart;
    require(opened.value("ok", false));
    const auto document = opened["data"]["document"].get<std::string>();
    std::uint64_t revision = opened["data"]["revision"].get<std::uint64_t>();

    std::vector<double> edits, saves, finds;
    for (int i = 0; i < 100; ++i) {
        const auto start = stamp();
        meditor::Json editParams = meditor::Json::object();
        editParams["document"] = document; editParams["revision"] = revision;
        editParams["edits"] = meditor::Json::array({{{"start", 100 + i}, {"end", 100 + i}, {"text", "x"}}});
        const auto edited = command("document.edit", editParams);
        const auto editMs = stamp() - start;
        require(edited.value("ok", false));
        revision = edited["data"]["revision"].get<std::uint64_t>();
        edits.push_back(editMs);
    }
    const auto saveStart = stamp();
    const auto saved = command("document.save", {{"document", document}, {"revision", revision}});
    const auto saveMs = stamp() - saveStart;
    if (!saved.value("ok", false)) std::println(std::cerr, "save failed: {}", saved.dump().substr(0, 200));
    require(saved.value("ok", false));
    for (int i = 0; i < 20; ++i) {
        const auto start = stamp();
        const auto found = command("document.find", {{"document", document}, {"revision", revision}, {"query", "measured_value_" + std::to_string(i * 17)}, {"coordinateSpace", "display"}});
        const auto findMs = stamp() - start;
        if (!found.value("ok", false)) std::println(std::cerr, "find {} failed: {}", i, found.dump().substr(0, 200));
        require(found.value("ok", false));
        finds.push_back(findMs);
    }

    const auto report = [](const char* name, const std::vector<double>& series) {
        const auto stats = percentiles(series);
        std::println("{}: n={} p50={:.1f}ms p95={:.1f}ms max={:.1f}ms", name, series.size(), stats[0], stats[1], stats[2]);
        return stats;
    };
    std::println("open 10k-line module: {:.1f}ms", openMs);
    std::println("save 10k-line module: {:.1f}ms", saveMs);
    const auto editStats = report("edit (single char)", edits);
    const auto findStats = report("find (identifier)", finds);
    require(openMs < 5000.0);
    require(saveMs < 5000.0);
    require(editStats[1] < 500.0);
    require(findStats[1] < 1000.0);
    std::println("Performance smoke: large-file open/edit/save/find within regression guards: passed");
}

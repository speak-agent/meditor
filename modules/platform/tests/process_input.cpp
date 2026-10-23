// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.platform;
namespace {
using Clock = std::chrono::steady_clock;
void require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Process input contract failed at line " + std::to_string(at.line()));
}
void ready(meditor::Process& process) {
    std::string output; const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!output.contains("ready") && Clock::now() < deadline) output += process.read(std::chrono::milliseconds(20)).data;
    require(output.contains("ready"));
}
}
int run(int argc, char** argv) {
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--blocked") {
            std::println("ready"); std::cout.flush(); std::this_thread::sleep_for(std::chrono::seconds(60)); return 0;
        }
        if (std::string_view(argv[1]) == "--reader") {
            std::array<char, 65536> buffer; std::size_t bytes {};
            while (std::cin) { std::cin.read(buffer.data(), buffer.size()); bytes += static_cast<std::size_t>(std::cin.gcount()); }
            std::println("{}", bytes); return 0;
        }
        return 0;
    }
    const auto executable = meditor::path_to_utf8(std::filesystem::absolute(argv[0]));
    const auto root = std::filesystem::temp_directory_path() / ("meditor-pipe-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
    const std::string payload(2 * 1024 * 1024, 'x');
    for (bool cancel : {false, true}) {
        meditor::Process process; process.start({executable, "--blocked"}, root); ready(process);
        std::stop_source stop;
        std::jthread canceller;
        if (cancel) canceller = std::jthread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(75)); stop.request_stop(); });
        const auto start = Clock::now(); bool rejected {};
        try { process.write(payload, cancel ? std::chrono::seconds(5) : std::chrono::milliseconds(150), stop.get_token()); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && Clock::now() - start < std::chrono::seconds(5));
        require(!process.poll());
        rejected = false; try { process.write("x"); } catch (const std::exception&) { rejected = true; } require(rejected);
        meditor::write_file_atomic(root / "after.cppm", "unaffected"); // Cancellation must end before subsequent I/O on this thread.
        require(meditor::read_file(root / "after.cppm") == "unaffected");
        process.terminate();
    }
    meditor::Process reader; reader.start({executable, "--reader"}, root); reader.write(payload); reader.close_input();
    std::string output; const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < deadline) { auto read = reader.read(std::chrono::milliseconds(20)); output += read.data; if (read.ended) break; }
    std::erase(output, '\r');
    require(output == std::to_string(payload.size()) + "\n");
    std::println("Bounded process writes handle backpressure, timeout, cancellation, later I/O and complete delivery: passed");
    return 0;
}
int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::println("{}", error.what()); return 1; }
}

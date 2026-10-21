// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.platform;
void require(bool value) { if (!value) throw std::runtime_error("Privacy assertion failed"); }
int main() {
    auto value = meditor::redact("error /home/synthetic/work/a.cpp C:\\Users\\synthetic\\work\\a.cpp token=abc123 https://name:secret@example.invalid/path?password=hidden");
    require(value.find("synthetic") == std::string::npos);
    require(value.find("abc123") == std::string::npos);
    require(value.find("secret") == std::string::npos);
    require(value.find("hidden") == std::string::npos);
    auto root = std::filesystem::temp_directory_path() / ("meditor-test-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directory(root);
    try {
        auto file = root / "unicode-name.txt";
        meditor::write_file_atomic(file, "你好🙂\r\n");
        require(meditor::read_file(file) == "你好🙂\r\n");
        meditor::write_file_atomic(file, "replacement");
        require(meditor::read_file(file) == "replacement");
        require(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1);
    } catch (...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
    std::println("privacy and file replacement: passed");
}

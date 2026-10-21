import std;
import meditor.platform;

int main() {
    auto directory = std::filesystem::temp_directory_path() / ("meditor-lock-test-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directory(directory);
    try {
        const auto file = directory / "settings.lock";
        {
            meditor::FileLock first(file);
            bool rejected {};
            try { meditor::FileLock second(file); } catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Competing file lock was accepted");
        }
        { meditor::FileLock afterRelease(file); }
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("Lock inode must survive release");
    } catch (...) { std::filesystem::remove_all(directory); throw; }
    std::filesystem::remove_all(directory);
    std::println("Exclusive file locks release and preserve their inode: passed");
}

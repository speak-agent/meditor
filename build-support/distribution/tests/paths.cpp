import std;
import nlohmann.json;
import meditor.distribution;

int main() {
    using Json = nlohmann::json;
    auto temporary = std::filesystem::temp_directory_path() / ("meditor-payload-plan-" + std::to_string(std::random_device{}()));
    auto directory = temporary / "payload";
    std::filesystem::create_directories(directory);
    try {
        auto files = Json::array();
        for (const auto& tool : {"mcpp", "mcppls"}) {
            auto name = std::format("tools/{0}/bin/{0}", tool);
            auto source = directory / name; std::filesystem::create_directories(source.parent_path());
            { std::ofstream out(source); out << 'x'; }
            files.push_back({{"path", name}, {"bytes", 1}});
        }
        { std::ofstream out(directory / "outside"); out << 'x'; }
        { std::ofstream out(temporary / "secret"); out << 'x'; }
        auto receipt = Json{{"schemaVersion", 1}, {"target", "x86_64-linux-gnu"}, {"files", files}};
        auto write = [&](const Json& value) { std::ofstream out(directory / "inventory.json"); out << value; };
        auto reject = [&] {
            bool rejected = false;
            try { meditor::distribution::plan(directory, "x86_64-linux-gnu", true); } catch (...) { rejected = true; }
            if (!rejected) throw std::runtime_error("Unsafe payload was accepted");
        };
        write(receipt);
        if (meditor::distribution::plan(directory, "x86_64-linux-gnu", true).files.size() != 3) throw std::runtime_error("Valid payload plan failed");
        for (const auto& name : {"tools/../outside", "tools/../../secret", "tools/C:/secret", "tools/driver\\secret"}) {
            auto invalid = receipt; invalid["files"].push_back({{"path", name}, {"bytes", 1}}); write(invalid); reject();
        }
        auto duplicate = receipt; duplicate["files"].push_back(files[0]); write(duplicate); reject();
        auto changed = receipt; changed["files"][0]["bytes"] = 99; write(changed); reject();
        std::filesystem::remove(directory / "inventory.json"); reject();
        if (!meditor::distribution::plan(directory, "x86_64-linux-gnu", false).files.empty()) throw std::runtime_error("Development payload policy failed");
    } catch (...) { std::filesystem::remove_all(temporary); throw; }
    std::filesystem::remove_all(temporary);
    std::println("Payload confinement, duplicate/changed files and missing-tool policy: passed");
}

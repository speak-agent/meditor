// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.platform;

namespace {
void require(bool value, std::source_location location = std::source_location::current()) { if (!value) throw std::runtime_error("Channel contract failed at line " + std::to_string(location.line())); }
}
int main() {
    using namespace meditor;
    const auto address = local_channel_address("contract-" + std::to_string(std::random_device{}()));
    {
        ChannelListener listener(address);
        auto client = open_channel(address, std::chrono::seconds(2));
        require(client != nullptr);
        require(client->write_line(R"({"hello":"meditor"})"));
        auto connection = listener.accept(std::chrono::seconds(2));
        require(connection != nullptr);
        auto received = connection->read_line(std::chrono::seconds(2));
        require(received == std::optional<std::string>(R"({"hello":"meditor"})"));
        require(connection->write_line("answer"));
        auto reply = client->read_line(std::chrono::seconds(2));
        require(reply == std::optional<std::string>("answer"));
        require(!client->ended());
        require(client->read_line(std::chrono::milliseconds(50)) == std::nullopt);
        require(!client->ended());
        client->close();
        require(client->ended());
        auto closed = connection->read_line(std::chrono::seconds(2));
        require(closed == std::nullopt && connection->ended());
        bool refused {};
        try { ChannelListener second(address); } catch (const std::exception&) { refused = true; }
        require(refused);
    }
    std::error_code ignored;
    require(!std::filesystem::exists(std::filesystem::u8path(address), ignored));
    {
        ChannelListener listener(address);
        require(listener.accept(std::chrono::milliseconds(60)) == nullptr);
        auto client = open_channel(local_channel_address("contract-absent-" + std::to_string(std::random_device{}())), std::chrono::milliseconds(300));
        require(client == nullptr);
    }
    std::println("Line-framed local channels: round trip, timeouts, exclusive address and cleanup: passed");
}

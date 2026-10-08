#include "../../src/CoopNet/GnsTransport.h"
#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <string>
using namespace coopnet;
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("Usage: GnsSmoke host|client endpoint-or-port");
        GnsRuntime runtime;
        const bool host_mode = std::string(argv[1]) == "host";
        HostPump host; ClientPump client;
        bool snapshot_received = false, admitted = false, input_received = false;
        client.set_snapshot_sink([&](const ActorSnapshot& snapshot) {
            if (snapshot.entity != 5 || snapshot.level != 10 || snapshot.position[0] != 42)
                throw std::runtime_error("Snapshot mismatch");
            snapshot_received = snapshot.movement == 1; // host acknowledgement after input validation
        });
        if (host_mode) {
            const auto port = std::stoul(argv[2]);
            if (port == 0 || port > 65535 || !runtime.listen(static_cast<std::uint16_t>(port)))
                throw std::runtime_error("Listen failed");
            // Fixed token is confined to this standalone test.
            host.start(10, 1, {1,1}, [] { return Identity{123}; });
            if (!host.create_actor({5,1,1,1,10})) throw std::runtime_error("Actor binding failed");
            std::cout << "LISTENING\n" << std::flush;
        } else {
            if (std::string(argv[1]) != "client") throw std::invalid_argument("Invalid mode");
            const auto connection = runtime.connect(argv[2]);
            if (connection == k_HSteamNetConnection_Invalid) throw std::runtime_error("Connect failed");
            client.start(std::make_unique<GnsTransport>(runtime, connection), 2, {1,1});
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        auto previous = std::chrono::steady_clock::now();
        std::uint32_t tick = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count(); previous = now;
            runtime.poll();
            if (host_mode) {
                const auto pending = runtime.take_pending();
                if (pending != k_HSteamNetConnection_Invalid &&
                    !host.attach(pending, std::make_unique<GnsTransport>(runtime, pending)))
                    throw std::runtime_error("Attach failed");
                host.update(elapsed);
                const auto& guest = host.session().players()[1];
                if (guest.connected && !admitted && host.assign_level(guest.id, 10, 456)) {
                    if (!host.create_actor({20,guest.id,guest.character,1,10}))
                        throw std::runtime_error("Guest input binding failed");
                    admitted = true;
                }
                ActorInput input;
                if (host.latest_input(guest.id,input)) {
                    if (input.entity != 20 || input.buttons != 1 || input.yaw != .25f)
                        throw std::runtime_error("Input mismatch");
                    input_received = true;
                }
                if (guest.connected && admitted) {
                    ActorSnapshot snapshot{5, 1, 10, ++tick, static_cast<std::uint64_t>(tick) * 5000,
                        {42,0,0}, {0,0,0}, {0,0,0}, static_cast<std::uint16_t>(input_received ? 1 : 0), 0};
                    if (!host.publish_snapshot(snapshot)) throw std::runtime_error("Snapshot publish failed");
                }
                if (admitted && input_received && !guest.connected) {
                    std::cout << "HOST_PASS admission, validated guest input, level-filtered snapshots and disconnect\n"; return 0;
                }
            } else {
                client.update(elapsed);
                if (client.assignment().ticket) client.acknowledge_level(10);
                if (client.actors().find(20)) client.send_input({20,1,10,++tick,1,.25f,0});
                if (snapshot_received && client.roster() && client.roster()->current().participants.size() == 2) {
                    std::cout << "CLIENT_PASS reliable session/roster, guest input and host acknowledgement snapshot\n";
                    client.stop(); return 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("Connection or message timeout");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

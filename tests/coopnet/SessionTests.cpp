#include "../../src/CoopNet/Session.h"
#include "../../src/CoopNet/Transport.h"
#include "../../src/CoopNet/Roster.h"
#include "../../src/CoopNet/ClientPump.h"
#include "../../src/CoopNet/HostPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value, int line) {
    if (!value) { std::cerr << "Session test failed at line " << line << '\n'; std::exit(1); }
}
#define require(value) check((value), __LINE__)
int main() {
    HostSession host;
    ClientHello hello{protocol_version, {10,20}, 100,0,0,0};
    require(host.admit(1, hello, 1000).result == Admission::WrongMode);
    host.start(99, 50, {10,20});
    auto endpoints = MemoryTransport::pair();
    Frame frame{Message::ClientHello, Channel::Control, Delivery::ReliableOrdered, 1, encode_hello(hello)};
    require(endpoints.first->send(frame) == SendResult::Sent);
    Frame received{}; ClientHello decoded;
    require(endpoints.second->receive(received) && decode_hello(received.payload, decoded));
    auto welcome = host.admit(7, decoded, 1000);
    require(welcome.result == Admission::Accepted && welcome.player == 2 && welcome.session == 99);
    require(host.admit(7, hello, 2000).result == Admission::DuplicateConnection);
    require(host.admit(8, hello, 2000).result == Admission::DuplicateCharacter);
    hello.character = 101; hello.version = protocol_version + 1;
    require(host.admit(8, hello, 2000).result == Admission::VersionMismatch);
    hello.version = protocol_version; hello.build.mods = 21;
    require(host.admit(8, hello, 2000).result == Admission::BuildMismatch);
    hello.build.mods = 20; require(host.admit(8, hello, 2000).result == Admission::Accepted);
    hello.character = 102; require(host.admit(9, hello, 3000).result == Admission::Accepted);
    hello.character = 103; require(host.admit(10, hello, 4000).result == Admission::Full);
    require(host.disconnect(7) && !host.disconnect(7));
    hello.character = 100; hello.resume_session = 99; hello.resume_player = 2; hello.resume_token = 999;
    require(host.admit(11, hello, 0).result == Admission::InvalidResume);
    hello.resume_token = 1000; welcome = host.admit(11, hello, 0);
    require(welcome.result == Admission::Accepted && welcome.player == 2 && welcome.generation == 2);
    require(!host.release(2) && !host.release(1));
    require(host.disconnect(11) && host.release(2));
    hello = ClientHello{protocol_version, {10,20}, 103,0,0,0};
    require(host.admit(12, hello, 4000).player == 5);
    auto bytes = encode_hello(hello);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        require(!decode_hello(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n), decoded));
    bytes.push_back(0); require(!decode_hello(bytes, decoded));
    for (unsigned i = 0; i < 64; ++i) require(endpoints.first->send(frame) == SendResult::Sent);
    require(endpoints.first->send(frame) == SendResult::Backpressure);
    require(endpoints.second->receive(received)); require(endpoints.first->send(frame) == SendResult::Sent);
    endpoints.second->close(); require(!endpoints.first->connected());
    require(endpoints.first->send(frame) == SendResult::Disconnected && !endpoints.first->receive(received));
    auto large = MemoryTransport::pair(); frame.payload.assign(max_payload - 16, 0);
    for (unsigned i = 0; i < 16; ++i) require(large.first->send(frame) == SendResult::Sent);
    require(large.first->send(frame) == SendResult::Backpressure);
    host.stop(); require(host.mode() == Mode::Offline && host.players()[0].id == 0);
    require(!host.release(0));
    ClientSession client;
    host.start(199, 50, {10,20});
    auto request = client.begin(100, {10,20});
    require(client.state() == ClientState::Connecting);
    auto reply = host.admit(20, request, 5000);
    auto wire = encode_welcome(reply);
    require(!client.accept({wire.begin(), wire.end() - 1}));
    require(client.state() == ClientState::Connecting);
    require(client.accept(wire) && client.state() == ClientState::Connected);
    require(!client.accept(wire));
    require(host.disconnect(20)); client.lost_connection();
    request = client.reconnect();
    require(!client.accept(wire)); // old generation cannot complete reconnect
    reply = host.admit(21, request, 0);
    require(client.accept(encode_welcome(reply)) && client.welcome().generation == 2);
    client.stop(); request = client.begin(100, {10,21});
    reply = host.admit(22, request, 6000);
    require(client.accept(encode_welcome(reply)) && client.state() == ClientState::Rejected);
    Welcome invalid{Admission::Accepted, 0, 0, 0, 0};
    require(!decode_welcome(encode_welcome(invalid), reply));
    auto roster = make_roster(host, 199, 1);
    ClientRoster view(199, 2);
    auto roster_wire = encode_roster(roster);
    require(view.apply(roster_wire) && view.current().participants.size() == 2);
    require(!view.apply(roster_wire));
    for (std::size_t n = 0; n < roster_wire.size(); ++n) {
        Roster parsed;
        require(!decode_roster({roster_wire.begin(), roster_wire.begin() + n}, parsed));
    }
    auto bad_roster = roster_wire; bad_roster[12] = 5;
    Roster parsed; require(!decode_roster(bad_roster, parsed));
    bad_roster = roster_wire; bad_roster.back() = 2; require(!decode_roster(bad_roster, parsed));
    roster.revision = 2; roster.session = 200; require(!view.apply(encode_roster(roster)));
    roster.session = 199; roster.participants[1].connected = false;
    require(!view.apply(encode_roster(roster)));
    roster.participants[1].connected = true;
    require(view.apply(encode_roster(roster))); // invalid updates did not consume revision
    ClientPump pump;
    auto peers = MemoryTransport::pair();
    pump.start(std::move(peers.first), 400, {10,20});
    pump.update(0.1);
    require(peers.second->receive(received) && decode_hello(received.payload, decoded));
    auto admission = host.admit(30, decoded, 9000);
    require(peers.second->send(Frame{Message::ServerHello, Channel::Control, Delivery::ReliableOrdered,
        1, encode_welcome(admission)}) == SendResult::Sent);
    pump.update(0.1); require(pump.session().state() == ClientState::Connected);
    auto published = make_roster(host, 199, 3);
    require(peers.second->send(Frame{Message::Roster, Channel::Control, Delivery::ReliableOrdered,
        2, encode_roster(published)}) == SendResult::Sent);
    pump.update(0.1); require(pump.roster()->current().participants.size() == 3);
    peers.second->close(); pump.update(0.1);
    require(pump.session().state() == ClientState::Disconnected);
    auto resumed_peers = MemoryTransport::pair();
    pump.reconnect(std::move(resumed_peers.first)); pump.update(10);
    require(pump.session().state() == ClientState::Disconnected); // retry remains possible
    pump.stop(); auto silent = MemoryTransport::pair();
    pump.start(std::move(silent.first), 500, {10,20}); pump.update(10);
    require(pump.session().state() == ClientState::Offline && !silent.second->connected());
    HostPump host_pump; Identity next_token = 100;
    host_pump.start(500, 1, {10,20}, [&next_token]() { return ++next_token; });
    ClientPump first, second;
    auto first_link = MemoryTransport::pair();
    require(host_pump.attach(100, std::move(first_link.second)));
    first.start(std::move(first_link.first), 2, {10,20});
    for (unsigned i = 0; i < 5; ++i) { first.update(.01); host_pump.update(.01); }
    require(first.session().state() == ClientState::Connected && first.roster()->current().participants.size() == 2);
    auto second_link = MemoryTransport::pair();
    require(host_pump.attach(101, std::move(second_link.second)));
    second.start(std::move(second_link.first), 3, {10,20});
    for (unsigned i = 0; i < 5; ++i) { first.update(.01); second.update(.01); host_pump.update(.01); }
    require(first.roster()->current().participants.size() == 3 && second.roster()->current().participants.size() == 3);
    second.stop(); host_pump.update(.01); first.update(.01);
    require(!first.roster()->current().participants[2].connected);
    auto abandoned = MemoryTransport::pair();
    require(host_pump.attach(102, std::move(abandoned.second)));
    host_pump.update(10); require(!abandoned.first->connected());
    host_pump.stop(); first.update(.01);
    require(first.session().state() == ClientState::Disconnected);
    std::cout << "CoopNet admission, identity, reconnect and queue-bound tests passed\n";
}

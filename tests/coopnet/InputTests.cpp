#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value, int line) {
    if (!value) { std::cerr << "Input test failed at line " << line << '\n'; std::exit(1); }
}
#define require(value) check((value), __LINE__)
int main() {
    ActorInput input{20,1,10,0xfffffffeu,1,1.f,.5f}, decoded;
    const auto bytes = encode_input(input);
    require(bytes.size() == 30 && decode_input(bytes,decoded) && decoded.buttons == 1 && decoded.yaw == 1.f);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        require(!decode_input({bytes.begin(),bytes.begin() + size},decoded));
    auto malformed = bytes; malformed.push_back(0); require(!decode_input(malformed,decoded));
    malformed = bytes; malformed[20] = 0x40; require(!decode_input(malformed,decoded)); // simulation turn state
    malformed = bytes; malformed[24] = 0x80; malformed[25] = 0x7f; require(!decode_input(malformed,decoded)); // infinity
    auto invalid = input; invalid.pitch = 2; require(!valid_input(invalid));
    auto armed=input; armed.buttons=fire_button|reload_button;
    require(decode_input(encode_input(armed),decoded) && decoded.buttons==armed.buttons);
    invalid = input; invalid.yaw = 4; require(!valid_input(invalid));
    invalid = input; invalid.entity = 0; require(!valid_input(invalid));
    require(valid_contract({Message::ActorInput,Channel::Actor,Delivery::UnreliableSequenced,0,{}}));
    require(!valid_contract({Message::ActorInput,Channel::World,Delivery::ReliableOrdered,0,{}}));
    HostPump host; ClientPump client;
    host.start(10,1,{1,1},[] { return Identity{100}; });
    auto link = MemoryTransport::pair(); auto* raw = link.first.get();
    require(host.attach(1,std::move(link.second))); client.start(std::move(link.first),2,{1,1});
    for (unsigned i = 0; i < 5; ++i) { client.update(.01); host.update(.01); }
    const auto player = client.session().welcome().player;
    require(host.create_actor({20,player,2,1,10}));
    require(host.create_actor({21,1,1,1,10}));
    require(client.send_input(input) == SendResult::Disconnected);
    require(host.assign_level(player,10,1000)); host.update(0); client.update(0);
    require(client.acknowledge_level(10)); host.update(0); client.update(0);
    require(client.send_input(input) == SendResult::Sent); host.update(0);
    require(host.latest_input(player,decoded) && decoded.sequence == input.sequence);
    input.sequence = 0; input.buttons = 2;
    require(client.send_input(input) == SendResult::Sent); host.update(0);
    require(host.latest_input(player,decoded) && decoded.buttons == 2); // sequence wrap
    input.sequence = 0xffffffffu; input.buttons = 1;
    require(client.send_input(input) == SendResult::Sent); host.update(.1);
    require(host.latest_input(player,decoded) && decoded.buttons == 2); // stale controls cannot replace newer ones
    host.update(.15); require(!host.latest_input(player,decoded)); // packet-loss neutralization
    input.sequence = 1; require(client.send_input(input) == SendResult::Sent); host.update(0);
    require(host.latest_input(player,decoded));
    require(host.assign_level(player,11,1001)); require(!host.latest_input(player,decoded));
    host.update(0); client.update(0); require(client.acknowledge_level(11)); host.update(0); client.update(0);
    require(host.set_interest_level(player,10)); host.update(0); client.update(0);
    // Delayed old packet after interest returns cannot resurrect held controls.
    require(raw->send({Message::ActorInput,Channel::Actor,Delivery::UnreliableSequenced,
        input.sequence,encode_input(input)}) == SendResult::Sent);
    host.update(0); require(!host.latest_input(player,decoded));
    require(host.remove_actor(20,1)); require(host.create_actor({20,player,2,2,10})); host.update(0); client.update(0);
    input.sequence = 2;
    require(raw->send({Message::ActorInput,Channel::Actor,Delivery::UnreliableSequenced,
        input.sequence,encode_input(input)}) == SendResult::Sent);
    host.update(0); require(!host.latest_input(player,decoded)); // retired actor generation
    input.generation = 2; input.sequence = 0;
    require(raw->send({Message::ActorInput,Channel::Actor,Delivery::UnreliableSequenced,
        input.sequence,encode_input(input)}) == SendResult::Sent);
    host.update(0); require(host.latest_input(player,decoded) && decoded.generation == 2);
    input.entity = 21; input.generation = 1;
    require(client.send_input(input) == SendResult::Invalid);
    require(raw->send({Message::ActorInput,Channel::Actor,Delivery::UnreliableSequenced,
        input.sequence,encode_input(input)}) == SendResult::Sent);
    host.update(0); require(host.ready_participants() == 1 && !host.latest_input(player,decoded)); // forged ownership
    client.stop(); host.stop();
    std::cout << "CoopNet input codec, ownership, sequencing, generation, transfer and expiry tests passed\n";
}

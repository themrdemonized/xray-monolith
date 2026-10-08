#include "../../src/CoopNet/ActorSnapshot.h"
#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value, int line) {
    if (!value) { std::cerr << "Snapshot test failed at " << line << '\n'; std::exit(1); }
}
#define require(value) check((value), __LINE__)
int main() {
    ActorSnapshot first{5, 1, 10, 1, 1000000, {0,0,0}, {1,0,0}, {0,3.1f,0}, 2, 1};
    auto bytes = encode_snapshot(first); ActorSnapshot result;
    require(bytes.size() == 67 && decode_snapshot(bytes, result) && result.entity == 5 && result.position == first.position);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        require(!decode_snapshot({bytes.begin(), bytes.begin() + size}, result));
    auto corrupt = bytes; corrupt.push_back(0); require(!decode_snapshot(corrupt, result));
    corrupt = bytes; corrupt.back() = 4; require(!decode_snapshot(corrupt, result));
    corrupt = bytes; corrupt[28] = 0; corrupt[29] = 0; corrupt[30] = 0x80; corrupt[31] = 0x7f;
    require(!decode_snapshot(corrupt, result)); // infinity
    SnapshotBuffer buffer; buffer.bind(5, 1, 10); require(!buffer.sample(0, result));
    require(buffer.push(first)); require(!buffer.push(first));
    auto second = first; second.tick = 2; second.time_us = 1200000; second.position[0] = 10;
    second.rotation[1] = -3.1f; second.stance = 2;
    auto other = second; other.level = 11; require(!buffer.push(other));
    other = second; other.generation = 2; require(!buffer.push(other));
    require(buffer.push(second));
    require(buffer.sample(1200000, result) && std::abs(result.position[0] - 5) < 1e-6f);
    require(std::abs(std::abs(result.rotation[1]) - 3.14159265f) < 1e-5f && result.stance == 1);
    require(buffer.sample(1300000, result) && result.position[0] == 10 && result.stance == 2);
    require(buffer.sample(10, result) && result.position[0] == 0);
    require(buffer.sample(UINT64_MAX, result) && result.position[0] == 10); // no unbounded extrapolation
    buffer.bind(5, 2, 11); require(!buffer.push(second) && buffer.size() == 0);
    first.generation = 2; first.level = 11; first.tick = 0xfffffff0u;
    for (unsigned i = 0; i < 50; ++i) {
        auto value = first; value.tick += i; value.time_us += i * 40000; require(buffer.push(value));
    }
    require(buffer.size() == 32);
    ActorReplicas replicas;
    ActorPresence presence{first.entity,1,1,first.generation,first.level}, decoded;
    const auto presence_bytes = encode_presence(presence);
    require(presence_bytes.size() == 34 && decode_presence(presence_bytes, decoded));
    require(valid_actor_visual("actors\\stalker.ogf") && !valid_actor_visual("../actor") &&
        !valid_actor_visual("C:\\actor") && !valid_actor_visual(std::string(192,'a')));
    auto appearance = presence; appearance.visual = "actors\\stalker.ogf";
    auto appearance_bytes = encode_presence(appearance);
    require(decode_presence(appearance_bytes,decoded) && decoded.visual == appearance.visual);
    appearance_bytes[34] = 0; require(!decode_presence(appearance_bytes,decoded));
    appearance.visual = std::string(191,'a');
    require(decode_presence(encode_presence(appearance),decoded) && decoded.visual.size() == 191);
    require(decode_presence(presence_bytes,decoded));
    for (std::size_t size = 0; size < presence_bytes.size(); ++size)
        require(!decode_presence({presence_bytes.begin(), presence_bytes.begin() + size}, decoded));
    require(!replicas.push(first) && replicas.create(presence) && replicas.push(first));
    require(!replicas.create(presence) && replicas.remove(presence) && !replicas.push(first));
    require(replicas.create(presence) && !replicas.push(first)); // delayed datagram after interest re-entry
    other = first; ++other.tick; ++other.time_us; require(replicas.push(other));
    require(replicas.remove(presence));
    ++presence.generation; ++presence.level;
    require(replicas.create(presence) && !replicas.push(other));
    require(!replicas.remove(decoded)); // old-generation removal cannot destroy the new binding
    HostPump host; host.start(10, 1, {1,1}, [] { return Identity{123}; });
    ClientPump client; auto pair = MemoryTransport::pair();
    require(host.attach(1, std::move(pair.first)));
    auto* guest_transport = pair.second.get();
    client.start(std::move(pair.second), 2, {1,1});
    for (int i = 0; i < 4; ++i) { client.update(0); host.update(0); }
    client.update(0);
    require(client.session().state() == ClientState::Connected);
    require(host.create_actor({first.entity,1,1,first.generation,first.level}));
    unsigned delivered = 0;
    client.set_snapshot_sink([&](const ActorSnapshot& value) { require(value.entity == first.entity); ++delivered; });
    require(host.publish_snapshot(first)); client.update(0); require(delivered == 0);
    require(host.set_interest_level(client.session().welcome().player, first.level));
    host.update(0); client.update(0);
    require(host.publish_snapshot(first)); client.update(0); require(delivered == 1);
    other = first; ++other.level;
    require(!host.publish_snapshot(other)); client.update(0); require(delivered == 1);
    require(host.set_interest_level(client.session().welcome().player, 0));
    require(host.publish_snapshot(first)); client.update(0); require(delivered == 1);
    require(host.assign_level(client.session().welcome().player, first.level, 555));
    host.update(0); client.update(0);
    require(client.assignment().ticket == 555 && !client.acknowledge_level(first.level + 1));
    require(host.publish_snapshot(first)); client.update(0); require(delivered == 1);
    require(client.acknowledge_level(first.level) && !client.acknowledge_level(first.level));
    host.update(0); client.update(0);
    other = first; ++other.tick; ++other.time_us;
    require(host.publish_snapshot(other)); client.update(0); require(delivered == 2);
    const auto player = client.session().welcome().player;
    unsigned cancellations = 0;
    client.set_transfer_failure_sink([&](const LevelFailure& event) {
        require(event.player == player && event.assignment.ticket != 0); ++cancellations;
    });
    require(host.assign_level(player, first.level + 1, 556));
    require(!host.assign_level(player, first.level, 557)); // no silent replacement of an in-flight checkpoint
    host.update(0); client.update(0);
    const auto cancelled = client.assignment();
    require(host.cancel_level(player)); host.update(0); client.update(0);
    require(!client.assignment().ticket && client.transfer_failure() == TransferFailure::Cancelled);
    LevelFailure failure;
    require(host.take_level_failure(failure) && failure.player == player && failure.assignment.ticket == 556);
    require(!host.take_level_failure(failure));
    require(guest_transport->send(Frame{Message::LevelReady,Channel::Transition,Delivery::ReliableOrdered,
        cancelled.revision,encode_assignment(cancelled)}) == SendResult::Sent);
    host.update(0); client.update(0); require(client.session().state() == ClientState::Connected);
    require(host.publish_snapshot(other)); client.update(0); require(delivered == 2);
    require(host.assign_level(player, first.level + 1, 558));
    host.update(120); client.update(0);
    require(client.transfer_failure() == TransferFailure::Timeout && !client.assignment().ticket);
    require(host.take_level_failure(failure) && failure.reason == TransferFailure::Timeout && failure.assignment.ticket == 558);
    require(cancellations == 2);
    // Guests cannot inject authoritative actor transforms through the host dispatcher.
    require(guest_transport->send(Frame{Message::ActorSnapshot, Channel::Actor,
        Delivery::UnreliableSequenced, first.tick, encode_snapshot(first)}) == SendResult::Sent);
    host.update(0); client.update(0);
    require(client.session().state() == ClientState::Disconnected);
    host.stop(); client.stop();
    std::cout << "CoopNet snapshot codec, level/generation isolation and interpolation tests passed\n";
}

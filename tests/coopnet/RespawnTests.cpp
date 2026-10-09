#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <cstdlib>
#include <iostream>
using namespace coopnet;
void require(bool value) { if (!value) std::abort(); }
int main() {
    RespawnRequest request{20,1,10,1},decoded;
    const auto request_bytes=encode_respawn_request(request);
    require(decode_respawn_request(request_bytes,decoded));
    for (std::size_t size=0;size<request_bytes.size();++size) require(!decode_respawn_request({request_bytes.begin(),request_bytes.begin()+size},decoded));
    RespawnResult result{request,RespawnStatus::Accepted,20,{3,4,5}},restored;
    const auto bytes=encode_respawn_result(result); require(decode_respawn_result(bytes,restored));
    for (std::size_t size=0;size<bytes.size();++size) require(!decode_respawn_result({bytes.begin(),bytes.begin()+size},restored));
    auto trailing=bytes; trailing.push_back(0); require(!decode_respawn_result(trailing,restored));
    result.position[0]=std::numeric_limits<float>::quiet_NaN(); require(!valid_respawn_result(result));
    HostPump host; ClientPump client; Identity token=100;
    host.start(500,1,{10,20},[&] { return ++token; }); auto link=MemoryTransport::pair(); auto* wire=link.first.get();
    require(host.attach(1,std::move(link.second))); client.start(std::move(link.first),2,{10,20});
    for (unsigned i=0;i<4;++i) { client.update(.01); host.update(.01); }
    const auto player=client.session().welcome().player;
    require(host.assign_level(player,10,800)); host.update(.01); client.update(.01); require(client.acknowledge_level(10)); host.update(.01);
    require(host.create_actor({10,1,1,1,10,"actor"}) && host.create_actor({20,player,2,1,10,"actor"})); host.update(.01); client.update(.01);
    unsigned mutations=0,approvals=0,vitals=0;
    host.set_respawn_handler([&](Identity owner,const RespawnRequest& value) {
        require(owner==player); ++mutations; return RespawnResult{value,RespawnStatus::Accepted,20,{3,4,5}};
    });
    client.set_respawn_sink([&](const RespawnResult& value) { require(value.status==RespawnStatus::Accepted && value.position[2]==5); ++approvals; });
    client.set_vitals_sink([&](const ActorVitals&) { ++vitals; });
    require(client.send_respawn({10,1,10,1})==SendResult::Invalid); // another player's identity
    require(client.send_respawn({20,2,10,1})==SendResult::Invalid); // retired binding
    require(client.send_respawn(request)==SendResult::Sent && client.respawn_pending());
    require(client.send_respawn(request)==SendResult::Backpressure);
    host.update(.01); client.update(.01);
    require(mutations==1 && approvals==1 && !client.respawn_pending());
    require(wire->send({Message::RespawnRequest,Channel::Combat,Delivery::ReliableOrdered,1,request_bytes})==SendResult::Sent);
    host.update(.01); client.update(.01); require(mutations==1 && approvals==1); // replay cannot revive twice
    require(host.publish_vitals({20,1,10,19,0,1,0})); client.update(.01); require(vitals==0); // pre-respawn death update
    require(host.publish_vitals({20,1,10,21,1,1,0})); client.update(.01); require(vitals==1);
    require(wire->send({Message::RespawnRequest,Channel::Combat,Delivery::ReliableOrdered,2,encode_respawn_request({10,1,10,2})})==SendResult::Sent);
    host.update(.01); require(mutations==1 && !host.session().players()[1].connected); // forged owner disconnected
    std::cout<<"Respawn bounds, owned requests, binding checks, replay protection and stale death updates passed\n";
}

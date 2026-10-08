#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
#include <limits>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"World state failed at "<<line<<'\n'; std::exit(1); } }
#define require(value) check((value),__LINE__)
int main() {
    std::set<Identity> anchors;
    for (unsigned id=0;id<65536;++id) require(anchors.insert(world_anchor(123,static_cast<std::uint16_t>(id))).second);
    require(!anchors.count(0) && world_anchor(123,5)!=world_anchor(124,5));
    WorldState state{10,1,{{world_anchor(123,5),7,{1,2,3},{0,1,0},.75f}}},decoded;
    auto encoded=encode_world_state(state);
    require(decode_world_state(encoded,decoded) && decoded.objects[0].health==.75f);
    for (std::size_t n=0;n<encoded.size();++n) require(!decode_world_state({encoded.begin(),encoded.begin()+n},decoded));
    encoded.push_back(0); require(!decode_world_state(encoded,decoded));
    auto invalid=state; invalid.objects.push_back(invalid.objects.front()); require(!valid_world_state(invalid));
    invalid=state; invalid.objects.front().health=std::numeric_limits<float>::quiet_NaN(); require(!valid_world_state(invalid));
    invalid=state; invalid.objects.resize(129); require(!valid_world_state(invalid));
    HostPump host; ClientPump client; auto links=MemoryTransport::pair(); Identity token=10;
    host.start(123,1,{1,1},[&] { return ++token; }); require(host.attach(1,std::move(links.second)));
    client.start(std::move(links.first),2,{1,1}); auto pump=[&] { host.update(.01); client.update(.01); };
    unsigned applied=0; client.set_world_sink([&](const WorldState& value) { applied+=static_cast<unsigned>(value.objects.size()); });
    for (unsigned n=0;n<10;++n) pump();
    require(host.publish_world_state(state)); pump(); require(!applied);
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(12),std::uint8_t(0)); WorldBaseline baseline{555,10,12,{}};
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return true; });
    const auto player=client.session().welcome().player;
    require(host.send_baseline(player,baseline,bytes)); for (unsigned n=0;n<10;++n) pump();
    require(client.acknowledge_baseline()); pump();
    require(host.assign_level(player,10,666)); pump();
    require(host.publish_world_state(state)); pump(); require(!applied);
    require(client.acknowledge_level(10)); pump();
    require(host.publish_world_state(state)); pump(); require(applied==1);
    require(host.publish_world_state(state)); pump(); require(applied==1);
    state.tick=2; require(host.publish_world_state(state)); pump(); require(applied==2);
    state.tick=3; state.objects.front().incarnation=8; require(host.publish_world_state(state)); pump(); require(applied==2);
    state.level=11; state.objects.front().incarnation=7; require(host.publish_world_state(state)); pump(); require(applied==2);
    client.stop(); host.stop();
    std::cout<<"CoopNet NPC codec, anchor uniqueness, loading barrier, level isolation, duplicate and incarnation rejection passed\n";
}


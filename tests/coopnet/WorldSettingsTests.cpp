#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <cstdlib>
#include <iostream>
using namespace coopnet;
void require(bool value) { if (!value) std::abort(); }
int main() {
    require(world_rule_path("alife/general/alife_mutant_pop") && world_rule_path("video/weather/rain_period"));
    require(!world_rule_path("video/basic/renderer") && !world_rule_path("control/general/mouse_sens") && !world_rule_path("gameplay/general/player_name"));
    require(!valid_world_rule({"alife/../bad","1",2}) && !valid_world_rule({"alife/general/pop","nan",2}) && !valid_world_rule({"alife/general/pop","1;quit",0}));
    WorldRulesChunk chunk{1,0,1,{{"alife/general/alife_mutant_pop","0.75",2}}},decoded;
    const auto bytes=encode_world_rules(chunk); require(decode_world_rules(bytes,decoded));
    for (std::size_t n=0;n<bytes.size();++n) require(!decode_world_rules({bytes.begin(),bytes.begin()+n},decoded));
    auto trailing=bytes; trailing.push_back(0); require(!decode_world_rules(trailing,decoded));
    WorldRulesAssembly assembly; bool complete=false; std::vector<WorldRule> rules;
    require(assembly.append(chunk,complete,rules) && complete && rules.size()==1);
    chunk.total=2; require(assembly.append(chunk,complete,rules) && !complete);
    chunk.offset=1; require(!assembly.append(chunk,complete,rules)); // duplicate paths across chunks
    WorldClock clock{10,7,123456789,7.f,0.f,2,"clear",""},restored;
    const auto clock_bytes=encode_world_clock(clock); require(decode_world_clock(clock_bytes,restored));
    for (std::size_t n=0;n<clock_bytes.size();++n) require(!decode_world_clock({clock_bytes.begin(),clock_bytes.begin()+n},restored));
    clock.time_factor=std::numeric_limits<float>::quiet_NaN(); require(!valid_world_clock(clock)); clock.time_factor=7;
    clock.cycle="../clear"; require(!valid_world_clock(clock)); clock.cycle="clear";
    HostPump host; ClientPump client; Identity token=100;
    host.start(500,1,{10,20},[&] { return ++token; }); auto link=MemoryTransport::pair();
    require(host.attach(1,std::move(link.second))); client.start(std::move(link.first),2,{10,20});
    unsigned views=0,clocks=0;
    client.set_world_rules_sink([&](std::uint32_t revision,const std::vector<WorldRule>& values) { require(revision==1 && values.size()==4096); ++views; });
    client.set_world_clock_sink([&](const WorldClock&) { ++clocks; });
    rules.clear(); for (unsigned i=0;i<4096;++i) rules.push_back({"alife/general/test_"+std::to_string(i),"0.75",2});
    require(host.publish_world_rules(1,rules));
    for (unsigned i=0;i<68;++i) { client.update(.01); host.update(.01); }
    require(views==1 && !host.publish_world_rules(1,rules)); // applies only after all chunks, before loading
    require(host.publish_world_clock(clock)); client.update(.01); require(clocks==0);
    const auto player=client.session().welcome().player; require(host.assign_level(player,10,800)); host.update(.01); client.update(.01);
    require(client.acknowledge_level(10)); host.update(.01); require(host.publish_world_clock(clock)); client.update(.01); require(clocks==1);
    require(host.publish_world_clock(clock)); client.update(.01); require(clocks==1); // stale clock ignored
    clock.level=11; ++clock.tick; require(host.publish_world_clock(clock)); client.update(.01); require(clocks==1);
    std::cout<<"Host world rules bounds, chunk assembly, local preference isolation and world clock loading checks passed\n";
}

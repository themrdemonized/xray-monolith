#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Shared world failed at "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    NPCRecord npc{{100,9,{1,2,3},{},1},"dog_weak","monsters/dog/dog"};
    std::vector<NPCRecord> decoded; auto bytes=encode_npcs({npc}); require(decode_npcs(bytes,decoded));
    for (std::size_t i=0;i<bytes.size();++i) require(!decode_npcs({bytes.begin(),bytes.begin()+i},decoded));
    auto extra=bytes; extra.push_back(0); require(!decode_npcs(extra,decoded));
    npc.visual="../bad"; require(!valid_npc(npc)); npc.visual="monsters/dog/dog";
    QuestState quests; QuestRecord q; q.id="shared_task"; q.title="Find the loot"; q.description=std::string(4096,'x'); q.target=100; quests.tasks.push_back(q); quests.infos={"shared_story_step"};
    auto quest_bytes=encode_quests(quests); QuestState decoded_quests; require(decode_quests(quest_bytes,decoded_quests));
    for (std::size_t i=0;i<quest_bytes.size();++i) require(!decode_quests({quest_bytes.begin(),quest_bytes.begin()+i},decoded_quests));
    extra=quest_bytes; extra.push_back(0); require(!decode_quests(extra,decoded_quests));
    SharedChunk chunk{SharedKind::NPC,10,1,static_cast<std::uint32_t>(bytes.size()),0,bytes},out;
    auto wire=encode_shared_chunk(chunk); for (std::size_t i=0;i<wire.size();++i) require(!decode_shared_chunk({wire.begin(),wire.begin()+i},out));
    SharedAssembly assembly; require(assembly.accept(chunk) && assembly.complete()); chunk.offset=1; require(!assembly.accept(chunk));
    HostPump host; ClientPump client; auto links=MemoryTransport::pair(); Identity token=10;
    host.start(123,1,{1,1},[&] { return ++token; }); require(host.attach(1,std::move(links.second))); client.start(std::move(links.first),2,{1,1});
    auto pump=[&] { host.update(.01); client.update(.01); }; unsigned npc_applied=0,quests_applied=0;
    client.set_shared_world_sink([&](SharedKind kind,std::uint32_t,const std::vector<std::uint8_t>& payload) {
        if (kind==SharedKind::NPC) { require(decode_npcs(payload,decoded)); ++npc_applied; } else { require(decode_quests(payload,decoded_quests)); ++quests_applied; }
    });
    for (unsigned i=0;i<10;++i) pump();
    require(host.publish_shared_world(SharedKind::NPC,10,1,bytes)); require(host.publish_shared_world(SharedKind::Quests,10,1,quest_bytes)); pump(); require(!npc_applied && !quests_applied);
    auto baseline_bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(12),std::uint8_t(0)); WorldBaseline baseline{555,10,12,{}};
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return true; }); const auto player=client.session().welcome().player;
    require(host.send_baseline(player,baseline,baseline_bytes)); for (unsigned i=0;i<10;++i) pump(); require(client.acknowledge_baseline()); pump();
    require(host.assign_level(player,10,666)); pump(); require(client.acknowledge_level(10)); for (unsigned i=0;i<10;++i) pump(); require(npc_applied==1 && quests_applied==1);
    unsigned poses=0; client.set_world_sink([&](const WorldState&) { ++poses; });
    WorldState pose{10,10,{npc.pose}}; require(host.publish_world_state(pose)); pump(); require(poses==1);
    npc.pose.incarnation=10; npc.pose.health=0; require(host.publish_shared_world(SharedKind::NPC,10,2,encode_npcs({npc}))); for (unsigned i=0;i<10;++i) pump(); require(decoded[0].pose.incarnation==10 && decoded[0].pose.health==0);
    pose.tick=11; require(host.publish_world_state(pose)); pump(); require(poses==1);
    pose.tick=12; pose.objects[0]=npc.pose; require(host.publish_world_state(pose)); pump(); require(poses==2);
    require(host.publish_shared_world(SharedKind::NPC,10,3,encode_npcs({}))); for (unsigned i=0;i<10;++i) pump(); require(decoded.empty());
    pose.tick=13; require(host.publish_world_state(pose)); pump(); require(poses==2);
    quests.tasks[0].state=2; require(host.publish_shared_world(SharedKind::Quests,10,2,encode_quests(quests))); for (unsigned i=0;i<10;++i) pump(); require(decoded_quests.tasks[0].state==2);
    // A large update finishes its captured revision while the host replaces the cached snapshot.
    quests.tasks.clear(); for (unsigned i=0;i<120;++i) { q.id="task_"+std::to_string(i); quests.tasks.push_back(q); }
    require(host.publish_shared_world(SharedKind::Quests,10,3,encode_quests(quests))); pump();
    quests.tasks.resize(1); require(host.publish_shared_world(SharedKind::Quests,10,4,encode_quests(quests))); for (unsigned i=0;i<100;++i) pump(); require(decoded_quests.tasks.size()==1 && quests_applied>=4);
    require(host.publish_shared_world(SharedKind::NPC,11,4,bytes)); for (unsigned i=0;i<10;++i) pump(); require(npc_applied==3);
    client.stop(); host.stop(); std::cout<<"Shared NPC spawn/death/removal, quest snapshots, bounds, loading isolation and revision streaming passed\n";
}

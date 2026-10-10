#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
#include <limits>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Inventory view failure "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    require(valid_item_state({1,0,1,1,true,"ammo_5.45x39_fmj"}));
    require(!valid_item_state({1,0,1,1,true,"ammo..fmj"}));
    ItemState loot{900,0,10,1,true,"bandage"}; loot.world=true; loot.anchor=901; loot.incarnation=902;
    loot.position={1,2,3}; loot.condition=.4321f;
    const auto loot_bytes=encode_item_state(loot); ItemState decoded_loot;
    require(decode_item_state(loot_bytes,decoded_loot) && decoded_loot.world && decoded_loot.anchor==901 && decoded_loot.incarnation==902 && decoded_loot.position==loot.position && decoded_loot.condition==loot.condition);
    for (std::size_t n=0;n<loot_bytes.size();++n) require(!decode_item_state({loot_bytes.begin(),loot_bytes.begin()+n},decoded_loot));
    auto extra=loot_bytes; extra.push_back(0); require(!decode_item_state(extra,decoded_loot));
    loot.incarnation=0; require(!valid_item_state(loot)); loot.incarnation=902;
    loot.position[0]=std::numeric_limits<float>::infinity(); require(!valid_item_state(loot)); loot.position[0]=1;
    loot.kind=3; require(!valid_item_state(loot)); loot.kind=0;
    loot.condition=std::numeric_limits<float>::quiet_NaN(); require(!valid_item_state(loot));
    InventoryView view; view.actor=20; view.generation=1; view.level=10; view.revision=1;
    view.money=314159;
    view.community="actor_stalker";
    view.npc_disposition={{800,-2000},{801,1000}};
    for (unsigned n=0;n<256;++n) view.items.push_back({100+n,1,"bandage",.8f,0xffff,0,0,0,0});
    InventoryViewAssembly assembly; InventoryView output; bool complete=false;
    for (unsigned offset=0;offset<256;offset+=32) {
        InventoryViewChunk chunk{view,static_cast<std::uint16_t>(offset),256};
        chunk.view.items.assign(view.items.begin()+offset,view.items.begin()+offset+32);
        const auto bytes=encode_view_chunk(chunk); InventoryViewChunk decoded;
        require(bytes.size()<max_payload && decode_view_chunk(bytes,decoded));
        for (std::size_t n=0;n<bytes.size();++n) require(!decode_view_chunk({bytes.begin(),bytes.begin()+n},decoded));
        auto trailing=bytes; trailing.push_back(0); require(!decode_view_chunk(trailing,decoded));
        require(decode_view_chunk(bytes,decoded)); require(assembly.append(decoded,complete,output));
        require(complete==(offset==224));
    }
    require(output.items.size()==256 && output.items.back().item==355 && output.money==314159);
    require(output.community=="actor_stalker");
    require(output.npc_disposition==view.npc_disposition);
    auto equipped=view; equipped.items.resize(1); equipped.items.front().kind=1;
    equipped.items.front().addons=5; equipped.items.front().scope=2; equipped.items.front().uses=3;
    equipped.items.front().upgrades={"up_firsta_pm","up_secona_pm"};
    InventoryViewChunk equipment_chunk{equipped,0,1},equipment_decoded;
    require(decode_view_chunk(encode_view_chunk(equipment_chunk),equipment_decoded));
    require(equipment_decoded.view.items.front().upgrades==equipped.items.front().upgrades && equipment_decoded.view.items.front().addons==5 && equipment_decoded.view.items.front().uses==3);
    equipment_chunk.view.items.front().upgrades={"../bad"}; require(!valid_view_chunk(equipment_chunk));
    InventoryViewChunk first{view,0,256}; first.view.items.assign(view.items.begin(),view.items.begin()+32);
    require(assembly.append(first,complete,output) && !complete);
    auto mixed=first; mixed.offset=32; mixed.view.revision=2; require(!assembly.append(mixed,complete,output));
    mixed=first; mixed.offset=32; require(!assembly.append(mixed,complete,output)); // repeated item identities
    auto empty=view; empty.items.clear(); empty.active=100;
    require(!assembly.append({empty,0,0},complete,output));
    empty.active=0; require(assembly.append({empty,0,0},complete,output) && complete && output.items.empty());
    auto bad=first; bad.total=257; require(!valid_view_chunk(bad));
    bad=first; bad.view.items[0].condition=std::numeric_limits<float>::quiet_NaN(); require(!valid_view_chunk(bad));
    bad=first; bad.view.items[0].section="../bad"; require(!valid_view_chunk(bad));
    auto invalid=view; invalid.active=100; require(!valid_inventory_view(invalid));
    invalid.items[0].place=2; invalid.items[0].slot=2; require(valid_inventory_view(invalid));
    invalid.items[1].place=2; invalid.items[1].slot=2; require(!valid_inventory_view(invalid));
    invalid=view; invalid.items.back().item=invalid.items.front().item; require(!valid_inventory_view(invalid));
    HostPump host; ClientPump a,b; Identity token=100;
    host.start(1,1,{1,1},[&] { return ++token; });
    unsigned character_received=0;
    host.set_character_handler([&](Identity player,const InventoryView& selected) {
        ++character_received; require(player>=2 && selected.actor==2 && selected.items.size()==256 && selected.money==314159); return true;
    });
    auto la=MemoryTransport::pair(),lb=MemoryTransport::pair();
    require(host.attach(1,std::move(la.second))); require(host.attach(2,std::move(lb.second)));
    a.start(std::move(la.first),2,{1,1}); b.start(std::move(lb.first),3,{1,1});
    auto selected=view; selected.actor=2; selected.level=1; a.set_character_profile(selected);
    auto pump=[&] { for (unsigned n=0;n<8;++n) { host.update(.01); a.update(.01); b.update(.01); } };
    pump(); require(character_received==1); const auto pa=a.session().welcome().player,pb=b.session().welcome().player;
    require(host.create_actor({20,pa,2,1,10})); require(host.create_actor({21,pb,3,1,10}));
    require(host.assign_level(pa,10,1000)); require(host.assign_level(pb,10,1001)); pump();
    require(a.acknowledge_level(10)); require(b.acknowledge_level(10)); pump();
    unsigned received_a=0,received_b=0;
    a.set_inventory_view_sink([&](const InventoryView& v) { ++received_a; require(v.items.size()==256); });
    b.set_inventory_view_sink([&](const InventoryView&) { ++received_b; });
    require(!host.publish_inventory_view(pb,view)); require(host.publish_inventory_view(pa,view)); pump();
    require(received_a==1 && received_b==0);
    require(host.publish_inventory_view(pa,view)); pump(); require(received_a==1); // stale whole view ignored
    view.revision=2; require(host.publish_inventory_view(pa,view)); pump(); require(received_a==2);
    require(host.assign_level(pa,11,2000)); pump(); require(!host.publish_inventory_view(pa,view));
    InventoryRequest request{20,100,1,10,7,1,InventoryAction::Equip,2},decoded;
    require(decode_inventory_request(encode_inventory_request(request),decoded) && decoded.slot==2);
    request.slot=0xffff; require(!valid_inventory_request(request));
    request.action=InventoryAction::Use; require(valid_inventory_request(request)); request.slot=2; require(!valid_inventory_request(request));
    HostPump fair_host; ClientPump fair_client; Identity fair_token=2000;
    fair_host.start(10,1,{1,1},[&] { return ++fair_token; }); auto fair_link=MemoryTransport::pair();
    require(fair_host.attach(9,std::move(fair_link.second))); fair_client.start(std::move(fair_link.first),2,{1,1});
    for (unsigned n=0;n<4;++n) { fair_host.update(.01); fair_client.update(.01); }
    const auto fair_player=fair_client.session().welcome().player;
    require(fair_host.assign_level(fair_player,10,7000)); fair_host.update(.01); fair_client.update(.01);
    require(fair_client.acknowledge_level(10)); fair_host.update(.01); fair_client.update(.01);
    std::set<Identity> delivered; fair_client.set_item_sink([&](const ItemState& state) { delivered.insert(state.item); });
    for (unsigned n=0;n<16;++n) require(fair_host.publish_item({100+n,0,10,1,true,"bandage"}));
    for (unsigned tick=0;tick<4;++tick) {
        for (unsigned n=0;n<8;++n) require(fair_host.publish_item({100+n,0,10,2+tick,true,"bandage"}));
        fair_host.update(.01); fair_client.update(.01);
    }
    require(delivered.size()==16); // frequently changing early entries must not starve later loot
    for (unsigned failure=0;failure<3;++failure) {
        HostPump guarded; Identity guard_token=4000; unsigned imported=0;
        guarded.start(80,1,{1,1},[&] { return ++guard_token; });
        guarded.set_character_handler([&](Identity,const InventoryView&) {++imported; return true;});
        auto raw=MemoryTransport::pair(); require(guarded.attach(80,std::move(raw.second)));
        ClientHello hello{protocol_version,{1,1},9,0,0,0};
        require(raw.first->send({Message::ClientHello,Channel::Control,Delivery::ReliableOrdered,1,encode_hello(hello)})==SendResult::Sent);
        guarded.update(.01); Frame reply; require(raw.first->receive(reply)); Welcome admitted; require(decode_welcome(reply.payload,admitted));
        require(!guarded.player_ready(admitted.player));
        InventoryView character; character.actor=failure==1 ? 10 : 9; character.generation=character.level=character.revision=1;
        character.items={{1,1,"bandage",1,0xffff,0,0,0,0}};
        InventoryViewChunk chunk{character,0,static_cast<std::uint16_t>(failure==0 ? 2 : 1)};
        Frame profile{Message::CharacterProfile,Channel::Control,Delivery::ReliableOrdered,0,encode_view_chunk(chunk)};
        require(raw.first->send(profile)==SendResult::Sent);
        if (failure==2) require(raw.first->send(profile)==SendResult::Sent);
        else require(raw.first->send({Message::ClientReady,Channel::Control,Delivery::ReliableOrdered,2,{}})==SendResult::Sent);
        guarded.update(.01);
        require(!raw.first->connected() && !guarded.player_ready(admitted.player));
        require(imported==(failure==2 ? 1u : 0u));
    }
    std::cout<<"Inventory view chunk assembly, owner isolation, stale views and control validation passed\n";
}

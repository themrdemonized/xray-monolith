#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Gameplay test failed at line "<<line<<'\n'; std::exit(1); } }
#define require(value) check((value),__LINE__)
template<class T,class Decode> void truncated(const std::vector<std::uint8_t>& bytes,Decode decode) {
    T output;
    for (std::size_t n=0;n<bytes.size();++n) require(!decode({bytes.begin(),bytes.begin()+n},output));
    auto trailing=bytes; trailing.push_back(0); require(!decode(trailing,output));
}
int main() {
    InventoryRequest request{20,100,1,10,0xfffffffeu,1,InventoryAction::Take},decoded;
    require(decode_inventory_request(encode_inventory_request(request),decoded));
    truncated<InventoryRequest>(encode_inventory_request(request),decode_inventory_request);
    InventoryResult reply{100,20,1,2,InventoryStatus::Accepted},decoded_reply;
    require(decode_inventory_result(encode_inventory_result(reply),decoded_reply));
    truncated<InventoryResult>(encode_inventory_result(reply),decode_inventory_result);
    ItemState item{100,0,10,1,true,"bandage"},decoded_item;
    require(decode_item_state(encode_item_state(item),decoded_item));
    truncated<ItemState>(encode_item_state(item),decode_item_state);
    auto invalid_item=item; invalid_item.section="../bandage"; require(!valid_item_state(invalid_item));
    ActorVitals vitals{20,1,10,1,.75f,.8f,.1f},decoded_vitals;
    require(decode_vitals(encode_vitals(vitals),decoded_vitals) && decoded_vitals.health==.75f);
    truncated<ActorVitals>(encode_vitals(vitals),decode_vitals);
    auto invalid_vitals=vitals; invalid_vitals.health=std::numeric_limits<float>::infinity(); require(!valid_vitals(invalid_vitals));
    require(!valid_contract({Message::InventoryRequest,Channel::Inventory,Delivery::UnreliableSequenced,0,{}}));
    HostPump host; ClientPump a,b;
    Identity token=500; host.start(1,1,{1,1},[&] { return ++token; });
    auto link_a=MemoryTransport::pair(),link_b=MemoryTransport::pair(); auto* raw_a=link_a.first.get();
    require(host.attach(1,std::move(link_a.second))); require(host.attach(2,std::move(link_b.second)));
    a.start(std::move(link_a.first),2,{1,1}); b.start(std::move(link_b.first),3,{1,1});
    auto pump=[&] { for (unsigned n=0;n<4;++n) { host.update(.01); a.update(.01); b.update(.01); } };
    pump();
    const auto pa=a.session().welcome().player,pb=b.session().welcome().player;
    require(host.create_actor({20,pa,2,1,10})); require(host.create_actor({21,pb,3,1,10}));
    require(host.assign_level(pa,10,1000)); require(host.assign_level(pb,10,1001));
    pump(); require(a.acknowledge_level(10)); require(b.acknowledge_level(10)); pump();
    require(host.publish_item(item));
    for (Identity id=101;id<301;++id) require(host.publish_item({id,0,10,1,true,"bandage"}));
    for (unsigned n=0;n<10;++n) pump();
    require(a.items().size()==201 && b.items().size()==201 && host.ready_participants()==3);
    std::vector<InventoryResult> ra,rb;
    a.set_inventory_sink([&](const InventoryResult& v) { ra.push_back(v); });
    b.set_inventory_sink([&](const InventoryResult& v) { rb.push_back(v); });
    unsigned mutations=0,calls=0;
    host.set_inventory_handler([&](Identity,const InventoryRequest& r) {
        ++calls; InventoryResult result{r.item,item.owner,r.sequence,item.revision,InventoryStatus::Conflict};
        if (r.item!=item.item) { result.status=InventoryStatus::Unavailable; return result; }
        if (r.revision!=item.revision) return result;
        if ((r.action==InventoryAction::Take && item.owner) || (r.action==InventoryAction::Drop && item.owner!=r.actor)) return result;
        item.owner=r.action==InventoryAction::Take ? r.actor : 0; ++item.revision; ++mutations;
        require(host.publish_item(item)); result.owner=item.owner; result.revision=item.revision; result.status=InventoryStatus::Accepted; return result;
    });
    require(a.send_inventory(request)==SendResult::Sent);
    auto competing=request; competing.actor=21; competing.sequence=1;
    require(b.send_inventory(competing)==SendResult::Sent); pump();
    require(mutations==1 && item.owner==20 && ra.back().status==InventoryStatus::Accepted && rb.back().status==InventoryStatus::Conflict);
    require(a.send_inventory(request)==SendResult::Sent); pump();
    require(mutations==1 && calls==2 && ra.size()==2 && ra.back().revision==2);
    request.action=InventoryAction::Drop; request.sequence=0; request.revision=item.revision;
    require(a.send_inventory(request)==SendResult::Sent); pump();
    require(mutations==2 && item.owner==0 && ra.back().status==InventoryStatus::Accepted);
    auto old=request; old.sequence=0xffffffffu;
    require(a.send_inventory(old)==SendResult::Sent); pump(); require(ra.back().status==InventoryStatus::Expired && mutations==2);
    require(host.remove_actor(20,1)); require(host.create_actor({20,pa,2,2,10})); pump();
    old.sequence=1;
    require(raw_a->send({Message::InventoryRequest,Channel::Inventory,Delivery::ReliableOrdered,old.sequence,encode_inventory_request(old)})==SendResult::Sent);
    pump(); require(ra.back().status==InventoryStatus::Denied && mutations==2);
    unsigned applied=0; a.set_vitals_sink([&](const ActorVitals&) { ++applied; });
    vitals.generation=2; require(host.publish_vitals(vitals)); pump(); require(applied==1);
    require(host.publish_vitals(vitals)); pump(); require(applied==1);
    vitals.tick=0; require(host.publish_vitals(vitals)); pump(); require(applied==1);
    vitals.tick=2; require(host.publish_vitals(vitals)); pump(); require(applied==2);
    request.actor=21; request.sequence=2;
    require(a.send_inventory(request)==SendResult::Invalid);
    require(raw_a->send({Message::InventoryRequest,Channel::Inventory,Delivery::ReliableOrdered,request.sequence,encode_inventory_request(request)})==SendResult::Sent);
    pump(); require(host.ready_participants()==2 && mutations==2);
    a.stop(); b.stop(); host.stop();
    std::cout<<"CoopNet gameplay codec, bounded baseline, competing loot, replay, stale binding and damage ordering tests passed\n";
}

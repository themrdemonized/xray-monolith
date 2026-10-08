#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Party transition failed at "<<line<<'\n'; std::exit(1); } }
#define require(value) check((value),__LINE__)
int main() {
    PartyBarrier barrier;
    for (unsigned n=0;n<100;++n) require(!barrier.update(10,1,2,.1));
    require(!barrier.update(10,2,2,30));
    for (unsigned n=0;n<5;++n) require(!barrier.update(10,2,2,.1));
    require(!barrier.update(10,1,2,.1));
    for (unsigned n=0;n<6;++n) require(!barrier.update(10,2,2,.1));
    require(!barrier.update(11,2,2,.1));
    bool ready=false; for (unsigned n=0;n<20;++n) ready=barrier.update(11,2,2,.1);
    require(ready); require(!barrier.update(11,3,3,.1)); require(!barrier.update(0,0,0,.1));
    PartyStatus status{1,3,PartyStage::Gathering,1,2},decoded;
    const auto wire=encode_party_status(status); require(decode_party_status(wire,decoded) && decoded.present==1);
    for (std::size_t n=0;n<wire.size();++n) require(!decode_party_status({wire.begin(),wire.begin()+n},decoded));
    auto invalid=status; invalid.present=3; require(!valid_party_status(invalid));
    invalid=status; invalid.stage=static_cast<PartyStage>(5); require(!valid_party_status(invalid));
    HostPump host; ClientPump client; auto links=MemoryTransport::pair(); Identity token=10;
    host.start(123,1,{1,1},[&] { return ++token; }); require(host.attach(1,std::move(links.second)));
    host.publish_party_status(status); client.start(std::move(links.first),2,{1,1});
    unsigned deliveries=0; client.set_party_sink([&](const PartyStatus& value) { ++deliveries; require(value.required==2); });
    auto pump=[&] { host.update(.01); client.update(.01); }; for (unsigned n=0;n<10;++n) pump();
    require(deliveries==1 && client.party_status().stage==PartyStage::Gathering);
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(12),std::uint8_t(0)); WorldBaseline baseline{555,3,12,{}};
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return true; });
    const auto player=client.session().welcome().player;
    require(host.send_baseline(player,baseline,bytes)); for (unsigned n=0;n<10;++n) pump();
    require(client.acknowledge_baseline()); pump(); require(host.assign_level(player,3,666)); pump();
    require(client.acknowledge_level(3)); pump(); require(host.level_ready(player,3));
    host.suspend_world(); require(!host.level_ready(player,3));
    status.stage=PartyStage::Loading; status.present=0; status.destination=4; host.publish_party_status(status); pump();
    require(deliveries==2 && client.party_status().stage==PartyStage::Loading);
    baseline.id=556; baseline.level=4; require(host.send_baseline(player,baseline,bytes));
    for (unsigned n=0;n<10;++n) pump(); require(client.acknowledge_baseline()); pump();
    require(!host.level_ready(player,4)); require(host.assign_level(player,4,667)); pump();
    require(!client.acknowledge_level(3)); require(client.acknowledge_level(4)); pump(); require(host.level_ready(player,4));
    client.stop(); host.stop();
    std::cout<<"CoopNet group exit dwell, leave/reset, roster count, status codec and repeated native-load barrier tests passed\n";
}

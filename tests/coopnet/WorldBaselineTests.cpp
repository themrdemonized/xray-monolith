#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"World baseline test failed at "<<line<<'\n'; std::exit(1); } }
#define require(value) check((value),__LINE__)
class StalledTransport final : public Transport {
    std::unique_ptr<Transport> transport_;
    bool& stalled_;
public:
    StalledTransport(std::unique_ptr<Transport> transport,bool& stalled) : transport_(std::move(transport)),stalled_(stalled) {}
    bool connected() const override { return transport_->connected(); }
    bool connecting() const override { return transport_->connecting(); }
    SendResult send(const Frame& frame) override { return stalled_ ? SendResult::Backpressure : transport_->send(frame); }
    bool receive(Frame& frame) override { return transport_->receive(frame); }
    void close() override { transport_->close(); }
};
int main() {
    WorldBaseline manifest{900,10,768*1024,{}}; manifest.digest[0]=123;
    WorldBaseline decoded;
    const auto encoded=encode_baseline(manifest);
    require(decode_baseline(encoded,decoded) && same_baseline(decoded,manifest));
    for (std::size_t n=0;n<encoded.size();++n)
        require(!decode_baseline({encoded.begin(),encoded.begin()+n},decoded));
    auto trailing=encoded; trailing.push_back(0); require(!decode_baseline(trailing,decoded));
    auto invalid=manifest; invalid.size=max_baseline_bytes+1; require(!valid_baseline(invalid));
    invalid=manifest; invalid.size=11; require(!valid_baseline(invalid));
    auto data=std::make_shared<std::vector<std::uint8_t>>(manifest.size);
    for (std::size_t n=0;n<data->size();++n) (*data)[n]=static_cast<std::uint8_t>(n%251);
    WorldChunk chunk{manifest.id,0,{data->begin(),data->begin()+baseline_chunk_bytes}},decoded_chunk;
    require(decode_world_chunk(encode_world_chunk(chunk),decoded_chunk) && decoded_chunk.bytes==chunk.bytes);
    const auto wire=encode_world_chunk(chunk);
    for (std::size_t n=0;n<wire.size();++n)
        require(!decode_world_chunk({wire.begin(),wire.begin()+n},decoded_chunk));
    BaselineAssembly assembly;
    require(!assembly.append(chunk) && assembly.begin(manifest) && !assembly.begin(manifest));
    auto wrong=chunk; wrong.offset=1; require(!assembly.append(wrong));
    wrong=chunk; wrong.baseline=1; require(!assembly.append(wrong));
    require(assembly.append(chunk) && !assembly.append(chunk) && !assembly.complete());
    assembly.clear(); require(assembly.bytes().empty() && !assembly.manifest().id);
    HostPump host; ClientPump client; Identity token=100;
    auto links=MemoryTransport::pair(); auto* raw_client=links.first.get();
    bool stalled=false;
    host.start(1,1,{1,1},[&] { return ++token; });
    require(host.attach(1,std::make_unique<StalledTransport>(std::move(links.second),stalled)));
    client.start(std::move(links.first),2,{1,1});
    auto pump=[&] { host.update(.01); client.update(.01); };
    for (unsigned n=0;n<10;++n) pump();
    const auto player=client.session().welcome().player;
    require(host.ready_participants()==2);
    unsigned deliveries=0;
    client.set_baseline_sink([&](const WorldBaseline& value,const std::vector<std::uint8_t>& bytes) {
        ++deliveries; return same_baseline(value,manifest) && bytes==*data;
    });
    require(host.send_baseline(player,manifest,data));
    require(!host.assign_level(player,10,500));
    require(!client.acknowledge_baseline());
    stalled=true;
    for (unsigned n=0;n<200;++n) pump();
    require(host.ready_participants()==2 && client.session().state()==ClientState::Connected && !deliveries);
    stalled=false;
    for (unsigned n=0;n<200 && !deliveries;++n) pump();
    require(deliveries==1 && client.session().state()==ClientState::Connected && !host.baseline_received(player));
    require(!client.baseline_acknowledged() && client.acknowledge_baseline());
    pump(); require(host.baseline_received(player));
    require(host.assign_level(player,10,500)); pump(); require(client.acknowledge_level(10)); pump();
    require(!client.acknowledge_baseline());
    auto forged=manifest; forged.digest[1]=1;
    require(raw_client->send({Message::WorldReceived,Channel::World,Delivery::ReliableOrdered,0,encode_baseline(forged)})==SendResult::Sent);
    pump(); require(host.ready_participants()==1);
    client.stop(); host.stop();
    links=MemoryTransport::pair(); host.start(1,1,{1,1},[&] { return ++token; });
    require(host.attach(2,std::move(links.second))); client.start(std::move(links.first),2,{1,1});
    for (unsigned n=0;n<10;++n) pump();
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return false; });
    require(host.send_baseline(client.session().welcome().player,manifest,data));
    for (unsigned n=0;n<200 && client.session().state()==ClientState::Connected;++n) pump();
    require(client.session().state()==ClientState::Disconnected && !client.baseline().id);
    client.stop(); host.stop();
    std::cout<<"CoopNet world baseline codec, bounded streaming, validation, loading barrier and forged acknowledgement tests passed\n";
}

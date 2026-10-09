#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <cstdlib>
#include <iostream>
using namespace coopnet;
void require(bool value) { if(!value) std::abort(); }
int main() {
    require(health_color(0)==HealthColor::Black);
    require(health_color(.001f)==HealthColor::Red && health_color(.25f)==HealthColor::Red);
    require(health_color(.251f)==HealthColor::Orange && health_color(.50f)==HealthColor::Orange);
    require(health_color(.501f)==HealthColor::Yellow && health_color(.75f)==HealthColor::Yellow && health_color(.899f)==HealthColor::Yellow);
    require(health_color(.90f)==HealthColor::Green && health_color(1)==HealthColor::Green);
    require(!valid_player_name("") && !valid_player_name(std::string(65,'a')) && !valid_player_name("bad\nname"));
    PlayerName value{2,"Guest %s"},decoded; auto bytes=encode_player_name(value);
    require(decode_player_name(bytes,decoded) && decoded.name==value.name);
    for(std::size_t i=0;i<bytes.size();++i) require(!decode_player_name({bytes.begin(),bytes.begin()+i},decoded));
    bytes.push_back(0); require(!decode_player_name(bytes,decoded));
    HostPump host; ClientPump client; Identity token=100;
    host.start(500,1,{10,20},[&]{return ++token;}); require(host.publish_name(1,"Host"));
    auto link=MemoryTransport::pair(); auto* wire=link.first.get();
    require(host.attach(1,std::move(link.second))); client.start(std::move(link.first),2,{10,20});
    for(unsigned i=0;i<6;++i) { client.update(.1); host.update(.1); }
    client.update(.1); require(client.player_name(1)=="Host");
    const auto player=client.session().welcome().player;
    require(client.send_name("Guest %s")==SendResult::Sent); host.update(.1); client.update(.1);
    require(host.player_name(player)=="Guest %s" && client.player_name(player)=="Guest %s");
    require(host.publish_name(1,"Host renamed")); host.update(.1); client.update(.1); require(client.player_name(1)=="Host renamed");
    require(client.send_name("Guest renamed")==SendResult::Sent); host.update(.1); client.update(.1); require(host.player_name(player)=="Guest renamed");
    require(wire->send({Message::PlayerName,Channel::Control,Delivery::ReliableOrdered,0,encode_player_name({1,"Spoofed"})})==SendResult::Sent);
    host.update(.1); client.update(.1); require(host.player_name(1)=="Host renamed");
    host.stop(); client.stop(); require(host.player_name(1).empty() && client.player_name(player).empty());
    std::cout<<"Player name and health color tests passed\n";
}

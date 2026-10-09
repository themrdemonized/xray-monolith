#pragma once
#include "Protocol.h"
#include <string>
namespace coopnet {
struct PlayerName { Identity player=0; std::string name; };
inline bool valid_player_name(const std::string& name) {
    if(name.empty() || name.size()>64) return false;
    for(unsigned char c:name) if(c<32 || c==127) return false;
    return true;
}
inline std::vector<std::uint8_t> encode_player_name(const PlayerName& value) {
    if(!value.player || !valid_player_name(value.name)) throw std::invalid_argument("Invalid player name");
    Writer w; w.integer(value.player,8); w.integer(value.name.size(),1);
    for(unsigned char c:value.name) w.integer(c,1); return w.bytes;
}
inline bool decode_player_name(const std::vector<std::uint8_t>& bytes,PlayerName& output) {
    Reader r(bytes); PlayerName value; std::uint64_t length,c;
    if(!r.integer(value.player,8) || !value.player || !r.integer(length,1) || length>64 || length!=r.remaining()) return false;
    for(unsigned i=0;i<length;++i) { if(!r.integer(c,1)) return false; value.name.push_back(static_cast<char>(c)); }
    if(!valid_player_name(value.name)) return false; output=std::move(value); return true;
}
enum class HealthColor { Black,Red,Orange,Yellow,Green };
inline HealthColor health_color(float health) {
    if(!std::isfinite(health) || health<=0) return HealthColor::Black;
    if(health<=.25f) return HealthColor::Red;
    if(health<=.50f) return HealthColor::Orange;
    if(health<.90f) return HealthColor::Yellow;
    return HealthColor::Green;
}
}

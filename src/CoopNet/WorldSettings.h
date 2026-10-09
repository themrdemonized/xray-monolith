#pragma once
#include "Gameplay.h"
#include <set>
#include <cstdlib>
namespace coopnet {
struct WorldRule { std::string name,value; std::uint8_t type=0; };
inline bool world_rule_path(const std::string& name) {
    return name.rfind("alife/",0)==0 || name.rfind("video/weather/",0)==0 || name.rfind("video/night/",0)==0 ||
        name.rfind("modded_exes/gameplay/",0)==0 || (name.rfind("gameplay/",0)==0 && name!="gameplay/general/player_name" &&
        name!="gameplay/general/outfit_portrait" && name!="gameplay/general/show_tip_reputation" &&
        name!="gameplay/gameplay_diff/notify_geiger" && name!="gameplay/gameplay_diff/notify_anomaly");
}
inline bool valid_world_rule(const WorldRule& rule) {
    if (!world_rule_path(rule.name) || rule.name.size()>96 || rule.value.size()>128 || rule.type>2) return false;
    for (unsigned char c:rule.name) if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='/' && c!='-') return false;
    if (rule.type==1) return rule.value=="true" || rule.value=="false";
    if (rule.type==2) { char* end=nullptr; const auto value=std::strtod(rule.value.c_str(),&end); return !rule.value.empty() && end==rule.value.c_str()+rule.value.size() && std::isfinite(value) && std::abs(value)<=1e9; }
    for (unsigned char c:rule.value) if (c<32 || c>126 || c==';' || c=='"' || c=='\\') return false;
    return true;
}
struct WorldRulesChunk { std::uint32_t revision=0; std::uint16_t offset=0,total=0; std::vector<WorldRule> rules; };
inline bool valid_world_rules_chunk(const WorldRulesChunk& chunk) {
    if (!chunk.revision || chunk.total>4096 || chunk.rules.size()>32 || chunk.offset>chunk.total ||
        chunk.offset+chunk.rules.size()>chunk.total || (chunk.total && chunk.rules.empty())) return false;
    std::set<std::string> names;
    for (const auto& rule:chunk.rules) if (!valid_world_rule(rule) || !names.insert(rule.name).second) return false;
    return true;
}
inline void settings_string(Writer& writer,const std::string& value) {
    writer.integer(value.size(),1); for (unsigned char c:value) writer.integer(c,1);
}
inline bool settings_string(Reader& reader,std::string& value,unsigned maximum) {
    std::uint64_t size; if (!reader.integer(size,1) || size>maximum) return false;
    for (unsigned i=0;i<size;++i) { std::uint64_t c; if (!reader.integer(c,1)) return false; value.push_back(static_cast<char>(c)); }
    return true;
}
inline std::vector<std::uint8_t> encode_world_rules(const WorldRulesChunk& chunk) {
    if (!valid_world_rules_chunk(chunk)) throw std::invalid_argument("Invalid host world rules");
    Writer writer; writer.integer(chunk.revision,4); writer.integer(chunk.offset,2); writer.integer(chunk.total,2); writer.integer(chunk.rules.size(),1);
    for (const auto& rule:chunk.rules) { settings_string(writer,rule.name); writer.integer(rule.type,1); settings_string(writer,rule.value); }
    return writer.bytes;
}
inline bool decode_world_rules(const std::vector<std::uint8_t>& bytes,WorldRulesChunk& output) {
    Reader reader(bytes); WorldRulesChunk chunk; std::uint64_t revision,offset,total,count;
    if (!reader.integer(revision,4) || !reader.integer(offset,2) || !reader.integer(total,2) || !reader.integer(count,1) || count>32) return false;
    chunk.revision=static_cast<std::uint32_t>(revision); chunk.offset=static_cast<std::uint16_t>(offset); chunk.total=static_cast<std::uint16_t>(total);
    for (unsigned i=0;i<count;++i) { WorldRule rule; std::uint64_t type; if (!settings_string(reader,rule.name,96) || !reader.integer(type,1) || !settings_string(reader,rule.value,128)) return false; rule.type=static_cast<std::uint8_t>(type); chunk.rules.push_back(std::move(rule)); }
    if (reader.remaining() || !valid_world_rules_chunk(chunk)) return false; output=std::move(chunk); return true;
}
class WorldRulesAssembly {
    std::uint32_t revision_=0; std::uint16_t total_=0; std::vector<WorldRule> rules_;
    std::set<std::string> names_;
public:
    void clear() { revision_=0; total_=0; rules_.clear(); names_.clear(); }
    bool append(const WorldRulesChunk& chunk,bool& complete,std::vector<WorldRule>& output) {
        complete=false; if (!valid_world_rules_chunk(chunk)) return false;
        if (!chunk.offset) { clear(); revision_=chunk.revision; total_=chunk.total; }
        if (chunk.revision!=revision_ || chunk.total!=total_ || chunk.offset!=rules_.size()) return false;
        for (const auto& rule:chunk.rules) if (!names_.insert(rule.name).second) return false;
        rules_.insert(rules_.end(),chunk.rules.begin(),chunk.rules.end());
        if (rules_.size()==total_) { complete=true; output=rules_; }
        return true;
    }
};
struct WorldClock {
    std::uint32_t level=0,tick=0; Identity game_time=0;
    float time_factor=1,fx_remaining=0; std::uint8_t difficulty=0;
    std::string cycle,fx;
};
inline bool valid_world_clock(const WorldClock& clock) {
    if (!clock.level || !clock.game_time || clock.game_time>100000000000000ull || clock.difficulty>3 ||
        !std::isfinite(clock.time_factor) || clock.time_factor<0 || clock.time_factor>1000 ||
        !std::isfinite(clock.fx_remaining) || clock.fx_remaining<0 || clock.fx_remaining>1000000 || clock.cycle.empty()) return false;
    for (const auto* value:{&clock.cycle,&clock.fx}) {
        if (value->size()>64) return false;
        for (unsigned char c:*value) if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='-') return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_world_clock(const WorldClock& clock) {
    if (!valid_world_clock(clock)) throw std::invalid_argument("Invalid host world clock");
    Writer writer; writer.integer(clock.level,4); writer.integer(clock.tick,4); writer.integer(clock.game_time,8);
    write_float(writer,clock.time_factor); write_float(writer,clock.fx_remaining); writer.integer(clock.difficulty,1);
    settings_string(writer,clock.cycle); settings_string(writer,clock.fx); return writer.bytes;
}
inline bool decode_world_clock(const std::vector<std::uint8_t>& bytes,WorldClock& output) {
    Reader reader(bytes); WorldClock clock; std::uint64_t level,tick,difficulty;
    if (!reader.integer(level,4) || !reader.integer(tick,4) || !reader.integer(clock.game_time,8) || !read_float(reader,clock.time_factor) ||
        !read_float(reader,clock.fx_remaining) || !reader.integer(difficulty,1) || !settings_string(reader,clock.cycle,64) || !settings_string(reader,clock.fx,64)) return false;
    clock.level=static_cast<std::uint32_t>(level); clock.tick=static_cast<std::uint32_t>(tick); clock.difficulty=static_cast<std::uint8_t>(difficulty);
    if (reader.remaining() || !valid_world_clock(clock)) return false; output=std::move(clock); return true;
}
}

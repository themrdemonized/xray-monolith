#pragma once
#include "WorldState.h"
#include <string>
#include <memory>
namespace coopnet {
// Complete, bounded host snapshots; never native spawn/save packets or Lua code.
enum class SharedKind : std::uint8_t { NPC, Quests, Containers };
constexpr unsigned shared_kind_count=3;
constexpr std::size_t shared_limit=1024*1024;
inline bool shared_name(const std::string& value,std::size_t limit,bool path=false) {
    if (value.empty() || value.size()>limit || value.find("..")!=std::string::npos) return false;
    for (unsigned char c:value) if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || (path && (c=='/' || c=='\\' || c=='.')))) return false;
    return !path || (value.front()!='/' && value.front()!='\\');
}
struct SharedWriter {
    std::vector<std::uint8_t> bytes;
    void integer(std::uint64_t n,unsigned width) { if (bytes.size()+width>shared_limit) throw std::length_error("Shared world limit"); for (unsigned i=0;i<width;++i) bytes.push_back(static_cast<std::uint8_t>(n>>(i*8))); }
    void string(const std::string& s) { if (s.size()>4096 || s.find('\0')!=std::string::npos) throw std::invalid_argument("Shared string"); integer(s.size(),2); for (unsigned char c:s) integer(c,1); }
    void number(float value) { std::uint32_t bits; std::memcpy(&bits,&value,4); integer(bits,4); }
};
inline bool shared_string(Reader& r,std::string& value,std::size_t limit) {
    std::uint64_t n,c; if (!r.integer(n,2) || n>limit || n>r.remaining()) return false;
    value.clear(); for (unsigned i=0;i<n;++i) { if (!r.integer(c,1) || !c) return false; value.push_back(static_cast<char>(c)); } return true;
}
inline bool shared_number(Reader& r,float& value) { std::uint64_t bits; if (!r.integer(bits,4)) return false; const auto b=static_cast<std::uint32_t>(bits); std::memcpy(&value,&b,4); return std::isfinite(value) && std::abs(value)<=1000000; }
struct NPCRecord { WorldPose pose; std::string section,visual; };
inline bool valid_npc(const NPCRecord& n) { return shared_name(n.section,96) && (n.visual.empty() || shared_name(n.visual,192,true)) && valid_world_state({1,0,{n.pose}}); }
inline std::vector<std::uint8_t> encode_npcs(const std::vector<NPCRecord>& records) {
    if (records.size()>4096) throw std::length_error("NPC catalogue limit");
    SharedWriter w; w.integer(records.size(),2); std::set<Identity> ids;
    for (const auto& n:records) {
        if (!valid_npc(n) || !ids.insert(n.pose.anchor).second) throw std::invalid_argument("NPC catalogue");
        w.integer(n.pose.anchor,8); w.integer(n.pose.incarnation,8); w.string(n.section); w.string(n.visual);
        for (const auto& v:{n.pose.position,n.pose.rotation}) for (float f:v) w.number(f); w.number(n.pose.health);
    } return w.bytes;
}
inline bool decode_npcs(const std::vector<std::uint8_t>& bytes,std::vector<NPCRecord>& output) {
    if (bytes.size()>shared_limit) return false; Reader r(bytes); std::uint64_t count; if (!r.integer(count,2) || count>4096) return false;
    std::vector<NPCRecord> result; std::set<Identity> ids;
    for (unsigned i=0;i<count;++i) {
        NPCRecord n; if (!r.integer(n.pose.anchor,8) || !r.integer(n.pose.incarnation,8) || !shared_string(r,n.section,96) || !shared_string(r,n.visual,192)) return false;
        for (auto* v:{&n.pose.position,&n.pose.rotation}) for (auto& f:*v) if (!shared_number(r,f)) return false;
        if (!shared_number(r,n.pose.health) || !valid_npc(n) || !ids.insert(n.pose.anchor).second) return false; result.push_back(std::move(n));
    } if (r.remaining()) return false; output=std::move(result); return true;
}
struct ContainerRecord { WorldPose pose; std::string section; bool closed=false,can_take=true; };
inline bool valid_container(const ContainerRecord& c) { return shared_name(c.section,96) && valid_world_state({1,0,{c.pose}}); }
inline std::vector<std::uint8_t> encode_containers(const std::vector<ContainerRecord>& records) {
    if (records.size()>4096) throw std::length_error("Container limit");
    SharedWriter w; w.integer(records.size(),2); std::set<Identity> ids;
    for (const auto& c:records) {
        if (!valid_container(c) || !ids.insert(c.pose.anchor).second) throw std::invalid_argument("Container record");
        w.integer(c.pose.anchor,8); w.integer(c.pose.incarnation,8); w.string(c.section);
        for (const auto& v:{c.pose.position,c.pose.rotation}) for (float f:v) w.number(f);
        w.integer(c.closed,1); w.integer(c.can_take,1);
    } return w.bytes;
}
inline bool decode_containers(const std::vector<std::uint8_t>& bytes,std::vector<ContainerRecord>& output) {
    if (bytes.size()>shared_limit) return false; Reader r(bytes); std::uint64_t count,n;
    if (!r.integer(count,2) || count>4096) return false;
    std::vector<ContainerRecord> result; std::set<Identity> ids;
    for (unsigned i=0;i<count;++i) {
        ContainerRecord c;
        if (!r.integer(c.pose.anchor,8) || !r.integer(c.pose.incarnation,8) || !shared_string(r,c.section,96)) return false;
        for (auto* v:{&c.pose.position,&c.pose.rotation}) for (auto& f:*v) if (!shared_number(r,f)) return false;
        if (!r.integer(n,1) || n>1) return false; c.closed=n!=0;
        if (!r.integer(n,1) || n>1) return false; c.can_take=n!=0;
        if (!valid_container(c) || !ids.insert(c.pose.anchor).second) return false; result.push_back(std::move(c));
    }
    if (r.remaining()) return false; output=std::move(result); return true;
}
struct QuestRecord {
    std::string id,title,description,icon,hint,spot;
    std::uint8_t state=1,type=0;
    Identity target=0;
    std::uint32_t priority=0;
    std::array<std::uint64_t,4> times{};
};
struct QuestState { std::vector<QuestRecord> tasks; std::vector<std::string> infos; };
inline bool valid_quest(const QuestRecord& q) {
    return shared_name(q.id,128) && q.state<=2 && (q.type<=1 || q.type==255) && q.title.size()<=1024 && q.description.size()<=4096 &&
        (q.icon.empty() || shared_name(q.icon,192,true)) && q.hint.size()<=1024 && (q.spot.empty() || shared_name(q.spot,96));
}
inline std::vector<std::uint8_t> encode_quests(const QuestState& state) {
    if (state.tasks.size()>1024 || state.infos.size()>16384) throw std::length_error("Quest limit");
    SharedWriter w; w.integer(state.tasks.size(),2); std::set<std::string> ids;
    for (const auto& q:state.tasks) {
        if (!valid_quest(q) || !ids.insert(q.id).second) throw std::invalid_argument("Quest record");
        for (const auto* s:{&q.id,&q.title,&q.description,&q.icon,&q.hint,&q.spot}) w.string(*s);
        w.integer(q.state,1); w.integer(q.type,1); w.integer(q.target,8); w.integer(q.priority,4); for (auto t:q.times) w.integer(t,8);
    } w.integer(state.infos.size(),2); ids.clear(); for (const auto& s:state.infos) { if (!shared_name(s,128) || !ids.insert(s).second) throw std::invalid_argument("Info record"); w.string(s); } return w.bytes;
}
inline bool decode_quests(const std::vector<std::uint8_t>& bytes,QuestState& output) {
    if (bytes.size()>shared_limit) return false; Reader r(bytes); std::uint64_t count,n; if (!r.integer(count,2) || count>1024) return false;
    QuestState result; std::set<std::string> ids;
    for (unsigned i=0;i<count;++i) {
        QuestRecord q; if (!shared_string(r,q.id,128) || !shared_string(r,q.title,1024) || !shared_string(r,q.description,4096) || !shared_string(r,q.icon,192) || !shared_string(r,q.hint,1024) || !shared_string(r,q.spot,96)) return false;
        if (!r.integer(n,1)) return false; q.state=static_cast<std::uint8_t>(n); if (!r.integer(n,1)) return false; q.type=static_cast<std::uint8_t>(n);
        if (!r.integer(q.target,8) || !r.integer(n,4)) return false; q.priority=static_cast<std::uint32_t>(n); for (auto& t:q.times) if (!r.integer(t,8)) return false;
        if (!valid_quest(q) || !ids.insert(q.id).second) return false; result.tasks.push_back(std::move(q));
    }
    if (!r.integer(count,2) || count>16384) return false; ids.clear(); for (unsigned i=0;i<count;++i) { std::string s; if (!shared_string(r,s,128) || !shared_name(s,128) || !ids.insert(s).second) return false; result.infos.push_back(std::move(s)); }
    if (r.remaining()) return false; output=std::move(result); return true;
}
struct SharedChunk { SharedKind kind=SharedKind::NPC; std::uint32_t level=0,revision=0,total=0,offset=0; std::vector<std::uint8_t> bytes; };
inline bool valid_shared_chunk(const SharedChunk& c) { return static_cast<unsigned>(c.kind)<shared_kind_count && c.level && c.revision && c.total && c.total<=shared_limit && c.offset<c.total && c.offset%8192==0 && c.bytes.size()==(std::min)(std::size_t(8192),std::size_t(c.total-c.offset)); }
inline std::vector<std::uint8_t> encode_shared_chunk(const SharedChunk& c) {
    if (!valid_shared_chunk(c)) throw std::invalid_argument("Shared chunk"); Writer w; w.integer(static_cast<unsigned>(c.kind),1); for (auto n:{c.level,c.revision,c.total,c.offset}) w.integer(n,4); w.bytes.insert(w.bytes.end(),c.bytes.begin(),c.bytes.end()); return w.bytes;
}
inline bool decode_shared_chunk(const std::vector<std::uint8_t>& bytes,SharedChunk& output) {
    Reader r(bytes); SharedChunk c; std::uint64_t n; if (!r.integer(n,1)) return false; c.kind=static_cast<SharedKind>(n);
    for (auto* v:{&c.level,&c.revision,&c.total,&c.offset}) { if (!r.integer(n,4)) return false; *v=static_cast<std::uint32_t>(n); }
    c.bytes.assign(bytes.begin()+17,bytes.end()); if (!valid_shared_chunk(c)) return false; output=std::move(c); return true;
}
class SharedAssembly {
    SharedChunk header_;
    std::vector<std::uint8_t> bytes_;
public:
    void clear() { header_={}; bytes_.clear(); }
    bool accept(const SharedChunk& c) {
        if (!valid_shared_chunk(c)) return false;
        if (!c.offset) { header_=c; bytes_.clear(); bytes_.reserve(c.total); }
        if (c.kind!=header_.kind || c.level!=header_.level || c.revision!=header_.revision || c.total!=header_.total || c.offset!=bytes_.size()) return false;
        bytes_.insert(bytes_.end(),c.bytes.begin(),c.bytes.end()); return true;
    }
    bool complete() const { return header_.total && bytes_.size()==header_.total; }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
};
}

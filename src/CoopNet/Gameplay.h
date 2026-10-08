#pragma once
#include "ActorSnapshot.h"
#include <string>
namespace coopnet {
enum class InventoryAction : std::uint8_t { Take = 1, Drop = 2 };
enum class InventoryStatus : std::uint8_t { Accepted, Unavailable, Conflict, Denied, OutOfRange, Capacity, Busy, Expired };
struct InventoryRequest {
    Identity actor = 0, item = 0;
    std::uint32_t generation = 0, level = 0, sequence = 0, revision = 0;
    InventoryAction action = InventoryAction::Take;
};
struct InventoryResult {
    Identity item = 0, owner = 0;
    std::uint32_t sequence = 0, revision = 0;
    InventoryStatus status = InventoryStatus::Unavailable;
};
struct ItemState {
    Identity item = 0, owner = 0;
    std::uint32_t level = 0, revision = 0;
    bool present = true;
    std::string section;
};
struct ActorVitals {
    Identity actor = 0;
    std::uint32_t generation = 0, level = 0, tick = 0;
    float health = 1, power = 1, radiation = 0;
};
inline void write_float(Writer& w, float value) {
    std::uint32_t bits; std::memcpy(&bits,&value,4); w.integer(bits,4);
}
inline bool read_float(Reader& r, float& value) {
    std::uint64_t bits; if (!r.integer(bits,4)) return false;
    const auto encoded = static_cast<std::uint32_t>(bits); std::memcpy(&value,&encoded,4); return true;
}
inline bool valid_inventory_request(const InventoryRequest& r) {
    return r.actor && r.item && r.generation && r.level && r.revision &&
        (r.action == InventoryAction::Take || r.action == InventoryAction::Drop);
}
inline std::vector<std::uint8_t> encode_inventory_request(const InventoryRequest& r) {
    if (!valid_inventory_request(r)) throw std::invalid_argument("Invalid inventory request");
    Writer w; w.integer(r.actor,8); w.integer(r.item,8); w.integer(r.generation,4);
    w.integer(r.level,4); w.integer(r.sequence,4); w.integer(r.revision,4); w.integer(static_cast<unsigned>(r.action),1);
    return w.bytes;
}
inline bool decode_inventory_request(const std::vector<std::uint8_t>& bytes, InventoryRequest& output) {
    Reader r(bytes); InventoryRequest v; std::uint64_t g,l,s,revision,action;
    if (!r.integer(v.actor,8) || !r.integer(v.item,8) || !r.integer(g,4) || !r.integer(l,4) ||
        !r.integer(s,4) || !r.integer(revision,4) || !r.integer(action,1) || r.remaining()) return false;
    v.generation=static_cast<std::uint32_t>(g); v.level=static_cast<std::uint32_t>(l);
    v.sequence=static_cast<std::uint32_t>(s); v.revision=static_cast<std::uint32_t>(revision);
    v.action=static_cast<InventoryAction>(action);
    if (!valid_inventory_request(v)) return false; output=v; return true;
}
inline std::vector<std::uint8_t> encode_inventory_result(const InventoryResult& v) {
    if (!v.item || static_cast<unsigned>(v.status)>7) throw std::invalid_argument("Invalid inventory result");
    Writer w; w.integer(v.item,8); w.integer(v.owner,8); w.integer(v.sequence,4);
    w.integer(v.revision,4); w.integer(static_cast<unsigned>(v.status),1); return w.bytes;
}
inline bool decode_inventory_result(const std::vector<std::uint8_t>& bytes, InventoryResult& output) {
    Reader r(bytes); InventoryResult v; std::uint64_t s,revision,status;
    if (!r.integer(v.item,8) || !r.integer(v.owner,8) || !r.integer(s,4) || !r.integer(revision,4) ||
        !r.integer(status,1) || r.remaining() || !v.item || status>7) return false;
    v.sequence=static_cast<std::uint32_t>(s); v.revision=static_cast<std::uint32_t>(revision);
    v.status=static_cast<InventoryStatus>(status); output=v; return true;
}
inline bool valid_item_state(const ItemState& v) {
    if (!v.item || !v.level || !v.revision || v.section.empty() || v.section.size()>128 || (!v.present && v.owner)) return false;
    for (const unsigned char c : v.section)
        if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='-') return false;
    return true;
}
inline std::vector<std::uint8_t> encode_item_state(const ItemState& v) {
    if (!valid_item_state(v)) throw std::invalid_argument("Invalid item state");
    Writer w; w.integer(v.item,8); w.integer(v.owner,8); w.integer(v.level,4); w.integer(v.revision,4);
    w.integer(v.present,1); w.integer(v.section.size(),1); w.bytes.insert(w.bytes.end(),v.section.begin(),v.section.end()); return w.bytes;
}
inline bool decode_item_state(const std::vector<std::uint8_t>& bytes, ItemState& output) {
    Reader r(bytes); ItemState v; std::uint64_t l,revision,present,length;
    if (!r.integer(v.item,8) || !r.integer(v.owner,8) || !r.integer(l,4) || !r.integer(revision,4) ||
        !r.integer(present,1) || !r.integer(length,1) || present>1 || length!=r.remaining()) return false;
    v.level=static_cast<std::uint32_t>(l); v.revision=static_cast<std::uint32_t>(revision); v.present=present!=0;
    v.section.assign(bytes.end()-static_cast<std::size_t>(length),bytes.end());
    if (!valid_item_state(v)) return false; output=std::move(v); return true;
}
inline bool valid_vitals(const ActorVitals& v) {
    return v.actor && v.generation && v.level && std::isfinite(v.health) && v.health>=-1 && v.health<=1 &&
        std::isfinite(v.power) && v.power>=-1 && v.power<=1 && std::isfinite(v.radiation) && v.radiation>=0 && v.radiation<=1;
}
inline std::vector<std::uint8_t> encode_vitals(const ActorVitals& v) {
    if (!valid_vitals(v)) throw std::invalid_argument("Invalid actor vitals");
    Writer w; w.integer(v.actor,8); w.integer(v.generation,4); w.integer(v.level,4); w.integer(v.tick,4);
    write_float(w,v.health); write_float(w,v.power); write_float(w,v.radiation); return w.bytes;
}
inline bool decode_vitals(const std::vector<std::uint8_t>& bytes, ActorVitals& output) {
    Reader r(bytes); ActorVitals v; std::uint64_t g,l,t;
    if (!r.integer(v.actor,8) || !r.integer(g,4) || !r.integer(l,4) || !r.integer(t,4) ||
        !read_float(r,v.health) || !read_float(r,v.power) || !read_float(r,v.radiation) || r.remaining()) return false;
    v.generation=static_cast<std::uint32_t>(g); v.level=static_cast<std::uint32_t>(l); v.tick=static_cast<std::uint32_t>(t);
    if (!valid_vitals(v)) return false; output=v; return true;
}
}

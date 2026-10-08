#pragma once
#include "EngineActorBridge.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace engine_coopnet {
constexpr std::size_t max_guest_save=5*1024*1024;
struct GuestSave {
    std::uint64_t scope=0,character=0,game=0,mods=0,sequence=0;
    ActorConditionState condition;
    GuestInventoryState inventory;
};
inline bool valid_guest_save(const GuestSave& v) {
    if (!v.scope || !v.character || !v.game || !v.mods || !v.sequence || v.inventory.items.size()>256 ||
        !std::isfinite(v.condition.health) || v.condition.health< -1 || v.condition.health>1 ||
        !std::isfinite(v.condition.power) || v.condition.power< -1 || v.condition.power>1 ||
        !std::isfinite(v.condition.radiation) || v.condition.radiation<0 || v.condition.radiation>1) return false;
    std::size_t total=64;
    for (const auto& item:v.inventory.items) {
        if (item.section.empty() || item.section.size()>128 || item.spawn.empty() || item.spawn.size()>=16384) return false;
        for (unsigned char c:item.section) if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-')) return false;
        total+=item.section.size()+item.spawn.size()+3;
        if (total>max_guest_save) return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_guest_save(const GuestSave& v) {
    if (!valid_guest_save(v)) throw std::invalid_argument("Invalid guest save");
    std::vector<std::uint8_t> bytes;
    auto integer=[&](std::uint64_t value,unsigned width) {
        for (unsigned i=0;i<width;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(8*i)));
    };
    integer(0x31534347,4); // GCS1: host-local format, never a network payload.
    for (auto value:{v.scope,v.character,v.game,v.mods,v.sequence}) integer(value,8);
    for (float value:{v.condition.health,v.condition.power,v.condition.radiation}) {
        std::uint32_t bits; std::memcpy(&bits,&value,4); integer(bits,4);
    }
    integer(v.inventory.active_slot,2); integer(v.inventory.items.size(),2);
    for (const auto& item:v.inventory.items) {
        integer(item.section.size(),1); integer(item.spawn.size(),2);
        bytes.insert(bytes.end(),item.section.begin(),item.section.end());
        bytes.insert(bytes.end(),item.spawn.begin(),item.spawn.end());
    }
    return bytes;
}
inline bool decode_guest_save(const std::vector<std::uint8_t>& bytes,GuestSave& output) {
    if (bytes.size()>max_guest_save) return false;
    std::size_t offset=0;
    auto integer=[&](std::uint64_t& value,unsigned width) {
        if (width>bytes.size()-offset) return false;
        value=0; for (unsigned i=0;i<width;++i) value|=std::uint64_t(bytes[offset++])<<(8*i);
        return true;
    };
    GuestSave v; std::uint64_t magic,slot,count;
    if (!integer(magic,4) || magic!=0x31534347 || !integer(v.scope,8) || !integer(v.character,8) ||
        !integer(v.game,8) || !integer(v.mods,8) || !integer(v.sequence,8)) return false;
    for (auto* value:{&v.condition.health,&v.condition.power,&v.condition.radiation}) {
        std::uint64_t raw; if (!integer(raw,4)) return false;
        const auto bits=static_cast<std::uint32_t>(raw); std::memcpy(value,&bits,4);
    }
    if (!integer(slot,2) || !integer(count,2) || count>256) return false;
    v.inventory.active_slot=static_cast<std::uint16_t>(slot);
    for (std::uint64_t i=0;i<count;++i) {
        std::uint64_t section,size;
        if (!integer(section,1) || !integer(size,2) || section>128 || !section || !size || size>=16384 ||
            section+size>bytes.size()-offset) return false;
        GuestInventoryItem item;
        item.section.assign(bytes.begin()+offset,bytes.begin()+offset+static_cast<std::size_t>(section)); offset+=static_cast<std::size_t>(section);
        item.spawn.assign(bytes.begin()+offset,bytes.begin()+offset+static_cast<std::size_t>(size)); offset+=static_cast<std::size_t>(size);
        v.inventory.items.push_back(std::move(item));
    }
    if (offset!=bytes.size() || !valid_guest_save(v)) return false;
    output=std::move(v); return true;
}
}

#pragma once
#include "ActorSnapshot.h"
#include <set>
namespace coopnet {
// Baseline-local anchors are session-scoped identities, never engine object IDs.
inline Identity world_anchor(Identity session, std::uint16_t saved_object) {
    auto value=(session | (Identity(1)<<63)) ^ Identity(saved_object);
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
struct WorldPose {
    Identity anchor=0, incarnation=0;
    std::array<float,3> position{}, rotation{};
    float health=0;
};
struct WorldState {
    std::uint32_t level=0, tick=0;
    std::vector<WorldPose> objects;
};
inline bool valid_world_state(const WorldState& state) {
    if (!state.level || state.objects.empty() || state.objects.size()>128) return false;
    std::set<Identity> unique;
    for (const auto& object:state.objects) {
        if (!object.anchor || !object.incarnation || !unique.insert(object.anchor).second ||
            !std::isfinite(object.health) || object.health < -1 || object.health>1) return false;
        for (const auto& vector:{object.position,object.rotation})
            for (const auto value:vector) if (!std::isfinite(value) || std::abs(value)>1000000) return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_world_state(const WorldState& state) {
    if (!valid_world_state(state)) throw std::invalid_argument("Invalid world state");
    Writer writer; writer.integer(state.level,4); writer.integer(state.tick,4); writer.integer(state.objects.size(),2);
    for (const auto& object:state.objects) {
        writer.integer(object.anchor,8); writer.integer(object.incarnation,8);
        for (const auto& vector:{object.position,object.rotation}) for (const auto value:vector) {
            std::uint32_t bits; std::memcpy(&bits,&value,4); writer.integer(bits,4);
        }
        std::uint32_t bits; std::memcpy(&bits,&object.health,4); writer.integer(bits,4);
    }
    return writer.bytes;
}
inline bool decode_world_state(const std::vector<std::uint8_t>& bytes,WorldState& output) {
    Reader reader(bytes); std::uint64_t level,tick,count;
    if (!reader.integer(level,4) || !reader.integer(tick,4) || !reader.integer(count,2) || !count || count>128 ||
        reader.remaining()!=count*44) return false;
    WorldState state; state.level=static_cast<std::uint32_t>(level); state.tick=static_cast<std::uint32_t>(tick);
    state.objects.resize(static_cast<std::size_t>(count));
    for (auto& object:state.objects) {
        if (!reader.integer(object.anchor,8) || !reader.integer(object.incarnation,8)) return false;
        for (auto* vector:{&object.position,&object.rotation}) for (auto& value:*vector) {
            std::uint64_t bits; if (!reader.integer(bits,4)) return false;
            const auto number=static_cast<std::uint32_t>(bits); std::memcpy(&value,&number,4);
        }
        std::uint64_t bits; if (!reader.integer(bits,4)) return false;
        const auto number=static_cast<std::uint32_t>(bits); std::memcpy(&object.health,&number,4);
    }
    if (!valid_world_state(state)) return false;
    output=std::move(state); return true;
}
}

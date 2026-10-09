#pragma once
#include "Protocol.h"
#include <cstring>
#include <limits>
#include <array>
namespace coopnet {
// Held controls, not movement results. Simulation duration comes from the host clock.
// Bits match native actor wishes: forward/back/strafe/crouch/accel/jump/sprint/lookout.
constexpr std::uint16_t fire_button=0x8000, reload_button=0x0800;
constexpr std::uint16_t input_buttons = 0xf8bf;
struct ActorInput {
    Identity entity = 0;
    std::uint32_t generation = 0, level = 0, sequence = 0;
    std::uint16_t buttons = 0;
    float yaw = 0, pitch = 0;
    bool has_pose=false;
    std::array<float,3> position{},velocity{};
};
inline bool valid_input(const ActorInput& value) {
    if(value.has_pose) {
        for(float n:value.position) if(!std::isfinite(n) || std::abs(n)>1000000.f) return false;
        for(float n:value.velocity) if(!std::isfinite(n) || std::abs(n)>100.f) return false;
    }
    return value.entity && value.generation && value.level && !(value.buttons & ~input_buttons) &&
        std::isfinite(value.yaw) && std::isfinite(value.pitch) &&
        std::abs(value.yaw) <= 3.141593f && std::abs(value.pitch) <= 1.570797f;
}
inline std::vector<std::uint8_t> encode_input(const ActorInput& value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE754 floats required");
    if (!valid_input(value)) throw std::invalid_argument("Invalid actor input");
    Writer writer;
    writer.integer(value.entity, 8); writer.integer(value.generation, 4);
    writer.integer(value.level, 4); writer.integer(value.sequence, 4); writer.integer(value.buttons, 2);
    for (const auto number : {value.yaw,value.pitch}) {
        std::uint32_t bits; std::memcpy(&bits,&number,sizeof(bits)); writer.integer(bits,4);
    }
    writer.integer(value.has_pose ? 1 : 0,1);
    for(const auto& vector:{value.position,value.velocity}) for(float n:vector) {
        std::uint32_t bits; std::memcpy(&bits,&n,4); writer.integer(bits,4);
    }
    return writer.bytes;
}
inline bool decode_input(const std::vector<std::uint8_t>& bytes, ActorInput& output) {
    Reader reader(bytes); ActorInput value;
    std::uint64_t generation, level, sequence, buttons;
    if (!reader.integer(value.entity,8) || !reader.integer(generation,4) || !reader.integer(level,4) ||
        !reader.integer(sequence,4) || !reader.integer(buttons,2)) return false;
    value.generation = static_cast<std::uint32_t>(generation); value.level = static_cast<std::uint32_t>(level);
    value.sequence = static_cast<std::uint32_t>(sequence); value.buttons = static_cast<std::uint16_t>(buttons);
    for (auto* number : {&value.yaw,&value.pitch}) {
        std::uint64_t encoded;
        if (!reader.integer(encoded,4)) return false;
        const auto bits = static_cast<std::uint32_t>(encoded); std::memcpy(number,&bits,sizeof(bits));
    }
    std::uint64_t has_pose;
    if(!reader.integer(has_pose,1) || has_pose>1) return false;
    value.has_pose=has_pose!=0;
    for(auto* vector:{&value.position,&value.velocity}) for(float& n:*vector) {
        std::uint64_t bits; if(!reader.integer(bits,4)) return false;
        const auto encoded=static_cast<std::uint32_t>(bits); std::memcpy(&n,&encoded,4);
    }
    if (reader.remaining() || !valid_input(value)) return false;
    output = value; return true;
}
}

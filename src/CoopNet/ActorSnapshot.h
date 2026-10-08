#pragma once
#include "Protocol.h"
#include <array>
#include <cstring>
#include <deque>
#include <limits>
namespace coopnet {
struct ActorSnapshot {
    Identity entity = 0;
    std::uint32_t generation = 0, level = 0, tick = 0;
    std::uint64_t time_us = 0;
    std::array<float, 3> position{}, velocity{}, rotation{};
    std::uint16_t movement = 0;
    std::uint8_t stance = 0;
};
inline bool valid_snapshot(const ActorSnapshot& snapshot) {
    if (!snapshot.entity || !snapshot.generation || !snapshot.level || snapshot.stance > 3) return false;
    for (const auto& vector : {snapshot.position, snapshot.velocity, snapshot.rotation})
        for (float value : vector) if (!std::isfinite(value)) return false;
    return true;
}
inline std::vector<std::uint8_t> encode_snapshot(const ActorSnapshot& snapshot) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE754 floats required");
    if (!valid_snapshot(snapshot)) throw std::invalid_argument("Invalid actor snapshot");
    Writer writer;
    writer.integer(snapshot.entity, 8); writer.integer(snapshot.generation, 4);
    writer.integer(snapshot.level, 4); writer.integer(snapshot.tick, 4); writer.integer(snapshot.time_us, 8);
    for (const auto& vector : {snapshot.position, snapshot.velocity, snapshot.rotation})
        for (float value : vector) {
            std::uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); writer.integer(bits, 4);
        }
    writer.integer(snapshot.movement, 2); writer.integer(snapshot.stance, 1);
    return writer.bytes;
}
inline bool decode_snapshot(const std::vector<std::uint8_t>& bytes, ActorSnapshot& output) {
    Reader reader(bytes); ActorSnapshot value;
    std::uint64_t generation, level, tick, movement, stance;
    if (!reader.integer(value.entity, 8) || !reader.integer(generation, 4) ||
        !reader.integer(level, 4) || !reader.integer(tick, 4) || !reader.integer(value.time_us, 8)) return false;
    value.generation = static_cast<std::uint32_t>(generation); value.level = static_cast<std::uint32_t>(level);
    value.tick = static_cast<std::uint32_t>(tick);
    for (auto* vector : {&value.position, &value.velocity, &value.rotation})
        for (float& number : *vector) {
            std::uint64_t encoded;
            if (!reader.integer(encoded, 4)) return false;
            const auto bits = static_cast<std::uint32_t>(encoded); std::memcpy(&number, &bits, sizeof(bits));
        }
    if (!reader.integer(movement, 2) || !reader.integer(stance, 1) || reader.remaining()) return false;
    value.movement = static_cast<std::uint16_t>(movement); value.stance = static_cast<std::uint8_t>(stance);
    if (!valid_snapshot(value)) return false;
    output = value; return true;
}
class SnapshotBuffer {
    Identity entity_ = 0;
    std::uint32_t generation_ = 0, level_ = 0;
    SequenceWindow ticks_;
    std::deque<ActorSnapshot> samples_;
public:
    void bind(Identity entity, std::uint32_t generation, std::uint32_t level) {
        if (!entity || !generation || !level) throw std::invalid_argument("Invalid actor binding");
        entity_ = entity; generation_ = generation; level_ = level; ticks_ = {}; samples_.clear();
    }
    std::size_t size() const { return samples_.size(); }
    bool push(const ActorSnapshot& snapshot) {
        if (!valid_snapshot(snapshot) || snapshot.entity != entity_ || snapshot.generation != generation_ ||
            snapshot.level != level_ || (!samples_.empty() && snapshot.time_us <= samples_.back().time_us) ||
            !ticks_.accept(snapshot.tick)) return false;
        samples_.push_back(snapshot); if (samples_.size() > 32) samples_.pop_front(); return true;
    }
    bool sample(std::uint64_t estimated_server_us, ActorSnapshot& output, std::uint64_t delay_us = 100000) const {
        if (samples_.empty()) return false;
        const auto target = estimated_server_us > delay_us ? estimated_server_us - delay_us : 0;
        if (target <= samples_.front().time_us) { output = samples_.front(); return true; }
        if (target >= samples_.back().time_us) { output = samples_.back(); return true; }
        for (std::size_t i = 1; i < samples_.size(); ++i) {
            const auto& right = samples_[i]; const auto& left = samples_[i - 1];
            if (target > right.time_us) continue;
            const double alpha = static_cast<double>(target - left.time_us) / (right.time_us - left.time_us);
            output = left; output.time_us = target;
            for (unsigned axis = 0; axis < 3; ++axis) {
                output.position[axis] = static_cast<float>(left.position[axis] * (1 - alpha) + right.position[axis] * alpha);
                output.velocity[axis] = static_cast<float>(left.velocity[axis] * (1 - alpha) + right.velocity[axis] * alpha);
                constexpr double tau = 6.283185307179586;
                const double delta = std::remainder(static_cast<double>(right.rotation[axis]) - left.rotation[axis], tau);
                output.rotation[axis] = static_cast<float>(std::remainder(left.rotation[axis] + delta * alpha, tau));
            }
            if (target == right.time_us) { output.movement = right.movement; output.stance = right.stance; output.tick = right.tick; }
            return true;
        }
        return false;
    }
};
}

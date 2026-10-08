#pragma once
#include "ActorSnapshot.h"
#include <map>
#include <string>
namespace coopnet {
struct ActorPresence {
    Identity entity = 0, player = 0, character = 0;
    std::uint32_t generation = 0, level = 0;
    std::string visual;
};
inline bool valid_actor_visual(const std::string& visual) {
    if (visual.size() > 191 || visual.find("..") != std::string::npos ||
        (!visual.empty() && (visual.front() == '/' || visual.front() == '\\'))) return false;
    for (const unsigned char c : visual)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '/' || c == '\\' || c == '.')) return false;
    return true;
}
inline bool valid_presence(const ActorPresence& value) {
    return value.entity && value.player && value.character && value.generation && value.level && valid_actor_visual(value.visual);
}
inline std::vector<std::uint8_t> encode_presence(const ActorPresence& value) {
    if (!valid_presence(value)) throw std::invalid_argument("Invalid actor presence");
    Writer writer;
    writer.integer(value.entity, 8); writer.integer(value.player, 8); writer.integer(value.character, 8);
    writer.integer(value.generation, 4); writer.integer(value.level, 4);
    writer.integer(value.visual.size(), 2);
    for (const unsigned char c : value.visual) writer.integer(c, 1);
    return writer.bytes;
}
inline bool decode_presence(const std::vector<std::uint8_t>& bytes, ActorPresence& output) {
    Reader reader(bytes); ActorPresence value; std::uint64_t generation, level;
    if (!reader.integer(value.entity, 8) || !reader.integer(value.player, 8) || !reader.integer(value.character, 8) ||
        !reader.integer(generation, 4) || !reader.integer(level, 4)) return false;
    std::uint64_t length;
    if (!reader.integer(length, 2) || length > 191 || length != reader.remaining()) return false;
    for (std::uint64_t i = 0; i < length; ++i) {
        std::uint64_t c; if (!reader.integer(c, 1)) return false;
        value.visual.push_back(static_cast<char>(c));
    }
    value.generation = static_cast<std::uint32_t>(generation); value.level = static_cast<std::uint32_t>(level);
    if (!valid_presence(value)) return false;
    output = value; return true;
}
// Reliable presence owns bindings. Unreliable poses cannot create or resurrect actors.
class ActorReplicas {
    struct Entry {
        ActorPresence presence; SnapshotBuffer snapshots; SequenceWindow ticks;
        std::uint64_t last_time = 0; bool active = false, sampled = false;
    };
    std::map<Identity, Entry> entries_;
public:
    bool create(const ActorPresence& value) {
        if (!valid_presence(value)) return false;
        auto found = entries_.find(value.entity);
        if (found != entries_.end()) {
            const auto& old = found->second.presence;
            if (old.player != value.player || old.character != value.character || value.generation < old.generation)
                return false;
            // Same-generation relevance re-entry is allowed after removal, but not a level reassignment.
            if (value.generation == old.generation && (old.level != value.level || old.visual != value.visual || found->second.active)) return false;
        } else if (entries_.size() >= 4096) return false;
        for (const auto& entry : entries_)
            if (entry.first != value.entity && entry.second.active && entry.second.presence.player == value.player) return false;
        auto& entry = entries_[value.entity];
        if (entry.presence.generation != value.generation) { entry.ticks = {}; entry.sampled = false; }
        entry.presence = value; entry.active = true;
        entry.snapshots.bind(value.entity, value.generation, value.level); return true;
    }
    bool remove(const ActorPresence& value) {
        auto found = entries_.find(value.entity);
        if (found == entries_.end()) return false;
        auto& entry = found->second; const auto& old = entry.presence;
        if (!entry.active || old.player != value.player || old.character != value.character ||
            old.generation != value.generation || old.level != value.level) return false;
        entry.active = false; entry.snapshots = {}; return true;
    }
    bool push(const ActorSnapshot& value) {
        auto found = entries_.find(value.entity);
        if (found == entries_.end()) return false;
        auto& entry = found->second;
        if (!entry.active || !valid_snapshot(value) || value.generation != entry.presence.generation ||
            value.level != entry.presence.level || (entry.sampled && value.time_us <= entry.last_time) ||
            !entry.ticks.accept(value.tick) || !entry.snapshots.push(value)) return false;
        entry.last_time = value.time_us; entry.sampled = true; return true;
    }
    bool sample(Identity entity, std::uint64_t server_us, ActorSnapshot& output) const {
        auto found = entries_.find(entity);
        return found != entries_.end() && found->second.active && found->second.snapshots.sample(server_us, output);
    }
    const ActorPresence* find(Identity entity) const {
        const auto found = entries_.find(entity);
        return found == entries_.end() || !found->second.active ? nullptr : &found->second.presence;
    }
    template<class Visitor> void visit(Visitor visitor) const {
        for (const auto& entry : entries_) if (entry.second.active) visitor(entry.second.presence);
    }
};
}

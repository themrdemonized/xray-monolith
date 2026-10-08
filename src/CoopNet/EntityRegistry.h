#pragma once
#include "Protocol.h"
#include <map>
#include <tuple>
namespace coopnet {
struct EngineKey {
    std::uint32_t worker = 0, level = 0;
    std::uint16_t object = 0xffff;
    bool operator<(const EngineKey& other) const {
        return std::tie(worker, level, object) < std::tie(other.worker, other.level, other.object);
    }
};
struct EntityBinding {
    Identity entity = 0;
    std::uint32_t generation = 0;
    EngineKey engine;
    bool active = false;
};
// Session-owned identities; raw engine pointers never enter this registry.
class EntityRegistry {
    std::map<Identity, EntityBinding> entries_;
    std::map<EngineKey, Identity> reverse_;
    Identity next_ = 1;
    std::size_t limit_;
public:
    explicit EntityRegistry(std::size_t limit = 4096) : limit_(limit) {
        if (!limit) throw std::invalid_argument("Empty entity registry capacity");
    }
    Identity create() {
        if (entries_.size() >= limit_ || next_ == UINT64_MAX) throw std::length_error("Entity registry full");
        const auto id = next_++; entries_.emplace(id, EntityBinding{id, 0, {}, false}); return id;
    }
    const EntityBinding* find(Identity id) const {
        const auto entry = entries_.find(id); return entry == entries_.end() ? nullptr : &entry->second;
    }
    Identity find_engine(EngineKey key) const {
        const auto entry = reverse_.find(key); return entry == reverse_.end() ? 0 : entry->second;
    }
    bool bind(Identity id, EngineKey key) {
        if (!key.worker || !key.level || key.object == 0xffff) return false;
        auto entry = entries_.find(id);
        if (entry == entries_.end() || entry->second.active || reverse_.count(key) ||
            entry->second.generation == UINT32_MAX) return false;
        entry->second.engine = key; entry->second.active = true; ++entry->second.generation;
        reverse_.emplace(key, id); return true;
    }
    bool unbind(Identity id, std::uint32_t generation) {
        auto entry = entries_.find(id);
        if (entry == entries_.end() || !entry->second.active || entry->second.generation != generation) return false;
        reverse_.erase(entry->second.engine); entry->second.active = false; return true;
    }
    void unload(std::uint32_t worker, std::uint32_t level) {
        for (auto& item : entries_) {
            auto& binding = item.second;
            if (binding.active && binding.engine.worker == worker && binding.engine.level == level) {
                reverse_.erase(binding.engine); binding.active = false;
            }
        }
    }
    bool erase(Identity id) {
        auto entry = entries_.find(id);
        if (entry == entries_.end() || entry->second.active) return false;
        entries_.erase(entry); return true;
    }
    bool matches(Identity id, std::uint32_t generation, std::uint32_t level) const {
        const auto* binding = find(id);
        return binding && binding->active && binding->generation == generation && binding->engine.level == level;
    }
};
}

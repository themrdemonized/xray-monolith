#pragma once
#include <cstdint>
#include <vector>
namespace engine_coopnet {
// Owner-thread save/load adapter. Names originate locally, never from network payloads.
bool capture_world_baseline(const char* name,std::uint32_t& level,std::vector<std::uint8_t>& bytes);
bool store_world_baseline(const char* name,const std::vector<std::uint8_t>& bytes);
bool load_world_baseline(const char* name);
bool world_baseline_loaded(const char* name);
}

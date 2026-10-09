#pragma once
#include <cstdint>
#include <vector>
#include "WorldSettings.h"
#include "SharedWorld.h"
class CObject;
class CGameObject;
class CSE_Abstract;
class ISheduled;
namespace engine_coopnet {
// Owner-thread save/load adapter. Names originate locally, never from network payloads.
bool capture_world_baseline(const char* name,std::uint32_t& level,std::vector<std::uint8_t>& bytes);
bool store_world_baseline(const char* name,const std::vector<std::uint8_t>& bytes);
bool load_world_baseline(const char* name);
bool world_baseline_loaded(const char* name);
bool read_join_profile_file(std::vector<std::uint8_t>& bytes);
bool write_join_profile_file(const std::vector<std::uint8_t>& bytes);
void export_settings_audit();
bool capture_world_rules(std::vector<coopnet::WorldRule>& rules);
void queue_host_world_rules(std::uint32_t revision,const std::vector<coopnet::WorldRule>& rules);
void update_host_world_rules();
void clear_host_world_rules();
bool capture_world_clock(coopnet::WorldClock& clock);
bool apply_host_world_clock(const coopnet::WorldClock& clock);
void exercise_world_settings_probe();
void begin_world_replication();
void end_world_replication();
struct NativePartyExit {
    std::uint16_t object=0xffff;
    std::uint32_t destination=0;
    unsigned present=0;
};
bool capture_party_exit(const std::vector<std::uint16_t>& actors,NativePartyExit& exit);
bool perform_party_transition(std::uint16_t exit);
bool prepare_party_probe(std::uint16_t& exit,float* origin);
bool position_party_probe(std::uint16_t actor,std::uint16_t exit,const float* origin,bool at_exit);
void display_party_status(unsigned stage,unsigned present,unsigned required,std::uint32_t destination);
struct NativeWorldPose {
    std::uint16_t object=0xffff;
    std::uint64_t incarnation=0;
    float position[3]{},rotation[3]{},health=0;
    std::string section,visual;
};
void queue_npc_catalogue(std::uint64_t session,std::uint32_t level,const std::vector<coopnet::NPCRecord>& records);
void update_npc_catalogue();
bool capture_containers(std::uint64_t session,std::uint32_t& level,std::vector<coopnet::ContainerRecord>& records);
void queue_container_catalogue(std::uint64_t session,std::uint32_t level,const std::vector<coopnet::ContainerRecord>& records);
void update_container_catalogue();
bool capture_shared_quests(std::uint64_t session,std::uint32_t& level,coopnet::QuestState& quests);
bool apply_shared_quests(std::uint64_t session,std::uint32_t level,const coopnet::QuestState& quests);
void exercise_shared_world_probe(double elapsed,unsigned& phase,double& wait,std::uint16_t& object);
bool capture_world_objects(std::uint32_t& level,std::vector<NativeWorldPose>& objects);
bool apply_world_object(std::uint64_t session,std::uint64_t anchor,std::uint64_t incarnation,
    const float* position,const float* rotation,float health);
bool world_level_is_replica();
void world_object_spawned(CGameObject* object,const CSE_Abstract* source);
void world_object_destroyed(CGameObject* object);
bool world_replica_object(const CGameObject* object);
bool update_world_replica(CObject* object);
bool schedule_world_replica(ISheduled* object,std::uint32_t elapsed);
void world_level_stopped();
}

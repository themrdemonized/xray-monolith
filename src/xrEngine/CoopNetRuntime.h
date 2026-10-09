#pragma once
namespace engine_coopnet {
// Plain-text bridge keeps the exception-enabled transport TU out of xrCore headers.
void report(const char* text);
void command(const char* name, const char* arguments);
void update(double elapsed);
void stop();
bool available();
bool guest_settings_locked();
bool world_setting_command(const char* command);
void register_world_setting_command(const char* command);
bool host_settings_application();
void applying_host_settings(bool value);
bool join_from_menu(const char* address);
void saved_join_address(char* output,unsigned capacity);
void join_status(char* output,unsigned capacity);
bool simulation_active();
bool shared_world_active();
bool party_level_change_allowed();
bool party_controls_enabled();
bool player_downed();
bool can_respawn();
bool request_respawn();
void respawn_status(char* output,unsigned capacity);
}

#pragma once
namespace engine_coopnet {
// Plain-text bridge keeps the exception-enabled transport TU out of xrCore headers.
void report(const char* text);
void command(const char* name, const char* arguments);
void update(double elapsed);
void stop();
bool simulation_active();
bool shared_world_active();
bool party_level_change_allowed();
bool party_controls_enabled();
}

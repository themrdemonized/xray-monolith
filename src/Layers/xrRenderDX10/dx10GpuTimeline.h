#pragma once

// GPU timestamps of every PIX_EVENT scope, drawn as a timeline in an ImGui window beside the device's frame stats
namespace gpu_timeline
{
constexpr u32 no_scope = u32(-1);

void frame_start();
void frame_end();
void frame_presented();
u32 scope_begin(LPCWSTR name);
void scope_end(u32 scope);

// Opens the window, on the overview, cpu or gpu tab when one is given
void open(LPCSTR tab);
void toggle_freeze();
void request_log();
void destroy();
}

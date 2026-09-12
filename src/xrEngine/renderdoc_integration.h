#pragma once

ENGINE_API void renderdoc_initialize();
ENGINE_API bool renderdoc_api_live();
ENGINE_API void renderdoc_poll_captures();
ENGINE_API void renderdoc_trigger_capture(u32 frames);
ENGINE_API void renderdoc_open_replay_ui();
ENGINE_API void renderdoc_set_overlay(bool visible);
ENGINE_API bool renderdoc_overlay_enabled();
ENGINE_API void renderdoc_set_active_window(void* device, void* window);

ENGINE_API void renderdoc_annotate_frame(const Fvector4* shader_params, u32 count);

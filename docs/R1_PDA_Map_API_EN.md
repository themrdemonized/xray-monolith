# PDA map API for Lua addons

Map inspection extends the existing Lua UI API. The engine owns no drawing layers, point buffers, renderer, tool dispatcher or annotation save format. Addons use CUIScriptWnd, CUIStatic:SetLineTo, textures, clipping and ordinary UI events.

- `pda_map.version()` returns `2`.
- `pda_map.window()` returns the current map viewport CUIWindow, or nil before it exists. Children draw over the map and beneath surrounding navigation/context controls.
- `pda_map.info("global_map" or level_name)` returns `{ready=false}` for unavailable/rotated maps. Otherwise: ready, global, shown, locked, x, y, width, height, bound_x1/y1/x2/y2, zoom, aspect. Rects are live virtual UI units; bounds are the native aspect-adjusted BoundRect.
- `CUIWindow:GetParent()` and `IsChild(window)` support attachment checks and cleanup of Lua-owned overlays.
- `on_pda_map_update` fires after map movement/navigation update. Refresh Lua widgets here; Lua is not called during rendering/parallel GC.

For map position px, py, calculate absolute UI coordinates:

```lua
sx = info.x + (px * info.aspect - info.bound_x1) * info.zoom
sy = info.y + (info.global and (py - info.bound_y1) or (info.bound_y2 - py)) * info.zoom
```

Inverse: px=(info.bound_x1+(sx-info.x)/info.zoom)/info.aspect; global py=info.bound_y1+(sy-info.y)/info.zoom; level py=info.bound_y2-(sy-info.y)/info.zoom. Global positions are artwork coordinates with UI aspect removed; level positions are world X/Z. Subtract the overlay's absolute origin before positioning its children. Re-read metadata while zoom/pan animates. Heading/line dimensions must account for the existing renderer's aspect handling.

Create Lua-owned widgets with SetAutoDelete(false) and AttachChildKeepOwner; retain Lua references while attached. Detach using the widget's current GetParent(), not a saved engine pointer. Engine windows are transient. Stop tools/capture on hide, teardown, death and transition. Save only versioned Lua coordinates/styles; rebuild widgets after load.

The previous draft's v1 command API is replaced by this v2 inspection API. A separate R1 Map Pencil Lua example demonstrates drawing/storage. Existing DLL addons need explicit adaptation. This PR auto-loads no pencil addon.

Build with upstream VS2022 and preserve its licence/credits. Runtime validation applies to the revision/scenarios listed in the PR description. [RU](R1_PDA_Map_API_RU.md).

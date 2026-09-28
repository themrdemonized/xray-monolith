# Scriptable UI previews and render surfaces

Proposed API v1 for the MT branch. All features are opt-in. This is a review candidate, not an accepted upstream release.

## Capabilities and defaults

`ui_preview.supports(name)` returns a boolean. Unknown names return false.

| Name | Supported path |
| --- | --- |
| background | DX11, feature level 11+, including the existing MSAA background path |
| isolated_model | DX11, MSAA off |
| ui_portal | DX11, MSAA off |
| scene_suppression | DX11, MSAA off |

No consumer means no active preview or scene suppression. Backgrounds are opaque. The preview shader assets ship with the engine; caller artwork does not. The MSAA depth target still gains shader-resource access on supported hardware, even when previews are inactive; do not claim zero allocation/format impact.

## Camera attachment backgrounds

Use a `script_attachment_type.CamAttached` attachment:

- `set_ui_background(argb, distance)` returns success. Distance is positive camera-space metres; zero disables. Invalid input disables the previous background and returns false. Alpha is currently forced opaque. Put the plane behind the model.
- `set_ui_background_texture(name)` returns success. DDS path relative to `$game_textures$`, without extension. Empty string clears. Missing/invalid paths return false and clear the texture; the colour remains the fallback.
- `set_ui_preview_dry(bool)` explicitly suppresses SSS-compatible HUD wetness on camera-preview draws. Defaults false, independent of background activation. This is a compatibility policy for the existing SSS constant, not a universal third-party shader override.
- `set_ui_studio_lighting(bool)` retains the older main-viewport studio path. It requires an active background. Lights marked `preview_light=true` are selected, and world lighting contributions are reduced. This affects the viewport: it is not per-object light linking. Use the isolated path for embedding in a visible world.

Opaque camera geometry is rendered first, the background is depth-tested against that geometry, and transparent camera materials are composited afterwards. Gameplay rendering retains the normal path when the feature is inactive.

## CUIPreviewContext

Construct with `CUIPreviewContext()` and keep it alive in the owning dialog.

| Method | Contract |
| --- | --- |
| `set_background_color(argb)` | Sets the opaque background colour, default dark neutral. |
| `set_background_texture(name)` | Same DDS validation/fallback contract as above. |
| `set_dry(bool)` | Explicit preview wetness policy, default false. |
| `set_lighting_gain(number)` | Returns false for non-finite or out-of-range values; accepted range 0 through 8, default 1. |
| `request_scene_suppression()` | Renew every visible fullscreen update. Returns false when unsupported or owned by another active preview. Applies stored configuration. |
| `is_active()` | Whether this context currently owns the renderer. |
| `is_scene_suppressed()` | Actual active suppression for this owner. |
| `release()` | Idempotent; cannot release another owner's preview. Also runs on context destruction. |

One active isolated context is supported. Requests expire after a missed renewal frame. The owner supplies an opaque fullscreen interface; the engine does not inspect texture alpha or decide whether hiding the world is visually appropriate. AI, physics, scripts, sound and A-Life continue. Never request this for a PDA, transparent overlay, or partial-screen window.

## CUIRenderPortal

A `CUIWindow`-derived Lua binding that displays a retained scripted dialog in its destination rectangle. Use inherited `SetWndRect`, `Show`, `SetAutoDelete` and attach it to the destination dialog.

- `SetSource(dialog_or_nil)` retains a strong Lua reference. The source must be Lua-owned with `SetAutoDelete(false)`. Detach with nil before manually deleting any native-owned source. Do not set the portal itself, an ancestor, or a second active copy as the source.
- `SetActive(bool)` returns success and requests embedded rendering rather than world suppression. Set source and visibility before activating. Only one active isolated preview is admitted, including fullscreen contexts.
- `SetBackgroundColor`, `SetBackgroundTexture`, `SetDry`, `SetLightingGain` configure its internal context.
- The source uses the engine's 1024x768 logical UI canvas. Destination rectangles may differ in size. Arbitrary source coordinate systems are not supported in this revision.
- Update renews only while portal/source are shown. Duplicate native updates in a frame are ignored. Input coordinates are mapped temporarily and then restored; keyboard forwarding requires the destination to route keyboard events to the portal.
- Close/hide must deactivate and detach the portal. Parent/source lifecycle remains the mod's responsibility.

Register PDA tabs through `pda_dynamic_tabs.register(caption_key, getter)` and unregister using the returned ID. The engine has no workbench-specific callback.

## Minimal integration

See `examples/ui_preview_example.script` and `examples/ui_preview_example.xml`. Copy to `gamedata/scripts/` and `gamedata/configs/ui/` respectively, then call `ui_preview_example.show('wpn_ak74')` from a developer script. No GAMMA or workbench data is required; the chosen section must have a valid base-game visual. Escape closes the example. These examples are opt-in and have no automatic callbacks.

For an embedded application, create a portal, set its source to your retained dialog, show both windows, and activate it. Attach that portal to the dialog returned by a PDA registry getter. Do not also call ShowDialog on the embedded source.

## Resources, restoration and limitations

Targets are lazily allocated at display resolution, reused while active, and released once no live owner remains. Geometry/device reset destroys the targets; ownership must be renewed afterwards. Inactive target contents are not reused as a fresh model frame. The implementation restores saved render targets and depth, and scopes cursor/scissor handling. MT/input interaction and device-reset behaviour still need in-game validation.

The stock/SSS-compatible shader path is tested by compilation, not every third-party shader pack. ReShade may continue to affect the final frame. Dryness is currently a scoped SSS-constant adjustment; a broader shader-bus policy is a possible future extension.

## Validation status

See [validation and limitations](ui-preview-validation.md) for exact revisions, build coverage and user-reported results. All DX8/DX9/DX10/DX11 and AVX build jobs passed for the tested implementation. Runtime testing was on the user's DX11 GAMMA installation, not every compiled renderer.

The contribution has been reapplied to upstream MT `ad31f200` without changing the tested engine feature code. Upstream's newer render-order and LOD fixes are retained; this combined revision needs its own build and focused visual check before ready-for-review status.

## Suggested contribution title

**Add scriptable UI preview rendering, embedded dialogs and optional scene suppression**

Suggested description: Scripted equipment and PDA interfaces need a way to display models with a stable background and embed existing UI without rendering an obscured world unnecessarily. This adds opt-in background rendering, an isolated model/UI path, mapped-input portals and owner-scoped fullscreen scene suppression. No workbench assets or gameplay rules are built into the engine. Initial isolated rendering requires DX11 without MSAA; background-only support also includes the existing MSAA path. See the capability table, example and test evidence for scope and limitations.

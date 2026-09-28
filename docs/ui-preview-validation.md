# UI preview validation and review limits

## Revisions

- Tested engine feature snapshot: `fdb34bd4149949923f8ee6771a0677dc43b7f641` in the author's fork.
- [Successful build](https://github.com/Fomok/xray-monolith-inventory-edits/actions/runs/36352797070): gamedata packaging and DX8, DX9, DX10, DX11, each ordinary and AVX, passed.
- Proposed base: upstream MT `ad31f200bbe1b6cf0c33f545d5792c124411be69`.
- The feature patch applies cleanly. Upstream's newer camera-UI render order and LOD clearing changes are retained. Runtime acceptance below is for the tested snapshot, not this newer combination.

## User-reported runtime checks

On a DX11 GAMMA installation with the workbench consumer (preview-api6): fullscreen and PDA presentation, manual/context opening, model controls, scope swaps preserving equipped slots, repeated opening/closing, lower resolution and restoration, and returning to gameplay passed. The context-menu fullscreen flash and stale-resolution portal image were corrected and retested.

With the consumer disabled, the user reported normal area transitions, stock workbench use, combat and looting. This is a compatibility sample, not a universal no-regression guarantee.

The standalone base-game AK74 example displayed against a grey background, and Escape restored gameplay. Native source-retention/replacement, hidden-source/portal activation, competing-owner rejection and idempotent release assertions passed through the optional local launcher. This does not exercise all native-parent destruction paths or prove transparent-only model handling.

## Performance observations

| Scene | Instantaneous screenshot FPS |
| --- | ---: |
| Gameplay before opening | 208 |
| Fullscreen Status, scene suppression off / on | 211 / 497 |
| Fullscreen Attachments idle, suppression off / on | 236 / 350 |
| Fullscreen Attachments rotating, suppression on | 230 |
| PDA Status / Attachments | 208 / 180 |

The user reports rapid fluctuations of about +/-30 FPS (roughly 460-520 around the 497 capture). These are not averages, percentiles, controlled engine-only benchmarks or promised speedups. Consumer Lua optimizations and presentation differ between tabs. PDA keeps the visible world; enabling the consumer's fullscreen-suppression setting does not mean the PDA suppresses the scene.

## Remaining review/coverage limits

- Fresh build and visual checks on the current upstream base.
- Standalone transparent surfaces without a background plane have code-path checks but limited native visual coverage.
- MSAA capability rejection, older-renderer fallback, forced device loss, native-parent source destruction and sustained memory measurements are not established by the above runtime checks.
- Render targets/depth and culling are restored. Stencil/shader state follows existing engine UI pass conventions; the portal is not an arbitrary GPU-state sandbox. Maintainer review of these call sites is requested.
- One active isolated owner and a fixed 1024x768 source canvas; no nested portals or simultaneous independent previews are promised.
- Dryness adjusts the SSS-compatible HUD wetness constant; it is not a universal shader policy.
- The supported MSAA base depth target gains shader-resource access even without a consumer. Features are opt-in, but zero resource impact is not claimed.
- A BusyHandsDebug report after resolution switching remains unresolved. Its stack reported `ui_hud_dotmarks` calling `UIStaticQuickHelp:Show(false)`. A Dot Marks interaction is suspected, not proven; do not attribute or dismiss it conclusively.

No workbench/ADS assets, gameplay rules, MCM integration or custom build workflow are required by the engine contribution.

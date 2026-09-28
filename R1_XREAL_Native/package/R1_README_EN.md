**0.1.4 lighting fix candidate.** The user quicksave_2 view now reconstructs lighting positions with the stereo projection shift. Standard 64 mm / 2 m retained. No FPS fix claimed. Two earlier diagnostic runs encountered shutdown 0xC0000374; its cause remains unresolved. Not a stable release. Close the game and replace the previous package; disable the old mod in MO2.

# R1 XREAL Native 0.1.4 CANDIDATE — experimental MT DX11

Geometry SBS stereo for the exact September MT DX11 executable. Separate DLL; the EXE file is not replaced. This is a world/iron-sight probe, not a complete VR adaptation.

## Install

1. Install this ZIP as a mod in MO2 and enable it. The archive root contains gamedata. Without MO2, copy gamedata into a separate test installation.
2. Launch MT September 12 AnomalyDX11.exe with SHA256 `C43701F5822950172C9D13A9CD4C92CBDA3E94060CAE794FBA81DEDFD5BA43D0`. Non-MT, AVX and other executable builds are refused.
3. Remove any manually installed R1_live/modxml_r1_stereo_test harness. Disable ReShade SuperDepth3D/other stereo converters and the older R1 XREAL Probe sharing F10.
4. Put the glasses in SBS mode and select 3840x1080 on that display. Actual backbuffer aspect must be between 3 and 4; ordinary 1920x1080 is refused with -24.
5. Disable HDR/MSAA and run `ssfx_taa (0,0,0,0)` in the game console. Restart if required to apply graphics settings. This addon does not rewrite them.
6. Load a save and press **F10**. Press F10 again or Escape to disable. Camera/settings changes, loading and transitions disable stereo; re-enable manually.

MCM is optional. The **R1 XREAL Native** page controls the toggle key, eye separation (default 64 mm) and convergence (2 m). Changing settings disables stereo; values apply on the next enable. Startup/load is default-off.

## Limits

Fix candidate: grass/light data replay between eyes; HUD, inventory and cursor composition on each eye. Main menu is duplicated in SBS; hooks install in the menu, geometry stays off until F10. Inventory was observed in an isolated save test. Loot, trade, other menus, grass and lighting still need full visual acceptance. The original loading crash is not declared fixed. The right eye uses the original camera, with a separate geometric left view. Impact calibration and the full weapon set are unverified.

No head tracking/Phoenix UDP, 3DSS/SecondViewport or optical-scope support. SSR/AO histories are not separated; artifacts may occur. Do not combine competing renderer hooks. A heartbeat pause beyond 1.5 seconds suspends stereo. Long sessions, all transitions and the full GAMMA stack are not certified.

## Troubleshooting and uninstall

See appdata/R1_XREAL_Native.log and `[R1_XREAL_NATIVE]` in the xray log. -10: wrong EXE/conflicting hook; -20: HDR/MSAA/SecondViewport; -25: TAA enabled; -24: narrow backbuffer; -21/-23: D3D resources/composition; -26: changed/unsupported renderer containers. Correct the mode and press F10 again. Restart after a DLL-load failure.

Disable the mod in MO2 and restart. For manual installation remove only files from this archive. No new game is required; the addon stores no custom save data. The DLL remains loaded until process exit even when stereo is off.

Developer source/docs: https://github.com/noname-r1nk1/R1_XREAL_xray-monolith/tree/R1_xreal_stereo_probe/R1_XREAL_Native . Published 0.1.4 CANDIDATE source may lag the local package. MinHook license: R1_MinHook_LICENSE.txt.

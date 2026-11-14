# R1 XREAL Native 0.1.1 — installable experimental probe

Supersedes the 0.1.0 README statements about missing player loader/MCM and missing TAA refusal. The original developer guides describe the initial prototype; unresolved UI, temporal and optical limitations still apply.

- Player package: prebuilt DLL, default-off Lua loader, optional MCM (toggle key, IPD, convergence), English/Russian CP1251 strings and installation READMEs.
- Scope deliberately restricted to exact September MT DX11 SHA256 c43701f5822950172c9d13a9cd4c92cbda3e94060cae794fba81dedfd5ba43d0. AVX is refused even though its historical profile remains in the header.
- PDB verified ps_ssfx_taa RVA 0x143fe78 (GUID/age matching, unmatched=0). Active TAA now refuses stereo with -25.
- Corrected incomplete GPU setup retry. Added scoped restoration of HOM/in_pair and isRendering. This does not establish universal race/exception safety.
- Player loader stops on Escape, inventory/PDA, alternate cameras, settings, load, death, destroy and level-changing callbacks. It does not press fire, manipulate actor state, or quit the game.
- Renderer errors stop the player loader. New enable clears stale errors so corrected settings can be retried. Unsupported-host and load errors remain disabled for that process.

Validation on 2026-09-29:

- MSVC x64 DLL build: passed.
- Camera reference test: passed.
- D3D11 WARP test including partial-resource recovery: passed.
- Lua mock: default-off, toggle, Escape, refusal/retry, settings/destroy/inventory and single installation: passed (not gameplay proof).
- Loading DLL in a Python process: returned -10, unsupported host refused.
- Isolated exact MT DX11 game with production scripts/DLL: 941 stereo pairs at checkpoint, error=0, Escape disabled, re-enable and stop passed. Process exited normally. Harness stored outside player archive; no scripted firing in this test.
- ZIP: 8 files, CRC, case-insensitive unique names and exact byte comparison passed. No automated harness or source files included. Manifest at evidence/R1_player_manifest.json.

Not proven by this run: manual MO2 deployment/virtualized DLL path, rendered MCM controls, full save/transition/death matrix, hardware viewing comfort, full HUD/menu support, optical sights, hit calibration, long-session behavior or GAMMA compatibility. No working game installation was modified.

Install and controls: package/R1_README_RU.md and package/R1_README_EN.md. Build with R1_build.cmd; package using Python 3 R1_make_player_package.py after building. Preserve package/R1_MinHook_LICENSE.txt. Native and Lua source in this directory correspond to 0.1.1.

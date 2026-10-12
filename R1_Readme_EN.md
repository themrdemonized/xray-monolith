# R1 PDA Map API

This Modded Exes source fork gives addon authors generic drawing layers, coordinate conversion and input callbacks for the PDA map. The Lua module is `pda_map`, API version 1. Tool UI, persistence, erasing and undo belong to addons.

[API and integration guide](docs/R1_PDA_Map_API_EN.md) · [Russian guide](docs/R1_PDA_Map_API_RU.md) · [Validation and limits](docs/R1_PDA_Map_Validation.md) · [Opt-in example](examples/R1_Pda_Map_Example.lua)

Branches: `R1-pda-map-api` (ordinary engine) and `R1-pda-map-api-mt` (experimental MT port). Both x64 DX11 engines were fully linked. The ordinary build passed native 2D PDA drawing, independent layers, input ownership and Lua save/cold-load restoration on Anomaly 1.5.3 + Modded Exes 2026.10.11. MT acceptance remains incomplete due native shutdown/load failures. See the validation document for exact evidence and limits. The existing R1 Map Pencil DLL requires a separate addon adaptation.

Build using the upstream VS2022 solution. Preserve `License.txt`, existing credits and upstream documentation. New code follows the repository's existing license; no third-party addon binary or art is redistributed by this change.

# R1 PDA Map API

This Modded Exes source fork gives addon authors generic drawing layers, coordinate conversion and input callbacks for the PDA map. The Lua module is `pda_map`, API version 1. Tool UI, persistence, erasing and undo belong to addons.

[API and integration guide](docs/R1_PDA_Map_API_EN.md) · [Russian guide](docs/R1_PDA_Map_API_RU.md) · [Validation and limits](docs/R1_PDA_Map_Validation.md) · [Opt-in example](examples/R1_Pda_Map_Example.lua)

Branches: `R1-pda-map-api` (ordinary engine) and `R1-pda-map-api-mt` (MT). See the validation document for exact source baselines and checks. This is a source/API development fork. No game-tested executable release is supplied. The existing R1 Map Pencil DLL requires a separate addon adaptation.

Build using the upstream VS2022 solution. Preserve `License.txt`, existing credits and upstream documentation. New code follows the repository's existing license; no third-party addon binary or art is redistributed by this change.

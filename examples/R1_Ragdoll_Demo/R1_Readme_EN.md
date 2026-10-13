# R1 Ragdoll Demo 0.1.0 EXPERIMENTAL

A lightweight addon demonstrating temporary living stalker/player ragdolls and recovery through the R1 Ragdoll API. It does not add knockout probabilities, injury, treatment or looting rules. No custom model, texture, sound or DLL is included.

Requires Anomaly 1.5.3 and the separate custom EXE from branch `R1_ragdoll_api`. Stock Modded Exes and the old Dynamic Reactions DLL do not expose this API. MCM is optional; defaults are 8 seconds before recovery and an initial push speed of 2 m/s.

- F6: aim at a living stalker NPC and start its ragdoll.
- F7: start the player's ragdoll; this example switches to third person.
- F8: request recovery of this example's own ragdolls.
- MCM "R1 Ragdoll Demo": enable, duration 1–30 seconds, push speed 0–8 m/s. Options refresh within one second; duration is captured when starting a request.

The EXE blocks item pickup and normal controls during the ragdoll. Health is not replaced with invulnerability. Death keeps the existing body as a normal corpse. If recovery space is blocked, this example retries every 0.5 seconds. Every request has an exclusive token; another addon's request is not cancelled.

Install the agreed custom EXE and this addon in a separate test installation/MO2. The player's ZIP has `gamedata` at its install root. The interface itself does not require a new game; save acceptance must be checked against the exact supplied build. To remove: press F8, wait for recovery, save and close the game, then disable the addon. Follow the EXE's separate removal instructions.

Potential overlap: addons using F6/F7/F8, camera/inventory control or another ragdoll system. The old R1nk1 Ragdoll Bridge does not target this custom EXE. Mutants, mounted characters, multiplayer, MT and other renderers are not claimed supported.

Validation on 2026-10-13: 19 offline LuaJIT checks and 45 native game assertions in isolated Anomaly 1.5.3 using the custom DX11 EXE. The scenario covered living NPC/player falls and recovery, pickup refusal and restoration, demo timers and camera restoration, real NPC death, and a fresh-process load of a save made during the player's ragdoll. EXE SHA-256: `3f62d5edd665a21cfec2571139eafdfb56c06c87ef6171a786e79e68db4b4c44`. Manual F6/F7/F8 input, rendered MCM, visual recovery quality and large modpacks were not separately checked. This is an experimental package.

New example author: R1nk1/R1. Anomaly/Modded Exes provide the physics, models and Lua infrastructure. User-supplied legacy reference footage: [falls](https://media.moddb.com/images/members/5/4837/4836974/profile/file.gif), [NPC](https://media.moddb.com/images/members/5/4837/4836974/profile/1.gif), [interaction](https://media.moddb.com/images/members/5/4837/4836974/profile/2.1.gif). These show the previous system, not acceptance of the new EXE.

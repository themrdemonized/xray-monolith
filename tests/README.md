# Early destination loading artwork

From an x64 MSVC developer prompt, run `python tests\loading_logo.py`.
`--source-only` checks caller order without running the compiler.
The standalone regression compiles the production logo selector, LoadBegin,
LoadEnd, Level_Set and renderer setter with an instrumented filesystem/renderer
and production CRandom. It covers 128 RNG seeds, first-frame binding, nested
loads, cleanup, missing/global/local artwork, and no early active-zone mutation.

Zone transitions preview destination artwork once the graph vertex is known,
before ClientSave and reconnect. Only global `$game_textures$` images can be
selected early; level-local images retain the existing activation fallback.
The preview copies CRandom; Level_Set performs its original draw at its original
lifecycle point. Menu save loading is unchanged. This change does not call
Level_Set early, alter `$level$`, or add a save read.

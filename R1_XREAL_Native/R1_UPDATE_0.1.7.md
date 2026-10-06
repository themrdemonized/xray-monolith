# R1 XREAL Native 0.1.7 — loading-tip layout

RU: исправлен перенос подсказки на загрузочном экране SBS. Размер букв сохранён; строки рассчитываются с учётом их итоговой ширины. Это точечная правка для сентябрьской MT DX11, не исправление зависаний загрузки или всего интерфейса.

EN: correct loading-tip wrapping for September MT DX11 SBS. Glyph size is retained; the wrap calculation now accounts for the final glyph width. This does not fix loading stalls or certify the entire UI.

## Cause and implementation

`src/Layers/xrRender/dxApplicationRender.cpp`: `load_draw_internal` passes `600 * k.x * (b_ws ? 0.8 : 1)` to `draw_multiline_text`. Its word parser measures unscaled font glyphs. The existing SBS font hook doubles glyph X while rendering, so previously wrapped lines could become twice as wide as the card.

The DLL intercepts `draw_multiline_text` at RVA `0xb23700`, checking all 24 entry bytes before hook installation. Windows x64 signature: `void(void* font, float width, const char* text)`; width is the second argument in XMM1. Matching September PDB and executable SHA256 `C43701F5822950172C9D13A9CD4C92CBDA3E94060CAE794FBA81DEDFD5BA43D0` are required. Do not copy this RVA to a different executable.

On the owning render thread, while loading with menu SBS enabled and an actual buffer aspect ratio between 3 and 4, pass half the original wrap width to the original function. Ordinary widths, inactive SBS and unknown viewport height remain unchanged. The existing font hook still doubles glyph X. Hook creation, enable rollback and shutdown include this entry.

For an engine integration, correct the loading card's text layout width by the actual glyph scale before wrapping, or make the text measurement and rasterization use the same scale. Do not globally shrink every font or change the monitor resolution to compensate.

The loading-capture diagnostic now waits until a nonempty tip has been queued, avoiding a capture of an earlier blank loading frame.

## Evidence and limits

- MSVC x64 DLL build passed. `R1_test_loading_text.cmd` passed: wrapped doubled glyphs fit the card; inactive, ordinary-aspect and zero-height cases retain their width.
- Existing Lua loader mock passed after version metadata update; this is mock coverage, not gameplay proof.
- Actual loading capture at 3824x1041 inspected: English tip wraps into two lines within each eye card and does not cross the eye boundary. Local evidence: `evidence/R1_017_loading.png`. This does not certify all translations or unusually long unbroken words.
- Two candidate fixture launches reached the actor callback once but did not proceed to the stereo gameplay test; both were stopped. No fresh successful gameplay run is claimed for this version. Previous loading stalls and heap-corruption reports remain unresolved.
- Control with the issued 0.1.6 DLL in the same fixture also stopped progressing after its first actor callback and was terminated. This does not isolate the cause; it shows the new wrapping hook is not required to reproduce this fixture symptom. Another unrelated test-game process was present during the comparison and was left untouched.
- No performance measurement was made for this change. Version 0.1.6 benchmark numbers describe that earlier build.

Install the 0.1.7 ZIP as a replacement for the previous R1 XREAL Native package in MO2; keep only one version enabled. Roll back by disabling 0.1.7 and restoring the previous archive. Game EXE, saves and the active player profile were not modified by this test.

# R1 XREAL 0.1.5: partial UI correction / частичная правка UI

This package deliberately retains the exact 0.1.4 DLL (SHA256 121BB9D425A32B295ED5EB706CFB4B1FAF040956D9809717047B4A7F41323CFB). Native font-hook experiments did not complete their fixture scenario and were stopped, reverted and excluded. No claim that those failures were solved.

## Implemented

- The Lua key callback no longer treats every Escape as a request to disable stereo. F10 remains the explicit off switch. Other lifecycle guards remain.
- The production menu preparation wraps `utils_xml.screen_ratio` once, after successful exact-host DLL loading and only on an SBS-width display. It doubles the combined-screen ratio to obtain the one-eye ratio. Normal displays retain the original result. `utils_ui` caches this ratio at module load; preparation loads it after installing the wrapper.
- The scripted `utils_ui.UIInfoItem.InitControls` wrapper halves the description's logical width so that wrapping matches the existing two-times font raster width. It retains the parent's frame and uses the existing AdjustHeightToText path. It does not modify native CUIItemInfo or all other text controls.

Sources inspected: the target game's unpacked scripts.db0 (`utils_xml.screen_ratio`, `utils_ui` ratio cache / UIInfoItem / item icon sizing, `ui_mm_faction_select`), plus engine CUILines and dxFontRender. Source assets are private diagnostic references, not distributed.

## Evidence

The updated regression initially failed against the old Escape callback, then passed after the change. Mock checks also cover single wrapping, one-eye ratio, and normal-display fallback; these are not game proof.

Isolated exact September MT fixture: new-game item icons and bread tooltip captured at 3824x1041 SBS. The description remains within its frame in the inspected image, and the menu test quit with exit 0. An isolated save/inventory/console scenario using the retained 0.1.4 DLL checked Escape-keeps-enabled followed by F10-off and exited 0 (187 pairs at checkpoint). Final combined loader validation is recorded separately below.

## Remaining failures

3D PDA view alignment/clipping is NOT fixed. Other native tooltip types and all text/menus are not certified. The prior shutdown heap failure and FPS reports remain unresolved. No runtime-native font experiment is in this ZIP. No game saves or working installation were changed.

RU: исправлены отключение по Esc, пропорции Lua-иконок меню новой игры и перенос описания предмета в этом меню. Это частичное исправление. 3D-КПК и все остальные варианты нативных подсказок ещё не исправлены. DLL сохранена от 0.1.4; неудачные нативные эксперименты в архив не входят.

Final combined 0.1.5 Lua / retained 0.1.4 DLL fixture run: 441 pairs, native error 0, ESCAPE_KEPT_STEREO_F10_OFF, inventory/console scenario completed, process exit 0. ZIP verified: 9 files, CRC and exact bytes. New requested concussion camera mismatch, selectable dominant eye and optical ADS black-eye mask are not implemented in this package.

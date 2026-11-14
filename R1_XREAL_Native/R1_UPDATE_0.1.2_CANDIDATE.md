# R1 XREAL 0.1.2 candidate / кандидат

**WIP / DO NOT MERGE. Not a verified player release. / Не проверенный игровой релиз.**

0.1.1 user testing exposed missing loot UI, grass and unequal lighting between eyes. Original loading crash remains unresolved. Earlier pair counters were insufficient visual validation.

Candidate changes: preserve consumed detail-list endpoints between eye passes (refuse changed allocations), restore light per-frame markers before the second eye without advancing Device.dwFrame, render CHUDManager UI on each eye texture before SBS composition, duplicate main menu and correct font width. Exact September DX11 SHA guard remains; additional hook/call signatures are checked.

Исправления в коде кандидата: восстановление списков травы и кадровых меток света между глазами; отдельная отрисовка HUD/окон поверх каждого глаза; одинаковое главное меню для обоих глаз и коррекция ширины шрифтов. Номер кадра симуляции не меняется. Меню устанавливает hooks при открытии; геометрическое стерео по-прежнему выключено до F10.

Local checks: native compilation, camera math, WARP SBS and new container/state-restoration unit test passed. In-game validation is pending; do not infer safe gameplay, container interaction, temporal-effect correctness or crash resolution from those checks. MSAA/HDR/TAA/SecondViewport remain unsupported. Head tracking is not implemented.

Source references: September e189f080525335d4b9fbb282f8467b4968b13372; Light_DB.cpp::add_light, dx10DetailManager_VS.cpp::hw_Render_dump, DetailManager.cpp::details_clear, HUDManager.cpp::RenderUI. Exact EXE/PDB offsets are not portable to other builds. Pair-state header uses engine-owned container layouts without freeing/reallocating them.

Required acceptance: outdoor grass and indoor lights in both eyes, loot/container/trade/inventory/PDA, menu and return, new game/load/transition, eye-aligned ADS, normal/off comparison. Keep PR #701 draft.

Fresh candidate validation (2026-09-29): DLL SHA256 4DC6BB8066A52E5DCE06DE64EA4C4943A1882BF7C7F339F885FDEB668D7F85B5. Isolated copy of player autosave loaded; 260 pairs at checkpoint, error 0; Escape disable, re-enable, inventory opening and normal exit passed. Native captures show inventory, cursor and main-menu text in both eyes. Windowed backbuffer 3824x1041; TAA/MSAA off; g_always_active on only in fixture. Loot transfer/click hit-testing, outdoor grass, comparative indoor lighting, new-game crash and physical-glasses comfort remain unverified. Final cursor hook replays its own frame stamp; it does not advance Device.dwFrame.

Проверка кандидата: отдельная копия автосохранения, 260 пар в контрольной точке без native-ошибки, Esc/повторное включение/инвентарь/штатный выход. На захватах видны инвентарь, курсор и главное меню для обоих глаз. Перенос лута, клики, трава, сравнительный свет и первоначальный краш не прошли полную приёмку. Это не заявление о завершении исправления.

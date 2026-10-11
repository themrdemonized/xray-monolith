# R1 XREAL: lighting/loading follow-up, 2026-09-29

NOT A PLAYER RELEASE. Do not merge. Installed 0.1.2 remains unchanged by this follow-up.

User evidence: Yantar bunker captures show a substantially brighter right eye and displaced light/shadow silhouettes. Severe FPS loss is reported, not yet measured in that bunker. Grass is visible in both eyes in the new outdoor capture.

Concrete missed state: September `CRenderTarget::phase_accumulator` clears its additive light target only when `dwAccumulatorClearMark != Device.dwFrame`. The eye pair shared that marker, so right-eye light accumulated over left-eye contents. Candidate invalidates the marker before EACH eye render. Exact PDB: CRender::Target +1136, CRenderTarget::dwAccumulatorClearMark +16. Global Device.dwFrame is unchanged. Volumetric activity already resets in phase_scene_prepare; no speculative extra patch applied.

Loading: CApplication::LoadDraw bypasses main-menu activity but calls Device.End. Candidate includes `g_appLoaded == FALSE` in identical-eye composition and font correction. Exact September PDB g_appLoaded RVA 0x15b463c. A requested test capture occurred before the loading card appeared: it is a main-menu capture, NOT visual loading acceptance.

Performance instrumentation logs CPU elapsed submission/wait duration for complete eye pairs in batches of 120, max and mean, registered light count and full target size. It is NOT GPU timing or a bunker FPS benchmark. Initial outdoor Yantar batches: 7.653 / 9.565 ms mean, 18.319 / 22.295 ms max, 1712 registered lights (not visible lights), 3824x1041 per-eye temporary target. Both full-width eye renders remain expensive; no performance fix is claimed.

Runtime: isolated copy of latest Yantar autosave; checkpoint 306 pairs, native error 0; harness reached Escape/re-enable/inventory/quit. Process EXIT FAILED with 0xC0000374 (heap corruption) during shutdown. This overrides the harness PASS marker. Do not install or package this candidate as a fix. Baseline A/B investigation is required. No dump/stack proving the corruption origin is available yet.

RU: исправлена в коде причина накопления света второго глаза поверх первого; добавлен путь загрузочного экрана и замеры CPU-подачи рендера. В тесте обнаружено повреждение кучи при завершении процесса. Новая DLL пользователю не установлена, исправленной не объявляется. Нужны контроль предыдущей DLL на том же сохранении, точный сценарий бункера, проверка загрузочного экрана и измерение GPU/теней. Статус PR остаётся Draft / DO NOT MERGE.

## Latest validation / последняя проверка

Final DLL SHA256: BCE5F894E81DFF1BB21B9EF8549621295C696A1408D780BA9C249D33D7E9DC70. Console now has a separate guarded CConsole::OnRender hook; its image is composed on both eyes. Loading detection additionally requires CApplication::ll_dwReference > 0 (PDB pApp RVA 0x15b5a58, field +6176); g_appLoaded alone is false even in the initial main menu. A loading-only capture export supports this verification.

Fresh user bunker quicksave copied into fixture. Captures confirm matched lighting without the previously visible doubled light silhouette in the inspected view, console in both eyes, and the actual loading card duplicated. Baseline 0.1.2 bunker run: 188 pairs at approximately four-second checkpoint, exit 0. Candidate runs: 183 and 189 pairs at the same checkpoint, exit 0; final loading-scoped build also exited 0. These short runs show NO meaningful FPS improvement. They are not GPU profiling or a controlled hardware benchmark. First failed shutdown remains an unresolved single observation, not erased by later passing exits.

Сохранение пользователя в бункере проверено на отдельной копии. Свет и консоль визуально исправлены в проверенном ракурсе; захвачен настоящий загрузочный экран для обоих глаз. Ускорения нет: 188 пар у 0.1.2 против 183/189 у кандидата на коротком одинаковом отрезке. Один прежний выход с повреждением кучи остаётся необъяснённым; последующие выходы штатные. Кандидат не установлен поверх рабочей игры. Полная приёмка и оптимизация ещё не завершены.

Glass/HOM follow-up: the user's fresh engine log reports SSS GLASS SHADER INSTALLED 0 (SSR and SSS volumetric also 0). This does not prove ordinary glass is cheap, but an active SSS glass/refraction shader must not be blamed here. The old probe disabled HOM entirely. Candidate instead invalidates CHOM::MT_frame_rendered (+188) per eye and lets the original MT_RENDER rebuild its occlusion raster after workers are joined. Source vis_data delay caching returns TRUE while deferred (conservative visibility). Bunker checkpoint with this change: 188 pairs, comparable to 188 baseline / 189 accumulator-only candidate. No measured improvement in this short view; glass-specific cost is still unproven.

Стекло: по фактическому логу расширенный SSS glass не установлен. Обычная прозрачность не исключена как расход, но её вклад не измерен. Восстановлен пересчёт HOM для каждого глаза вместо отключения отсечения за стенами. В этом ракурсе число пар осталось примерно прежним — ускорение не заявляется.

Latest HOM candidate DLL SHA256: 9DA180DE17866249F646A76C5D51447C85E87CA001910BEB251C1FB48E2593DA; exact run exit 0, 188 pairs checkpoint. Earlier BCE5... hash belongs to the accumulator/loading/console candidate before HOM restoration.

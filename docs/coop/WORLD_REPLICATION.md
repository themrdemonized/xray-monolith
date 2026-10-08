# Canonical world integration

Protocol 8 implements a canonical starting-world transfer. `coop_world_probe` enables the current native integration. The host synchronizes current object state through the native save preparation path and creates a uniquely named session snapshot without changing the selected save name. The guest may start from the main menu, receives the host snapshot, verifies SHA-256 and the native save header, stores it under a locally derived session name, and loads it through the engine's normal lifecycle. No filename or native object ID is supplied by a transfer message.

The transfer is capped at 64 MiB, uses ordered 8 KiB chunks, limits bulk output to roughly 1 MiB/s per connection, reserves reliable queue capacity for control messages and times out after 120 seconds. The host will not assign the map before a matching validated-load acknowledgement. The client acknowledges only after a native actor exists on the expected map and the native server's loading options identify the received snapshot. Socket backpressure pauses bulk production instead of overflowing the byte limit.

The snapshots currently remain in the session's appdata savedgames directory after shutdown; their cleanup and handling of companion metadata are still required. Original save files are neither replaced nor merged.

## Verified checkpoint

All seven standalone suites passed, including strict metadata/chunk decoding, ordered assembly, a stalled transport, a baseline larger than the reliable queue, validation failure, the loading barrier and forged acknowledgements. The enabled engine build passed. The 90-second native `-WorldProbe` test started the guest without loading an independent game, transferred and loaded a fresh host snapshot, and compared the stored file hashes. Movement, native transient loot take/drop and exact replay, host damage correction, rendering, shutdown and original-save hash checks passed. Logs: `_build/coopnet-world-baseline-unit.log`, `_build/coopnet-world-baseline-build.log`, `_build/coopnet-world-baseline-engine.log`.

This establishes the canonical starting world, **not continuous shared-world gameplay**. The client still advances NPC AI, scripts and physics independently after loading. Normal weapons, durable inventory ownership, character import/export, quests, death/rejoin and independent active maps remain unfinished.

## Next integration

Use explicit client replica ownership at native spawn. Gate virtual frame and scheduled update dispatch before derived NPC code can run, keep original offline objects unaffected, and separate replica presentation from simulation. Suspend client ALife and shared world script scheduling. Use typed host NPC/object state with generation-aware logical bindings and consistent create/remove ordering; the asymmetric legacy stalker export/import format is unsuitable for direct forwarding. Route ordinary gameplay actions to the host only after object identity and persistent ownership are established.

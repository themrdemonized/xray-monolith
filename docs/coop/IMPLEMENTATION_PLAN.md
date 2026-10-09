# CoopNet implementation sequence

Current user-selected scope (2026-10-08): one host simulates one active location; all connected players gather at the same native exit and travel together. Independent active locations and location workers are deferred. PARTY_TRAVEL.md describes the current integration; the later independent-residency milestones below remain historical planning rather than current delivery requirements.

This plan covers the recovered requirements and the audited source. It is a newly written engineering plan, not the missing tail of the original master document. Recover the original remaining sections before treating it as the full historical implementation plan.

## Phase 0 — completed setup and static reconnaissance

- Established compile/deploy/startup baseline and scripts.
- Created `coopnet` branch, `upstream` remote, and `coopnet-build-baseline` local tag.
- Preserved recovered source design with a truncation notice.
- Audited level/actor identity and lifecycle, networking, ALife, persistence, inventory, Lua, transitions, physics, scheduler/threading.
- Evaluated all three multi-level candidates; no unproven architecture is presented as implemented.

Manual baseline test still outstanding: new/load game, movement, combat, inventory, save/load, and level transition. Use a disposable save. The existing startup smoke test did not cover these.

## Milestone 1 — isolated session/protocol harness

Build a small C++ module without gameplay/socket dependencies. Explicit Offline/Host/Client mode, versioned ClientHello/ServerHello, session/player/character identities, connection lifecycle, configurable fixed ticks, bounded framing/decoder, transport interface, and an in-memory transport for deterministic testing. Define channel/delivery contracts; do not add a pretend network implementation that reports connection success without a remote peer.

Checks: accepted/mismatched handshakes; malformed/truncated/oversized payloads; full 2–4 participant roster; disconnect/reconnect; bounded queues; stale sequence rejection; tick behavior across low/high frame rates; exact Offline behavior. Stable character identity must not be a transport handle or engine object ID.

Then evaluate a pinned GameNetworkingSockets version using its official build/API documentation. Keep its adapter outside gameplay. Verify two real processes connect and exchange reliable control and sequenced unreliable messages over loopback, then LAN. This milestone is a transport/session test, not playable co-op.

## Milestone 2 — opt-in engine integration

Add explicit session lifetime above CLevel and an engine adapter that queues transport messages for a safe engine update point. Add host/join/disconnect/status commands with honest state reporting. No automatic host/join during ordinary offline startup. Avoid changing legacy single-player packet interpretation.

Checks: full engine build; offline manual regression; host/join lifecycle from two client roots with separate appdata; clean shutdown; mismatch rejection; repeated transitions do not destroy durable session identity. Do not run two processes against the same save/appdata directory.

## Milestone 3 — same-level actor presence and movement

Before remote spawning, audit all `g_actor`/`Actor()` writes, control/view selection, input, HUD, inventory callbacks, and Lua local actor assumptions. Bind persistent PlayerState to actor identity without letting remote actors own camera/UI/input. Map spawn/destroy to engine lifecycle using generation-aware IDs.

Replicate transform/velocity/stance/basic movement, with level filtering and buffered interpolation (~100 ms configurable). Resolve late join from a consistent baseline followed by ordered events. Host simulation owns world results; presentation never becomes authoritative merely because it is on a client.

Checks: two actors on one map; camera/input isolation; destroy/rejoin with no stale object bindings; late join; loss/reordering; different frame rates; bounded bandwidth; long-distance same-map relevance and ALife activation. No quests or prediction in this milestone.

## Milestone 4 — shared location and party travel

The selected architecture uses one host simulation and one active location. Everyone must enter the same exit before the host transitions the party. Protocol 10 implemented and verified the gathering/reset barrier, canonical destination load, actor rebinding and arrival acknowledgements. Protocols 11–13 preserve guest equipment across that transition. See PARTY_TRAVEL.md and PROGRESS.md for the checks and limits.

Independent location residency and location-worker hosting are outside the selected scope. MULTI_LEVEL_ASSESSMENT.md is historical architecture research, not an implementation requirement.

## Subsequent milestones

1. Host AI/world lifecycle and combat outcomes; investigate bullet/projectile event semantics separately.
2. Reliable inventory/loot/equipment transactions and canonical item persistence; test simultaneous pickups and reconnect replay.
3. Versioned portable character import/export with original-save preservation, complete item subtree reconstruction, and mod/section compatibility handling. No complete world merges.
4. Coordinated world/time/weather/emission persistence and late join.
5. Explicit personal/shared quest and Lua side-effect ownership; portable quest state only after policy is agreed.
6. Movement prediction/reconciliation, bandwidth tuning, wider addon compatibility, and repeated party-travel recovery stress tests.

Anti-cheat remains out of scope. Input bounds, decoder safety, transaction correctness, and ownership invariants are required for stability regardless of anti-cheat goals.

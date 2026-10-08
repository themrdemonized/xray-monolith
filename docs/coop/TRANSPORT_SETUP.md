# CoopNet transport development

These commands exercise admission, actor presentation data and validated guest movement-command transport. They do not load a shared world, simulate guest actors, replicate gameplay, or make co-op playable.

From the repository in PowerShell:

```powershell
.\setup-coopnet-deps.ps1
.\test-coopnet.cmd
.\test-coopnet-network.ps1
.\build.ps1 -CoopNet -Deploy
```

Close running game processes before deployment. The first dependency build includes OpenSSL and protobuf and can take substantial time. Ordinary builds without `-CoopNet` do not link the networking library. Deployment backs up replaced runtime files.

For engine testing, use two client roots with separate `appdata`/save directories. In the host console:

```text
coop_host 27888 1 1 1
coop_status
```

In the guest console:

```text
coop_join 127.0.0.1:27888 2 1 1
coop_status
coop_disconnect
```

The last three arguments are decimal character identity, game build fingerprint, and mod fingerprint. Values `1 1` are explicit test fixture fingerprints, not automatically verified compatibility. Host and guest fingerprints must match; character identities must differ. Portable character storage and automatic build/mod fingerprint generation remain pending. Production reconnect tokens and session IDs use the Windows system random generator; the standalone smoke test uses a fixed token confined to the test.

`coop_status` reports the transport session, not gameplay readiness. Sessions are not automatically created on offline startup. They survive ordinary level teardown and are destroyed on explicit `coop_disconnect` or application shutdown. Level migration and gameplay persistence are not implemented by that lifetime alone.

Verified: real-process loopback session/roster/snapshot/disconnect test, ordinary and enabled engine compilation, and two actual engine instances with isolated appdata completing admission/roster delivery and normal shutdown. Verification pending: gameplay regression, session survival through transitions, LAN tests, and gameplay replication. Consult PROGRESS.md for completed checks.

Use `prepare-coopnet-engine-test.ps1 -Launch` after an enabled build to create two isolated runtime roots under `_build/coopnet-engine-test`. Their game assets come from the existing client; their appdata and executable directories differ. Logs may remain buffered until each engine closes normally. This launcher proves no gameplay automatically; inspect the logs for acknowledged host participants and the guest roster.

For the isolated presentation test, use `prepare-coopnet-engine-test.ps1 -LoadFixture -ReplicaProbe -Launch`. It copies `player - autosave` into both test appdata folders and enables the explicit development `coop_replica_probe` command. The probe assigns the host actor's level after admission and acknowledges it only when the copied guest level is ready, allowing host actor snapshots to reach the presentation adapter. The two test worlds run independently; this test does not establish canonical world authority, guest simulation, inventory synchronization, or playable co-op. Close both test clients after testing and inspect their flushed logs. Original save files are preserved.

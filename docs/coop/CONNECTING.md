# Connecting players

This is a direct connection to a player's host process. There is no hosted lobby, account service, automatic discovery or relay. The host simulates the active location; connected players travel together.

Every player needs Anomaly 1.5.3, the same co-op engine build and networking DLLs, and matching mods. The Git repository supplies engine source and build instructions, not a complete downloadable game package. Protocol 22 clients cannot connect to older protocol builds. Keep each installation's appdata and saves separate.

Enter `/help` (or `coop_help`) in the console for the host/join examples, same-PC address, status, disconnect and respawn commands. The existing `help` command still lists general engine commands.

The host loads a game, opens the console, and enters:

```text
coop_host 27888 1 1 1
```

A guest clicks **Join CoopNet**, enters the host IPv4 address (optional `:port`, default 27888), and chooses **Load save** or **Create character**. These open Anomaly's normal save picker or faction/character creator. After the selected character loads, joining starts automatically. Returning from the picker to the main menu cancels the pending join. For testing on one PC, enter `127.0.0.1:27888` in the second installation.

**Connect** uses a character already loaded in single-player, or resumes the remembered CoopNet character when no game is loaded. A first-time guest without a loaded character is asked to load or create one. The Join option remains available in the pause menu before connecting. A selected character imports its carried items, equipped and belt placement, active weapon, condition, ammunition, attachments/upgrades, consumable uses and rubles into the host's world. The source save's world and location are not transferred. Existing host campaign records resume that character's latest CoopNet equipment rather than granting the original loadout again. See GUEST_STATE.md for limits.

Successful connections remember the character and resume token. Up to sixteen profiles are encrypted for the current Windows account with DPAPI in `appdata/coopnet-connections.dat`. Character IDs are retained per source save alias in separate `appdata/coopnet-character-id-*.dat` files. No Steam password is requested. The most recent address is filled automatically; the menu uses placeholder game/mod fingerprints `1 1`.

The console remains available for explicit identities and fingerprints:

```text
coop_join HOST-IP:27888 2 1 1
```

Replace HOST-IP with the host's reachable IPv4 address. On the same home network, use the host's local address. For a direct internet connection, use its public address and configure the host's router to forward UDP 27888 to that computer. Windows Firewall must allow the host executable's inbound traffic. No router forwarding is needed for a connection entirely within the same LAN. The host's network must support inbound connections for the direct internet approach.

The arguments are port/address, character ID, game fingerprint and mod fingerprint. The example uses matching placeholder fingerprints `1 1`; those values are supplied assertions, not automatic installation checks. Each player uses a different nonzero character ID. The four-player limit includes the host; additional guests can use IDs 3 and 4.

Use `coop_status` to inspect the session and `coop_disconnect` to close it. A disconnected guest should reload a normal save or return to the menu before ordinary single-player use; its loaded world is a host snapshot with passive replica objects. A new guest process can resume against the same running host using saved credentials. An expired host session retries fresh admission using the saved character. Connection loss within a running client uses its existing session token to retry.

The native tests verify loopback transport and selected gameplay paths. Public internet play, full ordinary inventory controls, dynamic NPC lifecycles and the complete co-op gameplay loop are not yet verified. See PROGRESS.md and GUEST_STATE.md.

The host supplies shared simulation, gameplay/economy, population, weather and difficulty settings. Guests see those options disabled and gray; console and scripted world-setting writes are blocked too. Graphics, sound, controls, player name and personal display warnings remain editable. Host values override the session without overwriting the guest option file. World time and weather update from the host.

Steam friends and lobby joining are not implemented. They require Steamworks integration and an appropriate Steam App ID; the standalone networking library does not provide Steam friends or Valve relay access. See the [Steamworks friends API](https://partner.steamgames.com/doc/api/isteamfriends) and [API setup](https://partner.steamgames.com/doc/sdk/api).

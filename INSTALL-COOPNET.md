# Installing and playing Anomaly CoopNet

> **EARLY TEST / FREE PLAY BUILD — DOES NOT contain the full planned features yet. Expect bugs, crashes, and incomplete systems.** Think of this as a way to explore and experience the Zone with friends. Story progression and quest systems are still being developed; a complete co-op story experience is not available yet.

CoopNet is an experimental engine modification for **S.T.A.L.K.E.R. Anomaly 1.5.3**. This repository contains engine source and supporting files, not the full game. Downloading GitHub's source ZIP alone does not install a playable client. Most players should use the compiled package below; the source-build instructions are optional.

## Install the compiled test build (no compiler required)

1. Install the full [Anomaly 1.5.3 base game](https://www.moddb.com/mods/stalker-anomaly/downloads/stalker-anomaly-153). [Alternate download/torrent page](https://anomalymod.com/download-install/).
2. Download **FOBs-Anomaly-CoopNet-EarlyTest-DX11-c3b127bc-r2.zip** from the [early-test release](https://github.com/FleshofBeast/FOBs-Anomaly-CoopNet/releases/tag/coopnet-early-test-c3b127bc). Choose the named ZIP asset, not GitHub's source-code archives.
3. Close the game and back up your saves and existing `bin`/`gamedata` folders.
4. Extract the package and copy its `bin` and `gamedata` folders into the Anomaly game root, beside `fsgame.ltx`. Merge folders and replace matching files; keep the other base-game files.
5. Clear the shader cache through the Anomaly launcher, then select **DX11 with AVX disabled**. Only `bin\AnomalyDX11.exe` contains CoopNet; the base game's DX11-AVX and other renderer executables do not. If launching through a shortcut, set its “Start in” folder to the game root containing `fsgame.ltx`. Install Microsoft's [x64 Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) if missing, and [DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=8109) for missing D3DX libraries (extract the installer and run `DXSETUP.exe`).
6. Use the same package and matching game/mod files on every player's PC. Follow the host/join and port-forwarding sections below, or read `INSTALL.txt` inside the ZIP.

Start with clean Anomaly 1.5.3; arbitrary modpacks are not verified. The compiled package includes the networking DLLs and matching gamedata, but not the base game. **Visual Studio and Git are only required if you choose to build from source.**

Use package revision **r2**: it also includes matching ICU, TBB, Discord, and audio engine DLLs. If launch still fails, report the exact error and last 40 lines of the newest `.log` file under `appdata\logs`, plus the selected renderer and AVX setting.

## 1. Optional source build: prepare the game and build tools

Install Anomaly 1.5.3 separately and launch it once to check that it works. Back up your saves before testing CoopNet. Start with an unmodified game; host and guests need matching game files, mods, and CoopNet builds.

Download the base game from either of these pages:

- [Anomaly 1.5.3 full release on ModDB](https://www.moddb.com/mods/stalker-anomaly/downloads/stalker-anomaly-153).
- [Anomaly download and installation page](https://anomalymod.com/download-install/), which also links a torrent option.

Use the full **1.5.3** release. These downloads provide the base game; they do not include this CoopNet modification.

On Windows, install Git and Visual Studio 2022 with **Desktop development with C++**, the **v143 toolset**, a **Windows SDK**, and the matching **v143 MFC and ATL** components. Visual Studio 2026 requires the side-by-side **14.44 (17.14)** compiler and matching MFC/ATL components; see [build details](BUILD-AND-RUN.md).

Arrange your folders like this (the parent folder can have any name):

```text
StalkerDev/
  Anomaly-1.5.3/       # Full game, including bin and fsgame.ltx
  xray-monolith/      # Clone this repository here
```

The supplied build and launch scripts expect the game folder to be named `Anomaly-1.5.3` beside the repository.

## 2. Download, build, and install CoopNet

Open PowerShell in the parent folder and run:

```powershell
git clone --branch coopnet https://github.com/FleshofBeast/FOBs-Anomaly-CoopNet.git xray-monolith
cd xray-monolith
powershell -ExecutionPolicy Bypass -File .\setup-coopnet-deps.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration DX11 -CoopNet -Deploy
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

Close the game before deployment. The first dependency and engine build can take substantial time. Stop if either command reports a failure; check `build-DX11.log` for engine build errors. **Keep `-CoopNet` in the build command**: ordinary builds do not enable the networking library.

Deployment copies the executable, networking DLLs, and matching repository gamedata into the game. It backs up replaced files under the parent folder's `backups` directory and moves the shader cache into that backup so it regenerates. Use the DX11 client built here on every player's PC.

To update an existing clone, close the game, run `git pull --ff-only` on the `coopnet` branch, then repeat the dependency and build/deploy commands. Everyone should update to the same revision.

## 3. Host a game

Set your name under **Options → Gameplay → General → Player name**. Load the save you want to host. Open the in-game console (usually `~`) and enter:

```text
coop_host 27888 1 1 1
coop_status
```

The arguments are UDP port, character ID, game fingerprint, and mod fingerprint. Each player needs a different nonzero character ID: host `1`, guests `2`, `3`, and `4`. The example fingerprints `1 1` are manual compatibility identifiers; they do not inspect your installed files. Keep them identical on all clients and check file/mod compatibility yourself.

## 4. Join the host

Choose **Main Menu → Join CoopNet**, enter `HOST-IP:27888`, and select **Load save** or **Create character**. Choose a character in the **same faction as the host**. A different faction is rejected; CoopNet does not convert that character or rewrite its private save.

The console connection command is:

```text
coop_join HOST-IP:27888 2 1 1
```

Select your character through the join menu first. Replace `HOST-IP` with:

| Where you are playing | Address to use |
| --- | --- |
| Two clients on one PC | `127.0.0.1` |
| Two PCs on the same LAN | Host PC's local IPv4, such as `192.168.1.100` |
| Different internet connections | Host router's public IPv4 |

For two clients on one PC, use separate game installations and separate appdata/save folders. Only one instance hosts. LAN and same-PC connections need no router forwarding.

## 5. Port forwarding for internet play

Only the host needs these router settings:

1. Run `ipconfig` in Windows PowerShell or Command Prompt and find the host PC's active adapter **IPv4 Address**. Reserve that address in the router's DHCP settings.
2. Forward **UDP external port 27888 → internal port 27888**, with the destination set to that **local IPv4 address**. A router field called “server IP” normally means this local address, not your public internet address. It must belong to your router's LAN subnet.
3. Allow the game executable through Windows Firewall, or allow inbound UDP on that port for the active network profile.
4. Give your friend your **public IPv4 address** plus `:27888`. Test from a different internet connection; some routers cannot connect to their own public address from inside the LAN.

If you host on a different port, use it in every command and forwarding rule. TCP forwarding is unnecessary. CoopNet currently has no automatic NAT traversal, Steam friends joining, or relay. Double NAT requires forwarding through both routers; carrier-grade NAT requires a reachable public IPv4 from your ISP or a VPN connecting the players.

## Commands and current limits

Enter `/help` in the game console for available commands. Hosting uses `coop_host`, not `/host`. Use `coop_status` to inspect the session, `coop_disconnect` to leave or stop hosting, and `coop_respawn` to respawn at a living teammate when downed.

The host controls the shared world settings, and players travel between locations together. Selected guest saves transfer faction, inventory, equipment, and rubles. **Importing saved personal NPC goodwill, rank/reputation, and individual NPC relationships is unfinished.** Fresh characters use solo faction defaults; a fresh Free Stalker should not make Wolf hostile. Not every mutant's specialized ability or modded AI script has been verified. Quest turn-in and shared rewards also remain unfinished; this is a testing build, not a complete co-op release. See [current progress and test evidence](docs/coop/PROGRESS.md).

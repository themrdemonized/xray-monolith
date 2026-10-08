Keep this repository beside the `Anomaly-1.5.3` client folder. Build from the repository folder in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Deploy
powershell -ExecutionPolicy Bypass -File .\run.ps1
```

The default is DX11, x64. The build uses `src/engine-vs2022.sln` and requires the Visual Studio v143 toolset, Windows SDK, and matching v143 MFC/ATL libraries. On Visual Studio 2026, the script uses v145 build integration with the side-by-side 14.44 compiler and its matching MFC/ATL libraries. The default newer compiler lacks the legacy `hash_map` header required by this source.

`-Deploy` backs up replaced executables and loose game files under `backups`, copies the built executable and matching repository gamedata into Anomaly-1.5.3, and moves the shader cache into the backup so it regenerates. Build without `-Deploy` to compile only. The client must be closed during deployment.

For debugging, use `build.ps1 -Configuration VerifiedDX11 -Deploy`, then `run.ps1 -Renderer VerifiedDX11`. This configuration uses considerably more memory and runs slower. Open the solution in Visual Studio and attach the native debugger to the running executable.

Build logs are saved in the repository folder. Engine outputs are in `_build/_game/bin_dbg`. Backups are saved in the parent folder under `backups`. `run.ps1` launches from the client root so `fsgame.ltx` and game archives resolve correctly.

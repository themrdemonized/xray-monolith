# Native Static Geometry tests

Build the engine's DX11 x64 solution first, including its documented submodule.
Run this test project with the same VS2022/SDK and runtime settings. Example:

```
MSBuild R1_Tests/R1_Static_Geometry/R1_CDB_Tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

Run `_build/R1_Static_Geometry_Tests/R1_CDB_Tests.exe`. Expected: 19 checks
and 10000 concurrent queries while toggling a mask 500 times.

These tests link the real compiled xrCDB/xrCore libraries and exercise a real
OPCODE tree, IEEE CRC32, invalid ID atomicity, nearest/first ray hits, box/frustum
filtering, immutable snapshots, reset/no-op revisions and an
independent model. The console host supplies only logging/version services; it
does not initialize a renderer, Lua VM or ODE gameplay. Those need separate native
game acceptance. No game resources, binaries or saves are included here.

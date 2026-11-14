# R1 XREAL 0.1.4: off-centre lighting reconstruction

RU: На копии пользовательского quicksave_2 от 29.09.2026 02:17 воспроизведён сильный перекос света в 0.1.3. Отключение солнца и shadow-light occlusion query его не убрало. При IPD=0 оба глаза практически совпали. При IPD=64 мм и сведении 10000 м перекос исчез. Исправление ниже сохраняет стандартное сведение 2 м. Это исправление конкретного ракурса, не доказательство исправления всех материалов, FPS или стабильности выхода.

EN: User quicksave_2 reproduced a substantial eye-dependent lighting mismatch in 0.1.3. Sun off and skipping shadow-light occlusion queries did not remove it. Zero IPD produced almost identical eyes; 64 mm IPD with 10000 m convergence removed the mismatch. The correction below retains normal 2 m convergence. This validates the inspected view, not every material, performance or shutdown stability.

## Root cause and portable engine correction

`src/Layers/xrRenderPC_R4/r4.cpp`, `cl_pos_decompress_params::setup`, derives `pos_decompression_params` from FOV/aspect only. The G-buffer reconstruction expression is:

```cpp
view.xy = depth * (pixel.xy * params.zw - params.xy);
```

An off-centre row-vector projection adds `_31` / `_32` to NDC. Its inverse therefore needs an additional origin correction, without changing pixel scale:

```cpp
params.x += Device.mProject._31 / Device.mProject._11;
params.y += Device.mProject._32 / Device.mProject._22;
```

For a source integration, apply these additions to HorzTan/VertTan only in the first two arguments of `RCache.set_c`; retain the original third/fourth arguments. Symmetric projection has zero correction. Verify custom shader reconstruction formulas before applying this to another shader pack. The shader expression was cross-checked in a local extracted shader reference; the target runtime experiment provides the evidence for this installation. No full engine rebuild is claimed here.

## Standalone exact-host implementation

`native/R1_native.cpp::onDecompress` calls the original binder, then adjusts the CPU constant-buffer origin while a stereo eye render is active. No shader replacement and no EXE file replacement. No container allocation/free in engine memory. Null/bounds checks refuse unexpected buffers with error -27. Existing exact EXE hash and 24-byte signature guard now covers eight hooks.

Matched September DX11 PDB: binder RVA 0xaf7000; RCache RVA 0x15d53d0; pixel/vertex/geometry/hull/domain/compute buffer arrays +1256/+1144/+1368/+1480/+1592/+1704, 14 entries each. R_constant destination +28; loads +32..+52 with ushort byte offset. dx10ConstantBuffer size +112, CPU data +120, changed flag +128. Stage masks and packed buffer indices follow dx10r_constants_cache.cpp and R_Constants.h. Do not reuse offsets for another EXE.

## Evidence and remaining limits

First corrected run: 396 pairs at checkpoint, native error 0, inventory/console/Escape/re-enable scenario completed, process exit 0. Standard IPD/convergence restored. `tests/R1_math_test.cpp` now round-trips off-centre projection through pixel/depth reconstruction at several depths and shifts. Native build, math, pair-state and WARP composition tests pass.

Two earlier diagnostic runs encountered shutdown heap corruption 0xC0000374. Later successful exits do not resolve that finding. FPS improvement is not established. PR must remain Draft / DO NOT MERGE. User saves and game installation are not modified by the fixture and are not distributed.

Final versioned DLL: SHA256 121BB9D425A32B295ED5EB706CFB4B1FAF040956D9809717047B4A7F41323CFB. Repeat on the same save: 396 pairs, error 0, process exit 0. Player ZIP CRC and exact-byte comparison passed for 9 files. This DLL is packaged, not automatically installed into the working game.

# Temporal antialiasing

The DX11 renderer provides a self-contained temporal antialiasing mode:

- `r4_temporal_aa taa` enables TAA for the world.
- `r4_temporal_aa off` disables it.

MSAA must be disabled. The TAA resolve uses jittered world geometry, camera-motion reprojection, IX-Ray-style variance clipping, and sharpened history reconstruction. SMAA is evaluated independently and composited only through the near-geometry reactive mask. First-person hands and weapons are rendered without projection jitter and receive zero temporal history, leaving their antialiasing to SMAA without softening the world twice.

The implementation has no external SDK dependency.

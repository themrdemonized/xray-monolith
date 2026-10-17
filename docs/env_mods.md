# Environment Modifiers

---

## Intro

Environment modifiers are spheres in a level that blend the weather's view distance, fog and colours towards their own values while the camera is inside them.

They are normally loaded from the binary `levels\<level>\level.env_mod`. If `configs\environment\env_mods\<level>.ltx` exists, the engine loads that instead and skips the binary, so an .ltx with no sections turns the level's modifiers off. `<level>` is the level name, for example `l05_bar`.

`configs\environment\env_mods\_template.ltx` has a section with all fields to copy from.

---

## Format

Each section is one modifier, the section name can be anything. Sections without `position` are skipped.

```ini
[modifier_3]
position    = 151.71005, -86.26001, 103.80662
radius      = 80
power       = 100
far_plane   = 90
fog_color   = 0.48235297, 0.50980395, 0.54509807
fog_density = 0.7
ambient     = 0.078431375, 0.08627451, 0.09019608
sky_color   = 0.48235297, 0.50980395, 0.54509807
hemi_color  = 1, 1, 1
use_flags   = 0x3F
```

| field | value |
|---|---|
| `position` | x, y, z, centre of the sphere |
| `radius` | radius in metres |
| `power` | strength at the centre |
| `far_plane` | view distance |
| `fog_color` | r, g, b |
| `fog_density` | fog density |
| `ambient` | r, g, b |
| `sky_color` | r, g, b |
| `hemi_color` | r, g, b |
| `use_flags` | optional, which values are applied, default all |

All fields except `use_flags` are required, a missing one crashes the game with the field name.

`use_flags` is a sum of:

| flag | field |
|---|---|
| `0x01` | `far_plane` |
| `0x02` | `fog_color` |
| `0x04` | `fog_density` |
| `0x08` | `ambient` |
| `0x10` | `sky_color` |
| `0x20` | `hemi_color` |

---

## Blending

A modifier's weight is `power * (1 - distance / radius)`. Each applied value is a weighted average of the weather's value and every modifier the camera is inside:

```
result = (weather + value_1 * weight_1 + value_2 * weight_2 ...) / (1 + weight_1 + weight_2 ...)
```

The divisor includes every modifier the camera is inside, whatever its `use_flags`.

`far_plane` is then scaled by `rs_vis_distance`, and the weather's `fog_distance` is capped at `far_plane - 10`.

---

## Console command

`env_mods_reload` re-reads the modifiers for the current level without reloading it.

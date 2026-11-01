# Shader Bus

---

## Intro

The shader bus replaces the eight anonymous `shader_param_1..8` and `s3ds_param_1..4` float4
constants with lanes that each have an id and an `owner`. Any script can register a lane by name, any script can read
any lane, and only the script that registered a lane can write it. The engine needs no advance
list of names, so a mod can claim a lane without anything being patched into the exe for it.

Requires a modded exe with the bus. `shader_bus.version()` returns `8`. A script that wants
to detect the feature should test `shader_bus ~= nil` first, since an older exe has no such
module at all.

---

## The shader side

Declare a `float4` uniform whose name starts with `bus_`.

```hlsl
uniform float4 bus_myeffect;
```

That is all. When the pass compiles, the renderer sees the name in the reflected constant table
and attaches a binder that writes the lane's current bound value on every constant table switch.
The part of the name after `bus_` is the lane id, so `bus_myeffect` is the lane `myeffect`.

A lane binds a `float`, `float2`, `float3` or `float4`. On DX10 and DX11 it also binds an array
of those, a `float4x4`, `float3x4` or `float2x4` matrix, or an array of matrices, as rows of 16
bytes: HLSL element `i` of an array is row `i`, matrix row `i` is row `i` (the engine compiles
shaders row major), and an array of matrices runs its rows one matrix after the other. A row the
script never set reads `0, 0, 0, 0`, and rows past what a shader declares are not written. A lane
has at most 4096 rows. On DX9 an array or matrix lane binds to nothing.

```hlsl
uniform float4 bus_mypath[8];
uniform float4x4 bus_mymatrix;
```

On DX10 and DX11 a lane also binds `uint` through `uint4` and arrays of them, for flags, ids and
bit masks a float cannot store exactly. The script writes those with `set_uint` or
`set_array_uint` and the shader reads the exact 32 bit values. A `uint` constant whose name does
not start with `bus_` still stops shader compilation as before. A uint declaration reads the raw
bits of whatever the lane stores, so a lane written with `set` reads float bits there, and `list`
shows the mismatch through `kind` and `declared_kinds`.

A lane whose id starts `obj_` is an object lane: each draw of an object reads that object's own
value, and a draw of an object without one, or of level geometry, reads the lane's value. A draw
of the player's hands, weapon or anything else on the HUD reads the actor's value. An object lane
binds a `float` to `float4`, and one pass binds at most 16 of them; the rest bind to nothing.

```hlsl
uniform float4 bus_obj_mymark;
```

A lane can also show a texture. A `.s` or blender that uses the texture `$user$bus_<id>` samples
whatever texture or render target the script that registered the lane picks with `set_texture`, and an empty view
until it picks one. The texture follows a render target the engine recreates, for example after
a resolution change, and the lane keeps its texture over a level change.

```lua
-- in the .s
shader:dx10texture("s_mymap", "$user$bus_mymap")
```

On version `7`, a texture lane declared in more than one stage of one pass, say a `Texture2D
bus_mymap` read in both the vertex and the pixel shader, stops a release exe while the shader
compiles. Declare a texture lane in one stage of each pass. From version `8`, the second stage is
refused with a log line and the first stage keeps the binding. Only a `bus_` identifier gets the
refusal, and a texture of any other name read in two stages of one pass still stops the exe.

From version `8` on DX10 and DX11, a shader that declares `Texture2D bus_<id>` gets
`$user$bus_<id>` bound without a `.s`, in every blender, the engine's own C++ blenders included.
A texture the `.s` binds to the same slot wins.

From version `8` on DX10 and DX11, a lane can also be a typed buffer of up to 65,536 rows, for data past
the 4096 rows of a constant lane. `Buffer<float4> bus_<id>` reads a float lane and
`Buffer<uint4> bus_<id>` a uint lane. Any `Buffer` with a float or uint return type binds, over a
four component view, so a narrower type reads the leading components. `Load(i)` reads row `i`,
a row the script never set reads `0, 0, 0, 0`, and the buffer is empty until the script calls
`set_buffer` (see Buffer lanes). A float declaration reads the buffer while the lane holds float
rows and a uint declaration while it holds uint rows.

```hlsl
#if defined(SHADER_BUS) && SHADER_BUS >= 8
Buffer<float4> bus_marks;   // rows from set_buffer, $user$busbuf_marks
Buffer<uint4>  bus_ids;     // uint lane, $user$busbufu_ids
Texture2D      bus_hist;    // set_texture, $user$bus_hist
#endif
```

A `bus_` texture or buffer bound past the slots of its stage, 16 in the pixel shader, 4 in the
vertex shader and 16 in the others, binds to nothing and logs one line.

A `bus_` constant declared as an `int` or a `bool`, an array or matrix on DX9, or one larger than
4096 rows binds to nothing, the engine never writes it, and the log gets one line with the
constant's name and the reason. A pass whose shaders declare one lane with different types, say
a `float4` in the pixel shader and an `int4` in the vertex shader, binds the lane only in the
stages that agree with the first stage the engine reads (pixel, then vertex, geometry, hull,
domain and compute), and logs the clash once. When that first stage declares a type a lane cannot
bind, no stage binds.

Declaring costs nothing extra to compile and nothing to run if the constant is never read. A lane
nobody has registered reads `0, 0, 0, 0`, so an unclaimed lane behaves like an unmodded install as
long as your shader treats an all-zero value as off. A refused `bus_` constant is never written, so
its value is undefined, never zero.

From version `7` on, every renderer compiles every shader with `SHADER_BUS` defined as the bus
version, so a shader can keep its lane reads away from an exe that cannot fill them:

```hlsl
#if defined(SHADER_BUS) && SHADER_BUS >= 7
    float level = bus_cvar_r__nightvision.x;
#else
    float level = 0;
#endif
```

- A shader that never tests `SHADER_BUS` compiles to the same bytecode as before.
- Buffer lanes and the `Texture2D bus_<id>` bind need `SHADER_BUS >= 8`. Version `7` leaves those
  declarations unbound.
- Versions `1` to `6` have the bus but do not define `SHADER_BUS`, so a shader that tests it takes
  its fallback there too.
- The version is part of each shader cache file name, so the first start on an exe of a new
  version compiles every shader once. Cache files from other versions stay on disk unused.

---

## The script side

```lua
local token = nil

function on_game_start()
    if shader_bus then
        token = shader_bus.register("myeffect", "My Mod Name", "what this lane is for")
    end
end

function actor_on_update()
    if token then
        shader_bus.set(token, x, y, z, w)
    end
end
```

Register once, in `on_game_start`. That runs before the level finishes loading, so the lane is
already in the registry by the time the engine's level-load log prints and by the time any other
script's `shader_bus.list()` call would see it.

A refused registration returns `nil`, never a number, so `if token then` is the whole check.
Do not test the token against `0`, which is truthy in Lua.

### API

| call | returns |
|---|---|
| `shader_bus.register(id, owner, description)` | a token, or a hard fatal, with both `owner` strings and both script paths, if the id is already registered with a different `owner`, or with the lane's `owner` strings if the lane is shared and is not `obj_hotness` or `obj_tint`. A bad id, a missing `owner`, an `owner` over 64 characters, a description over 256 characters, an `engine_` or `cvar_` id or a full bus returns `nil` with a log line (once per game for a full bus), so check the token before using it |
| `shader_bus.try_register(id, owner, description)` | a token, or `nil` if the id is already registered with a different `owner` or under the same rules as `register`. A shared lane other than `obj_hotness` or `obj_tint` returns `nil` with a log line |
| `shader_bus.register_shared(id, owner, description)` | the token of this `owner` on a shared lane, or `nil` with a log line if a script took the lane with `register` or `try_register`, or under the same rules as `register`. See Shared lanes |
| `shader_bus.set(token, x, y, z, w)` | `true` if the token is valid and all four values are finite. A NaN or infinite value is dropped, the lane keeps its last value, and the first refusal on each lane logs a line |
| `shader_bus.set_array(token, first, rows)` | `true` if the token is valid, `first` is a whole number from `0`, `rows` is a non-empty array of rows `{x, y, z, w}` (a missing component reads `0`), every value is finite and the last row is below row 4096, or below the size `set_buffer` gave a larger buffer lane. One call writes at most 4096 rows. Writes `rows[1]` to row `first` (0-based, the HLSL element index) onward and leaves other rows as they were. Row 0 is the value `set` writes, so `set` and `set_array` from row 0 overwrite each other. The first refusal on each lane logs a line |
| `shader_bus.set_uint(token, a, b, c, d)` | `true` if the token is valid and all four values are whole numbers from `0` to `4294967295`. The lane becomes a uint lane. A value out of range, negative or fractional is dropped and the first refusal on each lane logs a line |
| `shader_bus.set_array_uint(token, first, rows)` | `set_array` with unsigned rows, under the same value rule as `set_uint` |
| `shader_bus.set_object(token, obj, x, y, z, w)` | `true` if the token is valid, the lane id starts `obj_`, `obj` is a game object, a `ScriptAttachment` or a particles object and all four values are finite. Draws of that object read the value from the next frame on, until `clear_object` or the object goes away. The first refusal on each lane logs a line |
| `shader_bus.clear_object(token, obj)` | `true` under the same token, lane and object rule. Draws of that object read the lane's value again from the next frame on |
| `shader_bus.set_texture(token, name)` | `true` if the token is valid and `name` is a texture or render target name of at most 255 characters other than a `$user$bus_` alias. From the next frame on, or the same frame on a late lane, `$user$bus_<id>` shows it; `""` empties it. A texture not yet loaded loads that frame. A name with no file behind it still returns `true`, and the engine logs `! Can't find texture` and shows its missing texture placeholder. A target or history lane refuses. The first refusal on each lane logs a line |
| `shader_bus.set_readback(token, source, x, y, w, h, reduce)` | `true` if the token is valid and the lane is not shared, `source` is `light`, `scene`, `backbuffer`, a render target id or its `<id>_prev`, `x` and `y` are from `0` to `1`, `w` and `h` are whole numbers from `1` to `32` and `reduce` is `none`, `mean`, `min` or `max`. Arms a readback of the `w` by `h` block centred at `x, y` from the next frame on. `""` as `source` disarms the lane and checks only that `w` and `h` are whole numbers. A 17th armed lane and a renderer other than DX11 refuse. The first refusal on each lane logs a line. See Readback |
| `shader_bus.set_buffer(token, rows)` | `true` if the token is valid, the lane is not shared and `rows` is a whole number from `1` to `65536` that keeps every buffer lane together within 16 MiB, `engine_mask_objects` not counted. Makes the lane a buffer of `rows` rows from the next frame on. The size only grows: a call at or below the current size returns `true` and changes nothing, and new rows read `0`. A renderer other than DX10 and DX11 refuses. The first refusal on each lane logs a line. See Buffer lanes |
| `shader_bus.set_late(token, on)` | `true` if the token is valid and the lane is not shared. From the next frame on, `true` copies the lane again after the frame callbacks, so a write from those callbacks reaches the same frame's draws, and `false` stops it. The first refusal on each lane logs a line. See Late lanes |
| `shader_bus.register_target(id, owner, description, spec)` | a token, or `nil` with a log line for a bad spec field, an id over 27 characters or one the engine reserves, an id another `owner` or another kind of lane holds, a renderer without targets, 16 live targets or targets that would pass 1024 MB. See Render targets and passes |
| `shader_bus.register_pass(id, owner, description, spec)` | a token, or `nil` with a log line for a bad spec field, an id the engine reserves or another `owner` or kind of lane holds, a renderer without passes, a `cs` pass on a renderer without compute, or 32 live passes. See Render targets and passes |
| `shader_bus.enable_pass(token, on)` | `true` if the token is a live pass. `false` stops the pass drawing, `true` on a stopped pass runs it again from a fresh start, and a call that does not change the state changes nothing |
| `shader_bus.release_pass(token)` | `true` if the token is a pass. The pass stops and its slot is free, and the id can be registered again |
| `shader_bus.release_target(token)` | `true` if the token is a target. The target and its history are freed and the id can be registered again |
| `shader_bus.get_readback(id)` | `ok, x, y, z, w, frame`, texel 0 of the lane's last readback result and the frame it was captured on, `false` with zeros while there is no result |
| `shader_bus.get_readback(id, i)` | the same for texel `i` (0-based, row-major) of a `none` result, `false` past the last texel. A reduced result has only texel `0` |
| `shader_bus.set_readback_rows(token, source, first, count, reduce)` | `true` if the token is valid and `source` is a buffer lane. Arms a readback of `count` rows, `1` to `1024`, from row `first` of the buffer, read after the frame's last pass. `reduce` as for `set_readback`, refused for uint rows. See Readback |
| `shader_bus.get_readback_uint(id)` | `ok, x, y, z, w, frame` as unsigned integers, for a result read from uint rows |
| `shader_bus.get_readback_uint(id, i)` | the same for row `i` of the result |
| `shader_bus.get_pass_time(id)` | `ok, x, y, z, w, frames`, the `pass_time` of the pass's last run and the frames since it ran, `1` for a pass that ran last frame, `false` for a pass that has not run since it last registered (see pass_time) |
| `shader_bus.get(id)` | `ok, x, y, z, w`, the bound value, one frame behind the latest `set` |
| `shader_bus.get_uint(id)` | `ok, a, b, c, d`, the bound value read as four unsigned integers |
| `shader_bus.get_row(id, i)` | `ok, x, y, z, w`, bound row `i` (0-based), `0, 0, 0, 0` for a row never set, `false` past the rows a shader declared or `set_buffer` sized. Row 0 always answers, like `get`. On a uint lane the values are the raw unsigned integers |
| `shader_bus.get_object(id, obj)` | `ok, x, y, z, w`, what draws of `obj` read, its own value or the lane's, `false` with zeros for an unknown lane or a `nil` object |
| `shader_bus.get_texture(id)` | the texture name `$user$bus_<id>` shows, or `nil` for none or an unknown id |
| `shader_bus.get_pending(id)` | `ok, x, y, z, w`, the value last written through the token and not yet copied into the bound value, on a shared lane the largest value of its tokens. On a uint lane the values are the raw unsigned integers |
| `shader_bus.writers(id)` | rows `{owner, x, y, z, w}`, one for each token of a shared lane that has called `set`, with that token's value, or an empty list for a lane that is not shared or an unknown id |
| `shader_bus.writers(id, obj)` | the same rows for the tokens that gave `obj` its own value, or an empty list for a `nil` object |
| `shader_bus.has(id)` | `true` for a lane a script registered or a shader declared, so a declared lane nobody registered answers `true` while `owner_of` gives `nil` |
| `shader_bus.describe(id)` | the description string, or `nil` for an id with no registered lane, the description of the first `owner` on a shared lane |
| `shader_bus.owner_of(id)` | the `owner` string, or `nil` for an id with no registered lane, the first `owner` on a shared lane |
| `shader_bus.last_refusal(id)` | a short fixed reason for the kind of the last refused call on `id`, a write or a registration, or `nil` when `id` is not among the last 64 ids refused. A write reason is the text after "refused" in its log line, a registration reason need not match its log line, and a refused history lane of `register_target` is kept under the target id too |
| `shader_bus.stats(id)` | `ok, changes, last_change_frame, bound_frame, writes` |
| `shader_bus.list()` | rows for the registered lanes, then the twelve legacy lanes |
| `shader_bus.list(true)` | the same, plus a row for every lane a shader declared that nobody registered |
| `shader_bus.values()` | a table from lane id to an array of four numbers `{ x, y, z, w }` by position, uint bits on a uint lane, the bound value of every lane a script registered or a shader declared, as `get` and `get_uint` give it by the lane's kind, read under one lock. The twelve legacy lanes are not in it |
| `shader_bus.gpu_lanes()` | `true` on a renderer with readbacks and `cs` passes, DX11 only |
| `shader_bus.targets()` | `true` on a renderer with render targets and passes, DX10 and DX11 |
| `shader_bus.masks()` | `true` when mask tags can reach the mask target, DX10 and DX11 on a device created with MSAA off. See Masks |
| `shader_bus.version()` | `8` |

Each `list` row has `id`, `owner`, `description`, `state` (`registered`, `declared` or
`legacy`), `source` (the script path that called `register`, empty for a declared or legacy row),
on a registered or declared row `forced` (`true` while `bus_force` overrides the lane), `rows` (the
rows `set_array` has filled, `0` for a lane only `set` writes), `declared_rows` (the most rows
any shader declares, `0` before a shader declares the lane), `kind` (`float` or `uint`, what the
last write stored, or what a shader declared before any write), `declared_kinds` (an array of
every kind a shader declared the lane as), `objects` (how many objects have their own value on an
object lane, or hold the bit of a mask lane, attachments and held items included), `shared`
(`true` for a shared lane), `writers` (how many tokens a shared lane has handed out, `1` for a
registered lane that is not shared), `texture` (the texture the lane shows,
empty for none), `readback` (`none`, `ok`, `refused` or `lost`, see Readback), `readback_inflight`
(the copies taken and not yet decoded, `0` to `3`), `buffer_rows` (the
rows of a buffer lane as of the last frame, `0` for a lane that is not one), `late` (the last
`set_late` value, the lane latches late from the next frame), `refusals` (how many writes the lane
refused) and `refusal` (the reason of the last one, empty for none), and, on a legacy row only,
`writer`. Registered rows come first (declared rows too, with the
`true` argument), then the twelve legacy rows, and a legacy row only appears if that console
command still exists on the running exe.

A row also has `role` (`target`, `history` or `pass` for the lanes of Render targets and passes,
`mask` for a mask lane with its bit in `mask_bit`, see Masks, and empty for a plain lane). A target
row and its history row `<id>_prev` carry `width`, `height`, `format`, `bytes`, `history`, `mips`
and `target_state` (`pending`, `ready` or `released`), with the size and bytes `0` until the target
exists and `bytes` covering the mip chain. A pass row carries `stage`, `order`, `pass_state`
(`pending`, `ready`, `failed`, `off` for a disabled pass or `released`) and `reason`, the text of
the failure and empty otherwise, `fails` (how many builds of the pass failed since it was registered
with this spec), and from its spec `ps` or `cs`, `outputs`, `inputs`, `blend`, `rect` when the spec
gave one smaller than the output, `svp`, `runs` when it is above `1`, `pingpong` when the spec gave
one, and on a `cs` pass `group`, and `grid` and `indirect` when the spec gave one.

`writes` in `stats` counts every `set` call made through the lane's token. `changes` counts the
times the published value moved, once a frame at most except on a late lane, which can move at
both latches, so a lane rewritten every frame with an unchanging value shows many writes and no
changes. `last_change_frame` is the last frame the bound
value changed, `bound_frame` is the last frame a shader pass read the lane, so a `bound_frame`
far behind the current frame means the lane is bound to a shader nothing is drawing. `stats`
answers for a declared-but-unregistered lane too; it comes back `false` with all zeros only when
no lane by that id exists at all.

Reads and listing are open to every script. Only the token returned by `register`,
`try_register` or `register_shared` can write.

### Engine lanes

The engine registers five lanes itself, two on DX9, with the `owner` string `engine`, and fills them every frame once a
shader has declared them, so they cost nothing until one does. The declaration lasts until the
game exits. Read them in a shader like any lane.

| lane | value |
|---|---|
| `engine_sun_los` | `x` is `1` when nothing blocks a ray of 500 m from the camera toward the sun, else `0` |
| `engine_sky_open` | `x` is how open the sky is over the view entity, from `0` to `1`, the value `level.rain_hemi()` returns |
| `engine_hud_mask` | `x` is `1` while the mask target holds this frame's HUD bits, else `0`. Declaring it makes the engine draw the HUD bits, on DX10 and DX11 only (see Masks) |
| `engine_mask_objects` | a buffer lane, on DX10 and DX11. Row 0 `x` is how many objects hold a live mask bit, and rows 1 to `x` hold one object each, `x, y, z` its position and `w` its live mask bits as a whole number. Declare it as `Buffer<float4>`, see Masks |
| `engine_mask_ids` | `x` is `1` while the id target holds this frame's object ids, else `0`. Declaring it makes the engine draw the ids, on DX10 and DX11 only (see Object ids) |

```hlsl
uniform float4 bus_engine_sun_los;
```

Ids starting `engine_` belong to the engine. `register` and `try_register` refuse them with a log
line and `nil`, never a fatal. A script checks for an engine lane with
`shader_bus.owner_of("engine_sun_los") == "engine"`. The prefix also covers the texture
`$user$bus_engine_scene_copy`, which a pass reads as `s_image` at the `scene` stage (see Render
targets and passes). `register_target` and `register_pass` refuse an `engine_` id too.

On DX11 the engine also keeps a render target a pass can list in `inputs`,
`$user$ssfx_motion_vectors`, with four 16-bit float channels, multisampled under MSAA. It holds
whatever the shaders write into it, and the engine gives its channels no meaning. The stock shaders
never write it. The engine clears it each frame only when `shaders\r3\screenspace_mvectors.h`
exists, so without that file it is never cleared.

### Console lanes

A shader that declares `bus_cvar_<name>` reads the console value `<name>`, copied once per frame
like any lane, so `bus_cvar_r__nightvision` reads `r__nightvision` with no script.
The engine registers the lane with the `owner` string `engine` when a shader first declares it and logs the
registration. Nothing is copied while no shader declares a console lane.

```hlsl
uniform float4 bus_cvar_r__nightvision;
```

| console value | lane value |
|---|---|
| on/off flag | `x` is `1` or `0`, `y, z, w` are `0` |
| integer | `x` is the integer, `y, z, w` are `0` |
| float | `x`, `y, z, w` are `0` |
| three floats | `x, y, z`, `w` is `0` |
| four floats | `x, y, z, w` |
| four integers or a colour | `x, y, z, w` |

A command that does not exist on the running exe, or has a text value or a choice from a list, leaves
the lane at `0, 0, 0, 0` and logs one line with the constant's name. Declare a console lane as `float`
to `float4`; a `uint` declaration reads the float bits.

Limits:
- The console name follows the lane id rules, lowercase letters, digits and `_`, at most 27
  characters after `cvar_`. A longer name or one with an uppercase letter never finds its command.
- The lane has the value the console had at the per-frame update, so a change shows one frame
  later, the same as a script write.
- Ids starting `cvar_` are refused to `register` and `try_register` with the same log line as
  `engine_` ids. `bus_force` forces a console lane like any other.
- An exe before version `4` treats `bus_cvar_<name>` as an ordinary lane nobody registered, which
  reads `0, 0, 0, 0` unless a script registers that id.

### Shared lanes

A shared lane has more than one writer, each with its own token. It suits a value several mods
write where the largest one should show.

```lua
local token = shader_bus.register_shared("mylane", "My Mod", "what this lane is for")
if token then shader_bus.set(token, 0.5, 0, 0, 0) end
```

The first `register_shared` call makes the lane shared, a call with another `owner` adds a token for
that `owner`, and a call with the same `owner` returns its existing token. A lane a script already
took with `register` or `try_register` stays with that script, and `register_shared` returns `nil`
with a log line. On a shared lane `register` is fatal and lists the lane's `owner` strings, and
`try_register` returns `nil` with a log line. A shared lane hands out at most 256 tokens.

Each token keeps its own lane value and its own object values. The bound value is the largest
value of the tokens that have called `set`, compared per component, so `x` can come from one token
and `y` from another. `set_object` writes the token's value for that object, and draws of the
object read the per component largest value of the tokens that gave it one. `clear_object` drops
only that token's value, so the object reads the largest of the values other tokens gave it, or
the lane's value once none is left. A token that never wrote has no say in the result.

The per-frame update works out the largest values, only for the lanes and objects a token
wrote since the previous update, and the draw path does not change.

Rows, uint values and textures have no largest value, so `set_array`, `set_uint`,
`set_array_uint` and `set_texture` return `false` on a shared lane, and the first refusal on each
lane logs a line.

On a shared lane `owner_of` and `describe` return the first `owner` and its description,
`get_pending` returns the largest value of the tokens, `writers(id)` lists the `owner` and value of
every token that has called `set`, and `writers(id, obj)` the `owner` and value of every token that
gave `obj` its own value. `bus_force` forces a shared lane like any other, and its `list` row
has `shared` as `true` and `writers` as its token count.

Limits:
- No token can pull a value below another token's value. A mod that needs to lower a value uses a
  lane of its own.
- An exe before version `5` has no `register_shared`, so check for `shader_bus.register_shared`
  before calling it.

### Object hotness

The object lane `obj_hotness` feeds the stock heat constant `L_hotness`. Once a script has given
an object its own value with `set_object`, that object's draws use `x` of the value as their
hotness in place of the one the engine computes, so every shader that reads `L_hotness` sees it
with no shader change. An object without its own value keeps the engine's hotness, and
`clear_object` restores it. Once a shader declares the lane or a script registers it, `bus_force obj_hotness x y z w`
gives every object `x` on its marked textures until `bus_release`. The lane works whether or not
any shader declares `bus_obj_hotness`, and a shader that does reads all four components like any
object lane.

The lane is shared from the start, so every mod gets its own token and draws of an object use the
largest value any mod gave it. On this lane `register` and `try_register` return a token of the
shared lane too, the same as `register_shared`. Once 256 mods have a token, `register` is fatal and
`try_register` returns `nil` with a log line.

```lua
local take = shader_bus.register_shared or shader_bus.try_register
local token = take("obj_hotness", "My Mod", "per object heat")
if token then shader_bus.set_object(token, obj, 0.8, 0, 0, 0) end
```

Limits:
- DX10 and DX11 only. The DX9 renderers never write `L_hotness`.
- Only object draws read it. Level geometry has no object and keeps the stock value.
- An object with its own value draws every surface with it, whatever its textures, so a stove a
  script heats reads hot all over and a creature given a value heats its gear along with its skin.
- Every other draw keeps the engine's texture check. A surface whose texture the engine has not
  marked hot gets a hotness of `0`, as it does without the bus. The engine marks the textures of
  every entity (creatures, the player and vehicles) and of the hands on the HUD. The mark sits on the
  texture, so any other model that uses a marked texture passes the check too.
- A `bus_force` value goes through that check even on an object with its own value, so it applies to
  marked textures only and never heats the rest of the world.
- One lane serves every mod, and no mod can pull an object below the value another mod gave it.
- An exe before version `5` treats `obj_hotness` as an ordinary object lane that `L_hotness` never
  reads.

### Particle tint

The object lane `obj_tint` tints particles. Once a script has given a particles object its own
value with `set_object`, the engine multiplies the colour of each of its particles by `x`, `y` and
`z` and the alpha by `w` before they draw, so every particle shader that reads the vertex colour
shows the tint, the stock ones included. Each channel stays within `0` to `1`, so a value above `1`
brightens a channel only up to full. A particles object without its own value draws as before,
`clear_object` restores it, and the lane value `set` writes tints nothing. Once a shader declares
the lane or a script registers it, `bus_force obj_tint x y z w` tints every particle drawn for an object until
`bus_release`.

The lane is shared from the start, so every mod gets its own token and a particles object draws with
the per component largest value any mod gave it (see Shared lanes). On this lane `register` and
`try_register` return a token of the shared lane too, the same as `register_shared`.

```lua
local token = shader_bus.register_shared("obj_tint", "My Mod", "particle tint")
if token then shader_bus.set_object(token, particles, 1, 0.4, 0.4, 1) end
```

A particle draw reads any `obj_` lane like other object draws, so a shader pack with its own
particle shaders can declare `uniform float4 bus_obj_<id>;` and read the effect's own value there.

### Masks

From version `8` on DX10 and DX11, the engine can draw a mask that tells a shader what drew each
pixel: the player's hands, weapon and other HUD parts by category, and the objects a mod tagged
with a mask bit. A shader reads the mask as a texture, and a script tags objects through a mask
lane.

#### The mask target

The mask is `Texture2D<float> bus_engine_mask`, one R32F texel per screen pixel that holds a 24 bit
tag as tag × 2^-24. `bus_mask.h` declares it and decodes a pixel with `bus_mask_bits`:

```hlsl
#include "bus_mask.h"

uint bits = bus_mask_bits(int2(I.hpos.xy));
```

Bits 0 to 9 are the HUD bits below and bits 10 to 23 belong to mods. A pixel holds the tag of the
last tagged draw whose depth equals the depth buffer there, so only the visible surface counts and
the tags of two draws never combine. A pixel where nothing tagged drew reads `0`.

The target exists while a shader declares `bus_engine_hud_mask` or a mod holds a live mask bit,
and it needs MSAA off, and `vid_restart` keeps the MSAA the device was created with. With MSAA on
the engine logs one `!` line and keeps no target, `masks()` returns `false`, `register_mask` still
returns a token but no tag draws, and without a target the texture reads `0`. The target is cleared
every frame. The HUD bits draw with the HUD and the world tags right after the G-buffer, so the
G-buffer's own draws cannot read this frame's tags, while passes from the `gbuffer` stage on and
the lighting, combine and post shaders can. A pass at half resolution scales its pixel coordinates.
On the frame the target first appears only the HUD bits draw, so a bit registered and set in the
same frame shows on the world from the second frame after the call. A second viewport frame, such
as a scope view, draws no world tags.

#### HUD bits

`bus_hud_mask.h` includes `bus_mask.h` and defines the HUD bits:

| bit | define | what draws it |
|---|---|---|
| `0x001` | `BUS_HUD_ARMS` | the main arms model, the right arm, and the HUD attachments on it |
| `0x002` | `BUS_HUD_ARMS2` | the second arms model, the left arm, and the HUD attachments on it |
| `0x004` | `BUS_HUD_ITEM0` | the HUD item in the first slot, the weapon |
| `0x008` | `BUS_HUD_ITEM1` | the HUD item in the second slot, such as a detector |
| `0x010` | `BUS_HUD_SCOPE` | the HUD item in the scope slot |
| `0x020` | `BUS_HUD_PROP` | the item model of a scripted HUD animation |
| `0x040` | `BUS_HUD_ATT_ACTOR` | HUD and camera attachments of the `Actor` category |
| `0x080` | `BUS_HUD_ATT_ITEM` | HUD and camera attachments of the `Item` category |
| `0x100` | `BUS_HUD_CAM` | camera attachments |
| `0x200` | `BUS_HUD_LENS` | set with the item's bit where a 3D scope lens put world depth back |

| group | define | bits |
|---|---|---|
| `0x043` | `BUS_HUD_ACTOR` | both arms models and `Actor` attachments |
| `0x1BC` | `BUS_HUD_ITEMS` | the items, the scope, the prop, `Item` attachments and camera attachments |
| `0x1FF` | `BUS_HUD_ALL` | every HUD bit but `BUS_HUD_LENS` |

`hud_all(p)`, `hud_items(p)` and `hud_actor(p)` test a group at pixel `p`. They read the engine lane
`bus_engine_hud_mask`, whose `x` is `1` while the target holds this frame's HUD bits, so they are
`false` while it is `0`, and `hud_mask_live()` tests that alone. The engine draws HUD bits only
while a shader declares `bus_engine_hud_mask`, as these helpers do, and world tags alone never
draw them.

`BUS_HUD_LENS` means world depth here. On DX11, a 3D scope lens draws its item's bit with
`BUS_HUD_LENS` and puts world depth back behind it, so the pixel shows the world through the lens
rather than the HUD. A tagged object seen through the lens can draw its own tag over it.

These draw no HUD bit:
- translucent HUD draws, which the engine sorts, such as scope glass and reticles, and HUD
  particles such as muzzle flashes
- the first person legs and body, which are world draws. A mod that tags the actor tags them

The actor's own mod bits never reach a HUD draw. A HUD draw carries its HUD bit, plus the mod bits
of an attachment that was itself tagged.

#### Attachment categories

A script attachment of type `Hud` or `CamAttached` draws with the HUD bit of what it hangs on:
`BUS_HUD_ARMS` or `BUS_HUD_ARMS2` for the arm it is on, `BUS_HUD_CAM` on the camera, and a child
attachment the bit of its parent attachment. `set_hud_category` picks another bit:

| `script_attachment_hud` | HUD bit |
|---|---|
| `Parent` | the bit of what it hangs on, the default |
| `Actor` | `BUS_HUD_ATT_ACTOR` |
| `Item` | `BUS_HUD_ATT_ITEM` |

```lua
att:set_hud_category(script_attachment_hud.Item)
```

`get_hud_category()` returns the category, and a value out of range sets `Parent`. The category
changes only HUD draws. An attachment's own mask bits join its draws whatever its type, but a
`Hud` or `CamAttached` attachment draws them into the mask only while a shader declares
`bus_engine_hud_mask`, so a mod that tags a HUD attachment and reads only world tags sees nothing.

#### Object masks

A mask lane holds one mod bit. A mod registers it, sets the bit on objects, and a shader reads the
bit from the lane and tests it against the mask.

| call | returns |
|---|---|
| `shader_bus.register_mask(id, owner, description)` | a token, or `nil` with a log line, never a fatal. It takes the lowest free bit from 10 to 23 and writes `1 << bit` to the lane as a uint, so `uint bus_<id>` reads the bit in `x`. The same id and `owner` get the same token and bit back while the bit is held. It refuses a bad id, `owner` or description as `register` does, an `engine_`, `cvar_` or `obj_` id, a shared lane, an id another `owner` holds or the same `owner` holds as another kind of lane, a full bus, a renderer other than DX10 and DX11, and a new bit while all 14 are held |
| `shader_bus.set_mask(token, obj, on)` | `true` if the token holds a live bit and `obj` is a game object, a `ScriptAttachment` or a particles object. From the next frame on, `true` sets the token's bit on `obj` and `false` clears it. `nil`, a released token or the token of a lane that is not a mask returns `false`, and the first refusal on each lane logs a line |
| `shader_bus.set_mask_layer(token, on)` | `true` if the token holds a live bit. From the next frame on `true` puts its bit on the mask layer and `false` takes it off. A released token returns `false`, and the first refusal on each lane logs a line. A bit released and handed out again starts off the layer |
| `shader_bus.get_mask(obj)` | the live mask bits `obj` carries, `0` for none or `nil` |
| `shader_bus.release_mask(token)` | `true` for a mask token. From the next frame on the bit draws nowhere, every object that carried it loses it, the lane reads `0`, and the bit returns to the pool, so `register_mask` can hand it out again inside the level. Until that frame, `register_mask` with the same id and `owner` takes it back with the objects that still carry it |

A mask lane takes no other write: `set`, `set_uint`, `set_array`, `set_array_uint`, `set_texture`,
`set_readback`, `set_readback_rows` and `set_buffer` refuse it, and the first refusal logs a line.
Its `list` row has `role` `mask`, `mask_bit`, the bit it holds, until a released bit returns to the
pool, and `objects`, the objects that hold it. An exe before version `8` has no `register_mask`, so
check `shader_bus.register_mask ~= nil` before calling it.

These draws carry an object's tag, its live bits together:
- the opaque dynamic draws of a game object or a level root attachment the renderer draws, in the
  main pass
- an attachment parented to it and an item it holds, with their own bits added to its bits

These never do:
- level geometry and other static draws, water, grass and details
- translucent and sorted draws, emissive and distortion draws, wallmarks and the forward pass
- shadows
- an object the renderer culls, so a fully hidden object draws no tag in the mask (see Mask layer)

`set_mask` also takes a particles object. A tagged world particle draws its tag during the forward
pass, before the `scene` stage, where its alpha reaches `0.5`, over the tag of whatever is behind
it, so passes from the `scene` stage on see it. HUD particles take no tag.

A shader reads every object that holds a live bit through the engine lane `engine_mask_objects`,
which the engine fills once a shader declares it:

```hlsl
Buffer<float4> bus_engine_mask_objects;
uint bus_mymark;

uint count = (uint)bus_engine_mask_objects.Load(0).x;
for (uint i = 1; i <= count; ++i)
{
	float4 o = bus_engine_mask_objects.Load(i);
	if ((uint)o.w & bus_mymark)
	{
		// o.xyz is where the object stands
	}
}
```

The buffer has a row for each object a mod set a bit on, held items and attachments included, at
its own position after the frame's updates, where the frame draws it. Held items and script
attachments set their position while the frame draws, as does an object a script moves during the
draw, so their rows can trail the drawn position by one frame. A script attachment holds its
position from the last frame it drew, and an attachment that never drew holds the origin. The lane
is always a float buffer, so a `Buffer<uint4>` declaration of it reads zeros. Rows keep no order
from frame to frame, so a row number does not stay with an object. The buffer grows to fit up to
65,535 objects and never shrinks, and it does not count toward the 16 MiB of the buffer lanes
scripts make.

#### Object ids

From version `8` on DX10 and DX11, `Texture2D<float> bus_engine_object_id` tells a shader which
game object drew a tagged pixel. Where the mask holds the tag of a world draw of a game object, it
holds that object's `id() + 1` as (`id` + 1) × 2^-24, and elsewhere `0`. `bus_mask.h` declares it
and decodes a pixel with `bus_mask_object_id`:

```hlsl
#include "bus_mask.h"

uint id1 = bus_mask_object_id(int2(I.hpos.xy)); // 0 for none, else the object's id() + 1
```

The ids draw with the world tags, from the same draws and the same depth test, so a pixel's id
belongs to the draw whose tag the mask holds. An item an object holds and an attachment parented
to it draw that object's id. A level root attachment, particles and HUD draws draw none, and where
a tagged particle covers a tagged object the mask holds the particle's tag and this target the
object's id. The target exists while a shader declares `bus_engine_mask_ids` and a mod holds a
live mask bit, under the rules of the mask target, and every tagged world draw of a game object
draws a second time into it. `bus_mask_object_id` reads the lane, so a shader that calls it
declares it, and returns `0` while the lane's `x` is `0`.

A full screen pass reads an object's own values through the id. The mod keeps a buffer lane with a
row per object id and the pass reads the row of the pixel's object:

```lua
shader_bus.set_buffer(token, 65536)
shader_bus.set_array(token, obj:id(), { { 1, 0, 0, 1 } })
```

```hlsl
Buffer<float4> bus_mymarks;

uint id1 = bus_mask_object_id(p);
float4 mark = id1 ? bus_mymarks.Load(id1 - 1) : 0;
```

The rows belong to the mod, which clears an object's row when the object goes.

#### Mask layer

From version `8` on DX10 and DX11, `Texture2D<float> bus_engine_mask_layer` shows tagged objects
through walls. The world draws of a bit on the layer draw again with no depth test, and each pixel
holds their layer bits as bits × 2^-24, `0` where none drew. `bus_mask.h` declares it and decodes a
pixel with `bus_mask_layer_bits`:

```lua
shader_bus.set_mask_layer(token, true)
```

```hlsl
uint mine = bus_mymark;
bool behind = (bus_mask_layer_bits(p) & mine) && !(bus_mask_bits(p) & mine);
```

Where two layer draws overlap, the one drawn last keeps the pixel. An object holding a bit on the
layer stays in the frame when the occlusion test hides it, so it is skinned, updated every frame
out to 60 m as a seen object is, drawn into the G-buffer, where depth rejects its pixels, and then
into the layer. An item on the layer keeps the object holding it in the frame.
The layer still leaves out an object outside the view and one in a part of the level the renderer
does not reach through portals. The target exists while a bit on the layer is live and the mask
target exists. A second viewport frame draws no layer. Particles draw none, and a particles object
with a bit on the layer stays out of the frame when occlusion hides it.

#### Per object rows

A mask says which objects a mod tagged. To give each object its own data, a mod pairs an `obj_`
lane that holds the object's slot with a buffer lane that holds the slot's rows. The mod owns the
slots, and the bus never tells a script that an object went away, so the mod frees its slots
itself. Slot 0 stays unused, since a draw of an object with no value reads the lane's own value,
which is `0`. Buffer lanes need DX10 or DX11.

```lua
local slot_lane, rows_lane = nil, nil
local slot_of, free = {}, {}

function on_game_start()
	if not (shader_bus and shader_bus.set_buffer) then return end
	slot_lane = shader_bus.register("obj_mymod_slot", "My Mod", "row slot of each tinted object")
	rows_lane = shader_bus.register("mymod_rows", "My Mod", "four rows per tinted object")
	if slot_lane and rows_lane and shader_bus.set_buffer(rows_lane, 256 * 4) then
		for slot = 255, 1, -1 do free[#free + 1] = slot end
	end
	RegisterScriptCallback("npc_on_net_destroy", untint)
end

-- gives obj the tint r, g, b from the next frame on
function tint(obj, r, g, b)
	local id = obj:id()
	local slot = slot_of[id] or table.remove(free)
	if not slot then return false end
	slot_of[id] = slot
	shader_bus.set_array(rows_lane, slot * 4, { { r, g, b, 1 } })
	return shader_bus.set_object(slot_lane, obj, slot, 0, 0, 0)
end

-- drops the tint and frees the slot
function untint(obj)
	local slot = slot_of[obj:id()]
	if not slot then return end
	slot_of[obj:id()] = nil
	shader_bus.clear_object(slot_lane, obj)
	shader_bus.set_array(rows_lane, slot * 4, { { 0, 0, 0, 0 } })
	free[#free + 1] = slot
end
```

```hlsl
#if defined(SHADER_BUS) && SHADER_BUS >= 8
uniform float4 bus_obj_mymod_slot;
Buffer<float4> bus_mymod_rows;
#endif

// in the object's pixel shader
#if defined(SHADER_BUS) && SHADER_BUS >= 8
float4 tint = bus_mymod_rows.Load(uint(bus_obj_mymod_slot.x) * 4);
#else
float4 tint = 0;
#endif
```

The slot and the rows of one frame's writes reach the draws together, so a slot freed and given to
another object in the same frame never shows the old rows. `obj_` values reach the draws of a level
root attachment, while a parented attachment draws with the value of the object it hangs on and a
HUD attachment with the actor's.

#### Example

The script registers a mask bit and a `scene` pass, and tags an object by id:

```lua
local mark = nil

function on_game_start()
	if shader_bus and shader_bus.register_mask then
		mark = shader_bus.register_mask("mymod_mark", "My Mod", "objects my mod marks")
	end
	if mark then
		shader_bus.register_pass("mymod_marks", "My Mod", "tints marked objects and the HUD items",
			{ stage = "scene", ps = "mymod_marks", outputs = { "scene" } })
	end
end

-- marks or unmarks the object with this id from the next frame on
function mark_object(id, on)
	local obj = level.object_by_id(id)
	return mark ~= nil and obj ~= nil and shader_bus.set_mask(mark, obj, on)
end
```

The pass shader `shaders\r3\mymod_marks.ps` tints the HUD items blue and the marked objects red:

```hlsl
#include "common.h"
#include "bus_hud_mask.h"

// x is the bit register_mask gave the lane, 0 before it registers and after release_mask
uniform uint4 bus_mymod_mark;

float4 main(p_screen I) : SV_Target
{
	int2 p = int2(I.hpos.xy);
	uint bits = bus_mask_bits(p);
	float3 c = s_image.Load(int3(p, 0)).rgb;
	if (hud_items(p) && (bits & BUS_HUD_LENS) == 0)
		c = lerp(c, float3(0.2, 0.5, 1.0), 0.5);
	else if ((bits & bus_mymod_mark.x) != 0)
		c = lerp(c, float3(1.0, 0.2, 0.1), 0.5);
	return float4(c, 1.0);
}
```

### Readback

A readback copies a small block of what the GPU drew back to Lua. A script arms it on its own lane
with `set_readback` and reads the result with `get_readback` a few frames later. The copy never
stalls the frame, and nothing is copied while no lane is armed.

```lua
local token = shader_bus.register("mymeter", "My Mod", "light at the screen centre")
if token and shader_bus.set_readback then
	shader_bus.set_readback(token, "light", 0.5, 0.5, 16, 16, "mean")
end

-- each update
local ok, r, g, b, _, frame = shader_bus.get_readback("mymeter")
if ok and device().frame - frame < 10 then level_light = 0.2126 * r + 0.7152 * g + 0.0722 * b end
```

| source | what the block holds |
|---|---|
| `light` | the light accumulation before the `lighting` stage and the combine, linear and unbounded |
| `scene` | the combined scene before bloom |
| `backbuffer` | the finished frame before the UI |
| `<target id>` or `<target id>_prev` | a render target of the bus or its history, after the frame's last pass |

`set_readback_rows` reads rows of a buffer lane instead of a block of texels, one result texel a
row in order. Rows past the end of the buffer are left out of the result: a `count` that runs past
the end is cut short, and a `first` past the end is accepted, copies nothing and leaves the result
`none`. A `first` of 65536 or more is refused, as no buffer holds that many rows. A reduce of uint
rows is refused with one log line.

The block is `w` by `h` texels centred on the texel under `x, y` of the target, moved inward where
it would cross an edge. The result holds every texel in rows, left to right and top to bottom,
for `none`, or one texel with the per component mean, minimum or maximum of the block for `mean`,
`min` and `max`. Each texel is `x, y, z, w` as the red, green, blue and alpha of the target, from
`0` to `1` for the `rgba8` and `rgb10a2` targets. A component the target's format lacks reads `0`,
and a missing alpha `1`.

`frame` is the `device().frame` of the frame the block was captured on. A result arrives two to
three frames after its capture, and a script that needs a fresh value compares `frame` with
`device().frame`. A lane armed on every frame reads `device().frame - frame` of `2` at best, and
more while the GPU runs behind. A new `set_readback` call with a different request drops the
result, and the next result comes from the new request. `set_readback(token, "")` disarms the lane,
and only the empty source does.

The `readback` key of a `list` row says where the lane stands:
- `none`, no result yet, after a change of request, a level load or a device reset, or for a lane
  that never armed one
- `ok`, `get_readback` has a result
- `refused`, the source cannot be read back, a multisampled source or a reduce of uint rows, with
  one log line until the lane disarms
- `lost`, the last copy failed, and `get_readback` returns `false` until the next result

Limits:
- 16 armed lanes, at most 32 texels a side, and at most 3 copies in flight per lane. A lane whose
  copies are all still in flight skips the copy that frame.
- DX11 only. Every other renderer refuses `set_readback` and `set_readback_rows`.
- A level load and a device reset drop every result. Frames that skip Present, such as second
  viewport frames, capture nothing.
- The block reads whatever the frame drew there, the player's hands and weapon included.
- A target source copies with the back buffer, and stops landing while the target is released or
  not yet made.
- An older exe has no `set_readback`, so check `shader_bus.set_readback ~= nil` before calling it.

### Buffer lanes

`set_buffer` makes a lane a buffer that a `Buffer<float4>` or `Buffer<uint4>` declaration reads,
and `set_array` or `set_array_uint` fill its rows as on any lane. Row 0 is still the value `set`
writes. A script reads rows a compute pass wrote with `set_readback_rows`.

```lua
local token = shader_bus.register("marks", "My Mod", "marker positions")
if token and shader_bus.set_buffer and shader_bus.set_buffer(token, 10000) then
	shader_bus.set_array(token, 0, { { 1, 2, 3, 4 }, { 5, 6, 7, 8 } })
end
```

Limits:
- 65,536 rows a lane and 16 MiB over every buffer lane, at most 4096 rows in one `set_array` call.
  `engine_mask_objects` grows in powers of two up to 65,536 rows (1 MiB) outside that 16 MiB and
  never adds to it.
- The size only grows. `set_buffer` with fewer rows than the lane has returns `true` and keeps the
  size.
- DX10 and DX11. The DX9 renderers refuse `set_buffer`. Only DX11 runs a `cs` pass that writes one.
- The buffer keeps its rows over a level change and a device reset.
- A frame on which the lane changed uploads the whole buffer once.
- An older exe has no `set_buffer`, so check `shader_bus.set_buffer ~= nil` before calling it.

### Late lanes

`set_late(token, true)` copies the lane a second time after the frame callbacks, so a `set`,
`set_array` or `set_texture` from that frame's callbacks reaches that frame's draws. The lane
turns late or back at the next per-frame update.
- Object values from `set_object` and `clear_object` still arrive the next frame.
- A shared lane refuses `set_late`.
- An older exe has no `set_late`, so check `shader_bus.set_late ~= nil` before calling it.

### Console

- `bus_list` prints every lane with its `owner`, description, current bound value, state and its
  change and write counters, then the twelve legacy lanes and their raw values. A lane with rows
  ends its line with `rows <filled>/<declared>`, and a uint lane prints unsigned values and ends
  with `uint`. `bus_get` and `bus_force` read and take unsigned values on a uint lane too. An
  object lane ends its line with `objects <count>`, and a lane showing a texture with
  `texture '<name>'`.
- `bus_get <id>` prints one lane, then up to fifteen more rows that `set_array` filled. On an
  object lane it prints the lane's value and how many objects have their own, and on a lane
  showing a texture the texture name.
- `bus_force <id> x y z w` pins a lane's row 0 to a value the engine publishes every frame until
  `bus_release`. All four components must be finite or the command is refused. It does not touch or
  reject what the script that registered the lane writes: `get_pending` still reads that script's
  value while the lane is forced, and that value is published again on the very next frame after
  `bus_release`. It also works on a declared lane nobody registered, which is how you can test a
  shader before its mod exists. On an object lane the forced value applies to every object.
- `bus_release <id>` ends the override, so the per-frame update goes back to publishing the pending
  value. It prints `bus_<id> released` for any known lane, forced or not, so read `bus_list` to see
  whether the lane was forced.

Forcing is a debug path for testing a shader against arbitrary values. The Lua write path is
unaffected while a lane is forced, and nothing about `bus_force` should be relied on by shipped
mod behavior.

### Monitor

`imgui_shader_bus.script` adds a live lane monitor to the ImGui overlay. Open the overlay with the
`editor` key (bind it in the key settings) or `rs_editor`, then pick Debug > Shader bus. The menu
opens the panel and the HUD and has the same toggles as the panel toolbar.

- **Panel** is one dockable window in the overlay. A toolbar holds Snapshot, Freeze, HUD, Strip,
  Dump and Copy, a filter that matches the id or `owner` on every tab (`Ctrl+F` focuses it) and a
  `(?)` legend of the colours and icons. Below it the tabs Lanes, Targets, Passes, Compute and Masks
  each show a table on the left and the selected row's detail on the right, and only the open tab
  reads and redraws its rows. Every table scrolls under a frozen header and id column, and its
  columns resize, reorder and hide from the header's right click menu. A click on a header sorts
  by that column, a second click turns the order round, and on the other tabs a third click goes
  back to the default order. A right click on a row offers Pin, Copy id, Copy value (Copy row on
  the other tabs), Force current value and Release.
- **Lanes** lists every lane, filtered by `owner` (`all`, `orphan` for a lane a shader declared
  and nobody registered, `legacy`, `read`, `unread` or one `owner`), by the text filter and by
  `live` (lanes that changed since the monitor opened), and sorted by id, `owner`, last change or
  change count. The pin icon marks a lane for the strip view. A NaN or infinite value shows on a
  red cell. Clicking a lane opens its detail: description, the script that registered it (or the last
  writer of a legacy lane), kind, rows, objects, texture, forced, bound and pending values, the
  session min and max and the counters. A lane with rows lists every row shaders declare,
  scrolling past eight, and `matrix view` groups them by four, with row `j` of matrix `k` labelled
  `m<k> r<j>`. An object lane shows the name, id and value of the object under the crosshair, or
  `no target`. A shared lane shows how many tokens it has, then each token that wrote a value with
  its `owner` and value and a `*` after each component that has the largest value, and on an
  object lane a second list of the tokens that gave the object under the crosshair a value. The
  monitor reads these only while a lane is selected, ten times a second, and for rows only the
  rows the list shows. With the panel focused, `L` switches the HUD between table
  and strip and `F` freezes sampling.
- **Targets**, **Passes**, **Compute** and **Masks** list the render targets, the pixel and
  compute passes and the mask lanes. Targets shows each target's size, format, bytes with a bar
  against the largest target, history and state. Passes and Compute list the passes in the order
  they run, with stage, order, state, shader, the compute group, outputs, inputs, the time between
  the last two runs from `pass_time` (`dt ms`), the frames since the last run (`ran`) and the
  reason a pass failed. A failed state shows on a red cell. Masks shows each mask lane's bit,
  value, the game objects that carry the bit with a bar
  against the largest count, and its state. The object count comes from the engine and counts every
  object that holds the bit, attachments and held items included. The detail of a target, pass or
  mask adds its spec, and a pass's detail its last `pass_time` and how many times it failed. A lane
  the engine refused a write on shows the refusal count and the last reason, and an armed readback
  shows its copies in flight.
- **HUD** draws over the game with the overlay hidden: the table view in the top left corner, at
  most 40 lanes, or the strip view in the top right with no header, showing pinned lanes or every lane when none
  is pinned. It takes
  no input, and it does not draw while the game is paused, which includes an alt-tab unless the
  game is set to stay active in the background.

Columns are `pin`, `lane`, `value` (the four components with zeros dimmed, after a colour swatch
on a float lane), `owner`, `kind`, `age` (since the last change), `chg` (the engine change count,
or changes the monitor saw on a legacy lane), `wr` (writes), `bnd` (frames since a shader last read
the lane, `never` or `stale`). `age` and `bnd` start hidden; right-click a header to show them. A
lane id ending in `!` has had a NaN or infinite value, which shows red. A changed lane's row
flashes amber for half a second. Declared lanes show amber and legacy lanes grey, gathered under a
collapsed `engine legacy (n)` row. `kind` reads `f` or `u` (float or uint, uint values print in hex in
the table and in hex and decimal in the detail), `[n]` for the rows shaders declare (values show
row 0, the detail lists every row), `obj n` for the objects with their own value on an `obj_`
lane (values show the lane's own value), `tex` for a lane showing a texture (its name fills the
value cell), `eng` for an engine lane, `con` for a console lane (`bus_cvar_<name>`), `frc` while
`bus_force` overrides the lane, and `mix` when shaders declare the lane as more than one kind or as
another kind than the last write stored. `cvar` marks a legacy lane.

Snapshot stores every value and then lists only the lanes that moved since, with deltas. Snapshot
again to leave that view. Freeze stops sampling and keeps the values on screen. Dump prints every
lane to the log, Copy puts the same lines on the clipboard. The counts under the toolbar filter
the lists when clicked: live lanes, orphans, and failed passes and targets. Force and Release in
the detail run `bus_force` and `bus_release` on the selected lane, target or pass, with four
numbers for a float lane and four whole numbers up to 4294967295 for a uint lane.

The filter, the lane sort and its direction, `owner` choice, `live`, the open tab, the HUD view and
the pins persist in `axr_options.ltx` under `[imgui_shader_bus]`. Window position, size, column
widths, order and visibility persist in ImGui's own `imgui.ini`. Nothing is sampled while every
window is closed. The Lanes tab and the HUD sample each lane once per frame, the other tabs sample
the selected lane and, on Masks, the mask lanes. Open tables redraw their text ten times a second.

Past 64 lanes the monitor spreads its reads over frames. Each frame it reads the lanes of the rows
it shows, the selected lane and 64 more in turn, and the panel draws only the rows in its scroll
area. One pass over a full bus of 65,535 lanes takes about 1,024 frames, about 17 seconds at 60
frames a second. A lane off screen shows what its last read found, values, counters and session
min and max alike, and the filters, Dump and Copy work from the same data. The filters apply ten
times a second. The last change and change count sorts run at once when picked or when the lane
list changes, and otherwise about every 2 seconds between two reads of the lane list, so a row
keeps its place in between.

A lane counts as live when a read finds a different value. Past 64 lanes a bus lane also counts as
live when the engine counted a change between two reads, since a read spread over frames can miss
one. A legacy lane has no engine counter. A snapshot takes the value of every lane read that
frame, and every other lane takes its snapshot value at its next read. A lane counts as moved when
its value differs from the snapshot, and past 64 lanes a bus lane counts as moved when the engine
changed it after the snapshot.

On the multithreaded exe the HUD does not draw while `mt_level_call` is `1`, since that setting
runs the HUD's frame callback beside the renderer, and the panel says so. An exe without
`shader_bus` logs one line and leaves the menu entries disabled. On an older bus the monitor shows
what that exe offers: kinds, rows, objects, textures and forced values appear only where `list()` has
their fields and the matching calls exist, Force shows only where `get_pending` exists, the value
under the crosshair only where `get_object` exists, and the writers of a shared lane only where
`writers` exists.

---

## Render targets and passes

From version `8` on DX10 and DX11, a script can register its own render targets and full screen
passes. A target is a texture a shader reads as `Texture2D bus_<id>`, with an optional copy of
its last frame. A pass is a pixel shader, or on DX11 a compute shader, that the engine runs at
one point of the frame into targets or into the scene. Targets and passes are lanes of their
`owner`, so `list()` shows them and the same id rules apply.

### Targets

```lua
local target = shader_bus.register_target("frost", "My Mod", "frost mask", {
	size = "half", format = "r16f", history = true, clear = 0 })
```

| key | value |
|---|---|
| `size` | `"screen"` (the default), `"half"`, `"quarter"` or `{ w, h }`, each side a whole number from `1` to `4096` |
| `format` | `rgba8` (the default), `rgb10a2`, `r11g11b10f`, `rg16f`, `r16f`, `r32f`, `rgba16f` or `rgba32f` |
| `history` | `true` keeps last frame's copy, default `false` |
| `mips` | `true` gives the target and its history a full mip chain, filled from the top level after every pass that writes the target, default `false` |
| `clear` | a number for all four channels or a table of up to four numbers, default `0` |

A spec field that is not valid refuses the call with a log line that cites the field. A target
id is a lane id of at most 27 characters, so `<id>_prev` fits.

A shader reads the target as `Texture2D bus_<id>` and its history as `Texture2D bus_<id>_prev`,
bound the way any `bus_` texture is (see The shader side). The target clears to `clear`, history
included, when it is created, when its spec changes, when the screen size changes under a
`"screen"`, `"half"` or `"quarter"` size, on a device reset and on a level change. Registering
again with the same spec changes nothing and returns the same token.

The history copy runs once a frame, just before the first pass that writes the target, and never
on a second viewport frame. A target no pass writes never copies. A pass that reads a target
before its writer has run in the same frame sees the last frame's value.

A target with `mips` reads its smaller levels through `SampleLevel`, or `Sample` with `smp_linear`,
which filters between levels. The engine fills them after each pass that writes the target and
after each clear, so a pass that writes only part of the target still gets levels made from the
whole of it. A readback of the target reads the top level.

History is a plain copy, so a texel of `bus_<id>_prev` holds what that spot of the screen held last
frame, and a history read at the pass's own texture coordinate trails while the camera turns.
`shaders\r3\bus_reproject.h` declares the engine's `m_vp_prev`, last frame's view projection, and
gives `bus_reproject(view_pos, tc)`, which puts in `tc` where a view space point of this frame sat on
last frame's screen and returns `false` where that was behind the camera or off the screen. Include
it after `common.h`, which gives `m_inv_V`. It leaves out its own `m_vp_prev` where an earlier
include declared it under `SSFX_MV_LOADED`, and a shader that declares `m_vp_prev` some other way
does not compile with it. The view space position comes from the G-buffer at
the `gbuffer`, `lighting`, `scene` and `post` stages, with `s_position = "$user$position"` in the
pass's `inputs`. It takes out only the camera's motion, so a moving object or the player's hands
land where they were not.

```hlsl
#include "common.h"
#include "bus_reproject.h"

Texture2D bus_trail_prev;

float4 main(p_screen I) : SV_Target
{
	gbuffer_data gbd = gbuffer_load_data(I.tc0, I.hpos.xy);
	float2 tc;
	float4 last = bus_reproject(gbd.P, tc) ? bus_trail_prev.SampleLevel(smp_rtlinear, tc, 0) : 0;
	return last * 0.9;
}
```

`release_target` frees the target and its history. Live targets together stay within the memory
budget under Limits, sized at the resolution in effect when the target registers. A call that
would pass it is refused with a log line, and a call that does not grow the target always passes.

### Passes

```lua
shader_bus.register_pass("frost_grow", "My Mod", "grows and melts the frost", {
	stage = "frame", ps = "myfrost", outputs = { "frost" } })
```

| key | value |
|---|---|
| `stage` | required, one of the stages below |
| `order` | a whole number, default `0` |
| `ps` or `cs` | the shader file name without folder or extension, exactly one of them. The file is `shaders\r3\<name>.ps` or `.cs` |
| `outputs` | required, 1 to 4 ids of the owner's targets that share one size, or one of the single outputs listed below the table |
| `inputs` | a table from an HLSL texture variable to a texture or render target name, at most 16. A `Texture2D bus_<id>` the shader declares needs no entry |
| `blend` | `none` (the default), `add` or `alpha`, how a pixel pass combines with what its outputs hold |
| `rect` | on a pixel pass, `{ x, y, w, h }` in the pass's texture coordinates, 0 to 1 from the top left. The pass shades only that part of its outputs and leaves the rest as it was, default the whole output. A different `rect` is a changed spec and restarts the pass |
| `group` | required on a `cs` pass, the thread group size `{ x, y }`, each side from `1` to `1024` |
| `grid` | on a `cs` pass into a buffer lane, the dispatch grid as a target `size`, `"screen"`, `"half"`, `"quarter"` or `{ w, h }`, see Compute |
| `indirect` | on a `cs` pass, `{ lane, row }`, a uint buffer lane of its owner whose row gives the dispatch's group counts, see Compute |
| `svp` | `true` also runs the pass on a second viewport frame, default `false` |
| `runs` | how many times the pass runs in a row each frame, `1` to `16`, default `1`, see Repeat |
| `pingpong` | on a pixel pass with `runs` above `1`, a target of its owner with the size, format and `mips` of its one output, see Repeat |

The single outputs are:
- `"scene"` on a `scene` or `post` pass
- `"albedo"` on a `gbuffer` pass, which draws into the G-buffer albedo with the pass's `blend` and
  leaves its alpha, the gloss, as it was (`none` replaces the colour, `add` and `alpha` combine with it)
- `"light"` on a `lighting` pass with blend `add` or `alpha`, which blends into the light
  accumulation (see Stages)
- on a `cs` pass, one buffer lane of the owner

A pass shader compiles with the defines the renderer gives its stock shaders, `SHADER_BUS`,
`USE_MSAA` and `MSAA_SAMPLES` among them. A pixel pass draws over the stock `stub_screen_space`
vertex shader, so it takes `p_screen` and samples with `I.tc0`. With `rect` the engine draws the
same quad cut to the rectangle, so `I.tc0` and `SV_Position` read as they would without it, and a pixel
is shaded when its centre is inside. For exact edges give `x` and `w` in steps of one over the
output's width, and `y` and `h` in steps of one over its height. The first output is
`SV_Target0`. Textures bind to `t0` to `t15`. The shader of the pass above is
`shaders\r3\myfrost.ps`:

```hlsl
#include "common.h"

uniform float4 pass_time;
uniform float4 bus_frost_cold;
Texture2D bus_frost_prev;

// frost grows from the screen edge while it is cold and melts when it is not
float4 main(p_screen I) : SV_Target
{
	float old = bus_frost_prev.Sample(smp_rtlinear, I.tc0).x;
	float edge = saturate(length(I.tc0 - 0.5) * 2.0);
	float rate = lerp(-0.2, 0.1, saturate(bus_frost_cold.x));
	return float4(saturate(old + rate * edge * pass_time.x), 0.0, 0.0, 0.0);
}
```

`frost_cold` is a plain lane the same mod registers and sets from its own cold value.

A pass starts enabled. `enable_pass(token, false)` stops it, `enable_pass(token, true)` runs it
again, and `release_pass` removes it. Registering a pass again with the same spec changes
nothing, and with a changed spec it restarts like a new pass.

A pass that cannot run is `failed` in `list()`, with the reason in `reason` and one `!` line in
the log, and it draws nothing. The reasons are:
- its shader file is missing, or a texture variable sits past `t15` or a UAV past `u7`
- an output is not a live target of the pass's `owner`, or the outputs differ in size
- it reads its own output, through `inputs` or by declaring `Texture2D bus_<output>`
- it reads its `pingpong` target, or the scene it writes
- a constant the pass binds as a texture is not a texture, or an `smp_` constant is not a sampler
- a `cs` pass writes `"scene"`, or an `svp` pass writes a history target
- a `lighting` pass writes `"light"` under `-r4xx`, which emulates a GPU that cannot blend 16-bit float targets
- a `cs` shader does not declare each output as a `RWTexture2D` or `RWBuffer` called `bus_<id>`
- a `cs` pass declares its buffer lane as anything but `RWBuffer<float4>` for a float lane, or
  `RWBuffer<uint4>` or `RWBuffer<uint>` for a uint lane
- a `cs` pass with a `grid` writes targets instead of a buffer lane, or a buffer lane of another
  `owner`
- `indirect` points at a lane that a `cs` pass of the `owner`, this one included, writes as float4
  rows, the counts need uint4
- a pass with `pingpong` writes the scene, its `pingpong` is not a target of the `owner`, it differs
  from the output in size, format or mips, or it is a history target under `svp`
- the compute shader could not be created
- `s_image` is misused, see `s_image`

### Stages

| stage | runs |
|---|---|
| `frame` | before the frame's first draw |
| `gbuffer` | after the G-buffer and the rain |
| `lighting` | at the start of the combine, after the lights and volumetrics |
| `scene` | after the MSAA resolve, before bloom |
| `post` | before post processing |

There is no stage for the world. Passes in a stage run by `order`, then `owner`, then id. A
second viewport frame runs only the passes registered with `svp = true`.

A `gbuffer` pass that writes `albedo` changes the colour every light and the combine read, the
player's hands and weapon included, since they are in the G-buffer too.

A `lighting` pass that writes `light` changes the light the stock combine multiplies by the
albedo, which a shader pack may define differently. Red, green and blue hold the light and alpha
the specular. With `add` the pass adds to all four, and with `alpha` it blends red, green and blue
and leaves the specular as it was. The `light` readback reads the light before this stage, so it
never shows what a pass added.

### s_image

A pass at `scene` or `post` that declares `Texture2D s_image` gets the scene as it stood before the
pass, so a pass that writes `"scene"` can read what it replaces. The engine binds `s_image` itself
there, and a pass that lists it in `inputs` fails. At `post` the copy is the engine's scratch
texture. At `scene` it is `$user$bus_engine_scene_copy`, which exists while a `scene` pass reads
`s_image`. A `post` pass that reads `s_image` fails when the scene and its copy differ in
format.

At the other stages there is no scene, `s_image` is an ordinary input name, and a shader that
declares it without listing it fails. A pass that writes `"scene"` cannot also list the scene's
own texture in `inputs`. At `scene` the output `"scene"` is the combined scene, and at `post` it
is the image post processing reads.

### pass_time

A pixel or compute shader declares `uniform float4 pass_time;`:

| component | value |
|---|---|
| `x` | seconds since the pass last ran |
| `y` | frames since the pass last ran |
| `z` | how many times the pass ran since its outputs were last cleared, not counting this run |
| `w` | `1` on the first run after the outputs were cleared, else `0` |

On a pass's first run, on the first run after an enable and on the first run after its outputs
cleared, `x` is the frame time and `y` is `1`. A pass that writes a buffer lane counts the buffer
being created again as a clear.

A repeated pass counts every run: `z` moves each run, `w` is `1` only on the first run after a
clear, and later runs in the frame read `x` and `y` as `0`. `get_pass_time` answers with the first
run of the frame.

`shader_bus.get_pass_time(id)` returns `ok, x, y, z, w, frames`, the `pass_time` of the pass's
last run and the frames since it ran, or `false` for a pass that has not run since it last
registered. It answers as of the start of the frame, the same from every callback and on the
multithreaded exe, so a pass that runs every frame reads `1`.

### Repeat

With `runs = n` the engine runs the pass `n` times in a row each frame. A `cs` pass, and a pixel
pass without `pingpong`, runs in place, and a dispatch sees what the one before wrote. A pixel pass
with `pingpong` reads the run before through `Texture2D bus_<id>` of its one output, which only
such a pass may declare, and the engine alternates the output and the `pingpong` target so the
last run lands in the output. The first run reads what the output held before the pass, and
afterwards the `pingpong` target holds the run before the last. `bus_<id>_prev` stays last frame's
for both. A `pingpong` pass cannot read `bus_<pingpong>`, blend, or write the scene, albedo or
light, and an odd `n` costs one copy of the output.

### Compute

A `cs` pass writes its outputs through `RWTexture2D` variables called `bus_<id>` at `u0` to `u7`,
and the dispatch covers the first output in groups of `group`. The pass sees last frame through
`Texture2D bus_<id>_prev`. A target becomes a UAV target while a `cs` pass of its owner
writes it, even a disabled one, and it is created again, and so cleared, when that changes.

A `cs` pass can instead write one buffer lane of its owner, which `set_buffer` made, through
`RWBuffer<float4> bus_<id>` for a float lane or `RWBuffer<uint4> bus_<id>` for a uint lane.
The buffer lane is then the pass's only output, `group` is `{ x, 1 }`, and the dispatch runs one
thread per row. With `grid`, sized like a target's `size`, the dispatch covers that grid in groups
of `group` instead, and `group` may have any `y`. Declared as `RWBuffer<uint> bus_<id>`, a uint
lane holds four elements a row, row `r` component `c` at `4 * r + c`, and takes `InterlockedAdd`
and the other atomics, which `RWBuffer<uint4>` does not. While the pass lives `set`, `set_uint`,
`set_array` and `set_array_uint` refuse the lane and the engine stops uploading it, and a buffer
starts at zero each time it is created.

With `indirect = { lane, row }` a `cs` pass takes its group counts from `x`, `y` and `z` of row
`row` (0-based) of a uint buffer lane of its owner, read on the GPU when the pass runs, so a `cs`
pass earlier in the frame can write them. The lane cannot also be an output of the pass and cannot
go with `grid`, a buffer lane of another owner refuses the pass, and a buffer lane output then
takes any `group` `y`. The pass fails when a `cs` pass writes the lane as float rows, and skips
while the lane is not a uint buffer lane of its owner or has no buffer or no such row. A lane
becomes an args buffer while a pass of its owner dispatches from it, even a disabled one, and its
buffer is created again when that changes, which clears a lane a `cs` pass writes and uploads the
rows of any other again. The engine cannot check counts the GPU writes, so keep each at 65535 or
below.

### Limits

- 16 live targets, 32 live passes, 4 outputs and 16 inputs a pass. A released target or pass
  stops counting.
- A target side from `1` to `4096`, a compute group side from `1` to `1024`.
- 1024 MB over all live targets, a history target counted twice and a target with `mips` by its
  whole chain, about a third more for a screen-shaped target.
- Texture slots `t0` to `t15` and UAV slots `u0` to `u7`.
- A target id of at most 27 characters.
- A pass runs at most 16 times a frame.

### Renderers

| renderer | what works |
|---|---|
| DX11 | everything above |
| DX10 | targets, history, pixel passes and buffer lanes a shader reads. A `cs` pass and `set_readback` are refused |
| DX9 | `register_target` and `register_pass` return `nil` with one `!` line |

A script can ask before it registers. `gpu_lanes()` is `true` on DX11, `targets()` on DX10 and
DX11, and `masks()` on DX10 and DX11 under the conditions in Masks.

---

## Semantics

A lane that is not shared has at most one `owner` for the life of the process. Once
`register` succeeds for an id, only the token it returned can call `set` on that lane; a different
script asking for the same id gets a fatal with both `owner` strings and both script paths, unless it
used `try_register`, in which case it gets `nil` and a log line instead.

A shared lane can have many writers, each with its own token, and the per-frame update binds the largest value
of their tokens (see Shared lanes).

`set` stores a pending value. The engine copies every lane's pending value (or its forced value,
if forced) into its bound value once per frame, before the frame callbacks run, so a four-float
write is never half visible to a shader mid-write. This copy runs every frame the game draws,
the main menu included, and stops while a level loads, so a value set during a load reaches
shaders on the first frame after it. The copy is also why `get` trails `set` by one frame and
why `get_pending` and `get` can legitimately disagree. A late lane is copied again after the frame
callbacks (see Late lanes).

Lanes are never removed once declared or registered, so a token stays valid for the rest of the
process, and a lane you query early keeps existing even if nobody ever claims it. The bus has room for
at most 65,535 lanes. Past that a new id is refused, `register` and `try_register` return `nil`, a
new `bus_` shader constant binds to nothing, and the first refusal logs a line.

The twelve legacy commands, `shader_param_1..8` and `s3ds_param_1..4`, still work exactly as
before. The first write to any of them in a session logs a nudge with the writer, once per
command and writer, so a lane four mods still write prints four lines rather than one. `list()`
reports the last writer of a legacy lane as `writer`: a Lua write reads back as
`<script>:<line>`, a write typed at the console or replayed after startup (for example by
`cfg_load`) reads `console`, and a write replayed out of the config the engine booted with reads
that file's name.

---

## Logging

Every rejection, a bad id, a missing or oversized `owner`, an oversized description, a collision
with a different `owner`, and every successful registration print through the normal log every time
they happen, as does the once per command and writer nudge for the legacy console lanes. A `bus_`
shader constant the bus cannot bind prints once per name with the reason. A refused write prints
once per lane, while `refusals` in `list()` counts every one and `last_refusal` keeps the reason of
the last. When a level finishes loading, a lane that a shader declared and nobody registered is warned as
`declared by a shader and registered by nobody`, which is how a missing mod dependency shows up
in a plain log with no console commands needed. A lane `bus_force` still overrides is warned the same
way with its forced value, so a forgotten `bus_force` shows up after a reload. The full dump of every lane
with its `owner` and value at level load prints only when the game runs with `-dbg`, where a forced
lane shows `forced`, and `bus_list` prints the same table on demand at any time.

---

## Migration recipe

One rename in the shader, one call in the script.

Shader:

```hlsl
-uniform float4 shader_param_5;
+uniform float4 bus_mymod;

-... shader_param_5.w ...
+... bus_mymod.w ...
```

Script:

```lua
-local con = get_console()
-con:execute(string.format("shader_param_5 %.3f,%.3f,%.3f,%.3f", x, y, z, w))
+local token = shader_bus.register("mymod", "My Mod", "what it is for")
+if token then shader_bus.set(token, x, y, z, w) end
```

Register the lane in `on_game_start`, then replace every place that used to `execute` the old
console command with `shader_bus.set(token, ...)` through the token. Keep the component layout
you already had and the visual result does not change.

Keep the old console write only if the mod must still run on an exe without the bus, guarded by a
feature test:

```lua
if shader_bus then
    if token then shader_bus.set(token, x, y, z, w) end
else
    con:execute(string.format("shader_param_5 %.3f,%.3f,%.3f,%.3f", x, y, z, w))
end
```

If your script currently reads, modifies and rewrites a `shader_param` slot to avoid stomping a
neighbour's value, delete that logic once you move to a lane you `register`. Nobody else can write it.

---

## A minimal example

Script:

```lua
local token = nil

function on_game_start()
    if shader_bus then
        token = shader_bus.register("mymod_tint", "My Mod", "a debug tint color")
    end
end

function actor_on_update()
    if token then
        shader_bus.set(token, 1.0, 0.6, 0.6, 1.0)
    end
end
```

Shader:

```hlsl
uniform float4 bus_mymod_tint;

// ...
float3 tinted = base_color.rgb * bus_mymod_tint.rgb;
```

On an exe without the bus, `shader_bus` is `nil`, so the script never registers or sets anything,
and `bus_mymod_tint` is an unbound constant in the shader, reading whatever else happens to sit in
the constant buffer at that offset. Ship the two halves together, and say in your mod description
that it needs an exe with the shader bus, or wrap the read in a `SHADER_BUS` test, which only
exes from version `7` on pass.

---

## Rules

- **Ids are lowercase.** `^[a-z][a-z0-9_]{0,31}$`. Anything else is rejected with a log line and a
  `nil` token. The console lowercases its own arguments before matching, so a lowercase-only id
  removes a whole class of mismatch.
- **All zero is the default.** Design your encoding so a shader reading zero behaves like an
  unmodded install. That is what an unregistered lane, and an exe without the bus, hand you.
- **One lane is one float4, or rows of float4 on DX10 and DX11.** If you need more values, declare
  an array or register more ids rather than packing two numbers into the decimal digits of one
  float.
- **Floats, or uints on DX10 and DX11.** Booleans and small integers go in as floats, bit masks
  and large ids as uints. Strings never.
- **The `owner` string is yours, up to 64 characters,** and the description up to 256. Use the mod's
  display name for `owner`, since it appears in `bus_list` and in the collision fatal.
- **Re-registering with the same `owner` is free.** Scripts re-run on every level load, and
  `register` returns the existing token.
- **A different `owner` on the same id is fatal**, by design, so a collision is loud instead of one
  mod silently overwriting another's value. Use `try_register` for a lane more than one mod might
  cooperatively want; a refused `try_register` returns `nil` and logs which `owner` kept it. Use
  `register_shared` for a lane more than one mod writes at once.
- **There is no compatibility check for old exes.** `if shader_bus then` is a complete detection on
  its own, and the all-zero default above is a convention this bus provides, not a guarantee on an
  exe that predates it. The only shader side test is `SHADER_BUS`, from version `7` on.
- **A failed pass draws nothing.** It logs one `!` line with its reason, shows `pass_state`
  `failed` and the reason in `list()`.
- **A target or history lane takes no texture or buffer.** `set_texture` and `set_buffer` refuse
  it, so one id never holds two GPU resources.
- **`bus_list` shows a target or pass as a plain lane.** `list()` has its spec and state.
- **The mask target needs MSAA off.** With MSAA on the engine logs one `!` line, keeps no target,
  and the mask reads `0`.
- **A mask lane holds only its bit.** `set`, `set_uint`, the row calls, `set_texture`,
  `set_readback`, `set_readback_rows` and `set_buffer` refuse it, so it reads the bit `register_mask`
  wrote, or `0` after `release_mask`.

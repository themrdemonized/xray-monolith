# Shader Bus

---

## Intro

The shader bus replaces the eight anonymous `shader_param_1..8` and `s3ds_param_1..4` float4
constants with lanes that each have an id and an `owner`. Any script can register a lane by name, any script can read
any lane, and only the script that registered a lane can write it. The engine needs no advance
list of names, so a mod can claim a lane without anything being patched into the exe for it.

Requires a modded exe with the bus. `shader_bus.version()` returns `5`. A script that wants
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
| `shader_bus.register(id, owner, description)` | a token, or a hard fatal, with both `owner` strings and both script paths, if the id is already registered with a different `owner`, or with the lane's `owner` strings if the lane is shared and is not `obj_hotness`. A bad id, a missing `owner`, an `owner` over 64 characters, a description over 256 characters, an `engine_` or `cvar_` id or a full bus returns `nil` with a log line (once per game for a full bus), so check the token before using it |
| `shader_bus.try_register(id, owner, description)` | a token, or `nil` if the id is already registered with a different `owner` or under the same rules as `register`. A shared lane other than `obj_hotness` returns `nil` with a log line |
| `shader_bus.register_shared(id, owner, description)` | the token of this `owner` on a shared lane, or `nil` with a log line if a script took the lane with `register` or `try_register`, or under the same rules as `register`. See Shared lanes |
| `shader_bus.set(token, x, y, z, w)` | `true` if the token is valid and all four values are finite. A NaN or infinite value is dropped, the lane keeps its last value, and the first refusal on each lane logs a line |
| `shader_bus.set_array(token, first, rows)` | `true` if the token is valid, `first` is a whole number from `0`, `rows` is a non-empty array of rows `{x, y, z, w}` (a missing component reads `0`), every value is finite and the last row is below row 4096. Writes `rows[1]` to row `first` (0-based, the HLSL element index) onward and leaves other rows as they were. Row 0 is the value `set` writes, so `set` and `set_array` from row 0 overwrite each other. The first refusal on each lane logs a line |
| `shader_bus.set_uint(token, a, b, c, d)` | `true` if the token is valid and all four values are whole numbers from `0` to `4294967295`. The lane becomes a uint lane. A value out of range, negative or fractional is dropped and the first refusal on each lane logs a line |
| `shader_bus.set_array_uint(token, first, rows)` | `set_array` with unsigned rows, under the same value rule as `set_uint` |
| `shader_bus.set_object(token, obj, x, y, z, w)` | `true` if the token is valid, the lane id starts `obj_`, `obj` is a game object and all four values are finite. Draws of that object read the value from the next frame on, until `clear_object` or the object goes away. The first refusal on each lane logs a line |
| `shader_bus.clear_object(token, obj)` | `true` under the same token, lane and object rule. Draws of that object read the lane's value again from the next frame on |
| `shader_bus.get(id)` | `ok, x, y, z, w`, the bound value, one frame behind the latest `set` |
| `shader_bus.get_uint(id)` | `ok, a, b, c, d`, the bound value read as four unsigned integers |
| `shader_bus.get_row(id, i)` | `ok, x, y, z, w`, bound row `i` (0-based), `0, 0, 0, 0` for a row never set, `false` past the rows a shader declared. Row 0 always answers, like `get`. On a uint lane the values are the raw unsigned integers |
| `shader_bus.get_object(id, obj)` | `ok, x, y, z, w`, what draws of `obj` read, its own value or the lane's, `false` with zeros for an unknown lane or a `nil` object |
| `shader_bus.get_pending(id)` | `ok, x, y, z, w`, the value last written through the token and not yet copied into the bound value, on a shared lane the largest value of its tokens. On a uint lane the values are the raw unsigned integers |
| `shader_bus.writers(id)` | rows `{owner, x, y, z, w}`, one for each token of a shared lane that has called `set`, with that token's value, or an empty list for a lane that is not shared or an unknown id |
| `shader_bus.writers(id, obj)` | the same rows for the tokens that gave `obj` its own value, or an empty list for a `nil` object |
| `shader_bus.has(id)` | `true` for a lane a script registered or a shader declared, so a declared lane nobody registered answers `true` while `owner_of` gives `nil` |
| `shader_bus.describe(id)` | the description string, or `nil` for an id with no registered lane, the description of the first `owner` on a shared lane |
| `shader_bus.owner_of(id)` | the `owner` string, or `nil` for an id with no registered lane, the first `owner` on a shared lane |
| `shader_bus.stats(id)` | `ok, changes, last_change_frame, bound_frame, writes` |
| `shader_bus.list()` | rows for the registered lanes, then the twelve legacy lanes |
| `shader_bus.list(true)` | the same, plus a row for every lane a shader declared that nobody registered |
| `shader_bus.version()` | `5` |

Each `list` row has `id`, `owner`, `description`, `state` (`registered`, `declared` or
`legacy`), `source` (the script path that called `register`, empty for a declared or legacy row),
on a registered or declared row `forced` (`true` while `bus_force` overrides the lane), `rows` (the
rows `set_array` has filled, `0` for a lane only `set` writes), `declared_rows` (the most rows
any shader declares, `0` before a shader declares the lane), `kind` (`float` or `uint`, what the
last write stored, or what a shader declared before any write), `declared_kinds` (an array of
every kind a shader declared the lane as), `objects` (how many objects have their own value on an
object lane), `shared` (`true` for a shared lane), `writers` (how many tokens a shared lane has
handed out, `1` for a registered lane that is not shared) and, on a legacy row only,
`writer`. Registered rows come first (declared rows too, with the
`true` argument), then the twelve legacy rows, and a legacy row only appears if that console
command still exists on the running exe.

`writes` in `stats` counts every `set` call made through the lane's token. `changes` counts the
frames on which the published value moved, so a lane rewritten every frame with an
unchanging value shows many writes and no changes. `last_change_frame` is the last frame the bound
value changed, `bound_frame` is the last frame a shader pass read the lane, so a `bound_frame`
far behind the current frame means the lane is bound to a shader nothing is drawing. `stats`
answers for a declared-but-unregistered lane too; it comes back `false` with all zeros only when
no lane by that id exists at all.

Reads and listing are open to every script. Only the token returned by `register`,
`try_register` or `register_shared` can write.

### Engine lanes

The engine registers two lanes itself, with the `owner` string `engine`, and fills them every frame once a
shader has declared them, so they cost nothing until one does. The declaration lasts until the
game exits. Read them in a shader like any lane.

| lane | value |
|---|---|
| `engine_sun_los` | `x` is `1` when nothing blocks a ray of 500 m from the camera toward the sun, else `0` |
| `engine_sky_open` | `x` is how open the sky is over the view entity, from `0` to `1`, the value `level.rain_hemi()` returns |

```hlsl
uniform float4 bus_engine_sun_los;
```

Ids starting `engine_` belong to the engine. `register` and `try_register` refuse them with a log
line and `nil`, never a fatal. A script checks for an engine lane with
`shader_bus.owner_of("engine_sun_los") == "engine"`.

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

Rows and uint values have no largest value, so `set_array`, `set_uint` and `set_array_uint` return
`false` on a shared lane, and the first refusal on each lane logs a line.

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

### Console

- `bus_list` prints every lane with its `owner`, description, current bound value, state and its
  change and write counters, then the twelve legacy lanes and their raw values. A lane with rows
  ends its line with `rows <filled>/<declared>`, and a uint lane prints unsigned values and ends
  with `uint`. `bus_get` and `bus_force` read and take unsigned values on a uint lane too. An
  object lane ends its line with `objects <count>`.
- `bus_get <id>` prints one lane, then up to fifteen more rows that `set_array` filled. On an
  object lane it prints the lane's value and how many objects have their own.
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
write is never half visible to a shader mid-write. This copy runs every frame the device pumps,
so it is live in the main menu and while loading, not only during gameplay. It is also why `get`
trails `set` by one frame and why `get_pending` and `get` can legitimately disagree.

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
shader constant the bus cannot bind prints once per name with the reason. When a
level finishes loading, a lane that a shader declared and nobody registered is warned as
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
that it needs an exe with the shader bus.

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
  exe that predates it.

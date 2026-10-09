# Shader Bus

---

## Intro

The shader bus replaces the eight anonymous `shader_param_1..8` and `s3ds_param_1..4` float4
constants with lanes that each have an id and an `owner`. Any script can register a lane by name, any script can read
any lane, and only the script that registered a lane can write it. The engine needs no advance
list of names, so a mod can claim a lane without anything being patched into the exe for it.

Requires a modded exe with the bus. `shader_bus.version()` returns `2`. A script that wants
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

A lane binds a `float`, `float2`, `float3` or `float4`. A `bus_` constant declared as an `int`, a
`bool`, an array or a matrix binds to nothing, the engine never writes it, and the log gets one
line with the constant's name and the reason. A pass whose shaders declare one lane with different types, say
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
| `shader_bus.register(id, owner, description)` | a token, or a hard fatal, with both `owner` strings and both script paths, if the id is already registered with a different `owner`. A bad id, a missing `owner`, an `owner` over 64 characters, a description over 256 characters or a full bus returns `nil` with a log line (once per game for a full bus), so check the token before using it |
| `shader_bus.try_register(id, owner, description)` | a token, or `nil` if the id is already registered with a different `owner` or under the same rules as `register` |
| `shader_bus.set(token, x, y, z, w)` | `true` if the token is valid and all four values are finite. A NaN or infinite value is dropped, the lane keeps its last value, and the first refusal on each lane logs a line |
| `shader_bus.get(id)` | `ok, x, y, z, w`, the bound value, one frame behind the latest `set` |
| `shader_bus.get_pending(id)` | `ok, x, y, z, w`, the value last written through the token and not yet copied into the bound value |
| `shader_bus.has(id)` | `true` for a lane a script registered or a shader declared, so a declared lane nobody registered answers `true` while `owner_of` gives `nil` |
| `shader_bus.describe(id)` | the description string, or `nil` for an id with no registered lane |
| `shader_bus.owner_of(id)` | the `owner` string, or `nil` for an id with no registered lane |
| `shader_bus.stats(id)` | `ok, changes, last_change_frame, bound_frame, writes` |
| `shader_bus.list()` | rows for the registered lanes, then the twelve legacy lanes |
| `shader_bus.list(true)` | the same, plus a row for every lane a shader declared that nobody registered |
| `shader_bus.version()` | `2` |

Each `list` row has `id`, `owner`, `description`, `state` (`registered`, `declared` or
`legacy`), `source` (the script path that called `register`, empty for a declared or legacy row),
on a registered or declared row `forced` (`true` while `bus_force` overrides the lane) and, on a
legacy row only, `writer`. Registered rows come first (declared rows too, with the
`true` argument), then the twelve legacy rows, and a legacy row only appears if that console
command still exists on the running exe.

`writes` in `stats` counts every `set` call made through the lane's token. `changes` counts the
frames on which the published value moved, so a lane rewritten every frame with an
unchanging value shows many writes and no changes. `last_change_frame` is the last frame the bound
value changed, `bound_frame` is the last frame a shader pass read the lane, so a `bound_frame`
far behind the current frame means the lane is bound to a shader nothing is drawing. `stats`
answers for a declared-but-unregistered lane too; it comes back `false` with all zeros only when
no lane by that id exists at all.

Reads and listing are open to every script. Only the token returned by `register` or
`try_register` can write.

### Console

- `bus_list` prints every lane with its `owner`, description, current bound value, state and its
  change and write counters, then the twelve legacy lanes and their raw values.
- `bus_get <id>` prints one lane.
- `bus_force <id> x y z w` pins a lane to a value the engine publishes every frame until
  `bus_release`. All four components must be finite or the command is refused. It does not touch or
  reject what the script that registered the lane writes: `get_pending` still reads that script's
  value while the lane is forced, and that value is published again on the very next frame after
  `bus_release`. It also works on a declared lane nobody registered, which is how you can test a
  shader before its mod exists.
- `bus_release <id>` ends the override, so the per-frame update goes back to publishing the pending
  value. It prints `bus_<id> released` for any known lane, forced or not, so read `bus_list` to see
  whether the lane was forced.

Forcing is a debug path for testing a shader against arbitrary values. The Lua write path is
unaffected while a lane is forced, and nothing about `bus_force` should be relied on by shipped
mod behavior.

---

## Semantics

One lane has at most one `owner` for the life of the process. Once `register` succeeds for
an id, only the token it returned can call `set` on that lane; a different script asking for the
same id gets a fatal with both `owner` strings and both script paths, unless it used `try_register`, in
which case it gets `nil` and a log line instead.

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

If your script currently reads, modifies and rewrites a shared lane to avoid stomping a
neighbour's value, delete that logic once you move to a bus lane. Nobody else can write it.

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
- **One lane is one float4.** If you need more values, register more ids rather than packing two
  numbers into the decimal digits of one float.
- **Floats only.** Booleans and small integers go in as floats. Strings never.
- **The `owner` string is yours, up to 64 characters,** and the description up to 256. Use the mod's
  display name for `owner`, since it appears in `bus_list` and in the collision fatal.
- **Re-registering with the same `owner` is free.** Scripts re-run on every level load, and
  `register` returns the existing token.
- **A different `owner` on the same id is fatal**, by design, so a collision is loud instead of one
  mod silently overwriting another's value. Use `try_register` for a lane more than one mod might
  cooperatively want; a refused `try_register` returns `nil` and logs which `owner` kept it.
- **There is no compatibility check for old exes.** `if shader_bus then` is a complete detection on
  its own, and the all-zero default above is a convention this bus provides, not a guarantee on an
  exe that predates it.

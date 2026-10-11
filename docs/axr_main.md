## Changes
* Possibility to change execution order, define what callback functions should execute before or after when registering new callback.
* Add Pre, Normal and Post callbacks via `priority` argument. Pre will run before all normal callbacks, post - after. Default is Normal

`RegisterScriptCallback` new signature is `RegisterScriptCallback(name, func_or_userdata, before_funcs, after_funcs, priority)`, where new arguments are:
* `before_funcs` - function or table of functions. `func_or_userdata` will run Before `before_funcs` functions are executed. Default is empty table
* `after_funcs` - function or table of functions. `func_or_userdata` will run After `after_funcs` functions are executed. Default is empty table
* `priority` - defines a group where callback will reside and affect where this callback will run in relation to other groups. Each group also supports `before` and `after` funcs order described above. Supported values in this priority order (one runs before other, ie all PRE callbacks run before NORMAL, etc):
  * `CALLBACKS_PRE`
  * `CALLBACKS_NORMAL` (default value)
  * `CALLBACKS_POST`

Shorthands for PRE and POST callbacks:
* `RegisterScriptCallbackPre(name, func_or_userdata, before_funcs, after_funcs)` (equivalent to `RegisterScriptCallback(name, func_or_userdata, before_funcs, after_funcs, CALLBACKS_PRE)`
* `RegisterScriptCallbackPost(name, func_or_userdata, before_funcs, after_funcs)` (equivalent to `RegisterScriptCallback(name, func_or_userdata, before_funcs, after_funcs, CALLBACKS_POST)`

Example:
```lua
    RegisterScriptCallback("actor_on_update", actor_on_update, actor_status_sleep.actor_on_update, {actor_status_thirst.actor_on_update, dynamic_news_manager.actor_on_update})

    RegisterScriptCallbackPre("actor_on_update", actor_on_update, actor_status_sleep.actor_on_update)

    RegisterScriptCallbackPost("actor_on_update", actor_on_update)

```

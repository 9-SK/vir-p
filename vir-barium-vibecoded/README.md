# vir-barium-vibecoded

> Archive of the **Alpen-base port** — the `vir-p-alpha` shell running on top of the
> Alpen runtime. **Unworking as archived**: boot, state discovery and the WHSJ step
> hook all succeed live; environment init faults inside `lua_newthread` and script
> execution is never reached. Preserved for reference; superseded by `module4`
> (`vir-r-base3-beta`) on `main`.

## What this is

A port of the executor onto the **Alpen** base (the `ref\RobloxExecutor` reference
runtime), kept as one tree:

- **Alpen's spine** — shadow-vtable `TPHandler` hook, `KiGetLuaState` encoded-state
  scan, environment + library set.
- **viridian shell** (`Entry.cpp`) — boot telemetry, dual-path file watcher
  (`%LOCALAPPDATA%\Hello.txt` and `%LOCALAPPDATA%\viridian\execute.lua`), status
  writer.
- **Fork-corrected Luau** (`Dependencies/luau/`) — the 0.743 fork's layouts mirrored
  over the 0.741 dependency tree (e.g. `LSTATE_STACKSIZE_ENC` stacksize at `0x70`).

## How it works (as built)

The boot chain, straight from `%LOCALAPPDATA%\viridian\boot.log` on the live client:

```
[boot] vir-p-alpha online
[boot] execute.lua watcher live
[env]  gameloaded sentinel=31            DataModel alive  (FakeDataModel -> +0x1F8, sentinel +0x5D0)
[env]  scriptcontext=<addr>              ScriptContext found (+0x440)
[hook] whsj job=<addr> table=<vt>        WaitingHybridScriptsJob located
[hook] vtable swapped, step live         job step shadowed; the original still runs
[init] probe a=<addr> b=1
[init] context a=<addr> b=<addr>
[env]  ki state=<addr> field=+0x400      client lua_State decoded from +0x400
[init] ki state a=<addr> b=<addr>
[init] step: bypass write
[init] step: lua_newthread
[init] FAULT lua_newthread code=c0000005 extra=<state>
[env]  env init returned false
```

1. **Inject** — the viridian injector manual-maps the module into the client.
2. **Boot** — `Entry.cpp` starts telemetry and the `execute.lua` watcher.
3. **Find the world** — `FakeDataModel` deref (`+0x1F8`) with a liveness gate
   (sentinel `+0x5D0 == 31`); `ScriptContext` via the children walk (`+0x440`).
4. **Decode the state** — the Ki scan walks `ScriptContext+0..0x1000` for an
   encoded `lua_State` pointer (`field = +0x400` on this client), trying the four
   encode/decode ops.
5. **Hook the scheduler** — WHSJ found in the TaskScheduler job list; its vtable's
   step is shadowed so our step runs first (the original always still runs).
6. **Init the environment** (once, on the step) — the init chain writes the
   ScriptContext bypass, then creates the executor thread via `lua_newthread`;
   sandbox and library registration follow. **The fault happens here.**
7. **Execute** (never reached) — `execute.lua` -> compile -> run on a new thread of
   the engine's state.

## Why it does not work

`lua_newthread` on the engine's real state faults with an access violation
(`c0000005`). Root-cause candidates from the analysis pass: the engine's own
`lua_newthread` includes an extraspace null-out block under `__try` that this
build's call path lacks, plus fork divergence in the alloc/GC path. The init
pipeline is SEH-guarded, so the client survives the fault — the module simply never
comes up. Detailed root-cause work lives in the project's local (unshipped) analysis
notes; the direction was abandoned in favor of the module4 base.

## Build

- `Alpen.vcxproj`, x64 Release -> `Build\module.dll` (the original pipeline staged
  it as `bin\viridian.dll`).
- Dependencies are vendored: fork-corrected Luau, archived 0.741 Luau, curl 8.17.0,
  cpr 1.11.2, zlib 1.3.1.

## Credits

- Base: **Alpen** (`ref\RobloxExecutor`) — environment by **Dyn**; base by
  **lucef**. The base's original note is preserved at
  [README-ALPEN.md](README-ALPEN.md).
- Shell, port corrections, instrumentation: viridian project.

## Family

```
viridian v8 (root workspace)            first client-state executor, scripts ran
 └─ module/  -> vir-alpha-vibecoded     (Hyperion tree, full env; working)
     ├─ module2/  (exbeta base port)    froze during step; abandoned
     └─ module3/  -> vir-barium-vibecoded   <- this branch (Alpen port; init faults)
         └─ module4/  (vir-r-base3-beta)    current base -> main
```

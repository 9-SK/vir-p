# vir-alpha-vibecoded

> Archive of the **old Viridian module** — internally the "Hyperion" tree. The most
> complete legacy build of the executor: small scripts run end-to-end on the live
> client; the library surface is partial. Superseded by `module4` (`vir-r-base3-beta`)
> on `main`.

## What this is

A manually-mapped module for the Roblox client (0.743 generation). It finds the
client's **own** Lua state and scheduler and runs scripts on the engine's real
runtime — it ships a complete Luau fork (`Dependencies/Luau/`) and calls that C API
against the engine's `global_State`.

```
%LOCALAPPDATA%\Viridian\execute.lua          script inbox (file-polled)
        |
        v
   ExecWatcher -> luau_load -> task.defer -> WaitingHybridScriptsJob step
```

Builds as `viridian.dll`; the matching injector is `injector/` at the repo root
(`bin\Viridian.exe`).

## How it works

1. **Injector** manual-maps the DLL into RobloxPlayerBeta (staging + thread-pool
   load; debug privilege).
2. **EntryPoint.cpp** — `DllMain` registers the module's unwind tables
   (`RtlAddFunctionTable`) because manual mapping keeps the module out of the
   loader's module lists; then a worker thread boots the rest.
3. **Telemetry & channels** — `StatusRing`/`BootLog` write
   `%LOCALAPPDATA%\Viridian\boot.log`; `ExecWatcher` watches `execute.lua`;
   `Communication` opens the local TCP control channel (127.0.0.1:6969);
   `Scheduler::TeleportHandler` keeps the module alive across teleports.
4. **Scheduler bind** — finds `DataModel` (FakeDataModel deref; liveness sentinel
   `+0x5D0 == 31`), probes the TaskScheduler global (multiple RVAs, dumps job names
   when a probe misses), finds `WaitingHybridScriptsJob` and swaps its vtable's step
   slot — the original step is always called, ours runs first.
5. **Environment init** (one-time, on the job step) — discovers the client's
   `lua_State` from `ScriptContext` (encoded-field scan, several decode ops),
   sandboxes, then registers ~24 libraries: Closures, Debug, Drawing, FileDialog,
   Filesystem, Http, Input, Instance, LuaStateProxy, Metatable, Misc, Raknet,
   Reflection, Regex, Script, Signals, Websocket, Actor, Bit, Cache, Crypt, ...
6. **Execution** — script text -> `luau_load` on the executor state -> engine
   `task.defer` -> runs on the next job step, on the real state.

## Status (honest)

- Works: injection, boot, state/scheduler discovery, WHSJ hook, environment init,
  small scripts (boot/demo prints; UNC checker fetch/start).
- Partial/unfinished: the hard UNC cluster — `hookfunction`, `newcclosure`,
  `getgenv`/`getsenv`, `getconnections`, script introspection, WebSocket.
- History: earlier builds logged an engine-side access violation during init
  (`engine+0x49EEBA7`, stats accounting `inc [rcx]` with `rcx = -1`) in some
  sessions; later builds carried mitigations. Treat as partially working; not
  developed further.

## Build

1. Open `Viridian.sln`, x64 Release -> `build\viridian.dll`.
2. Retarget the toolset to yours if the solution asks for a newer one.
3. All dependencies are vendored under `Dependencies/`.

## Layout

```
EntryPoint.cpp          manual-map entry; unwind tables; worker start
Roblox/Hyperion/        scheduler/task-scheduler, execution, channels, telemetry
Roblox/Environment/     ~24 libraries (one unit per library)
Roblox/Renderer/        render hook (drawing support)
Roblox/Shared/          crash handler, memory helpers, shared globals
Roblox/Update/          engine offsets + VM value encodings
Dependencies/           Luau fork, imgui, cpr, curl, cryptopp, zstd, nlohmann
```

## Family

```
viridian v8 (root workspace)            first client-state executor, scripts ran
 └─ module/  -> vir-alpha-vibecoded     <- this branch (Hyperion tree, full env)
     ├─ module2/  (exbeta base port)    froze during step; abandoned
     └─ module3/  -> vir-barium-vibecoded   (Alpen base port; init faults)
         └─ module4/  (vir-r-base3-beta)    current base -> main
```

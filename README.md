# vir-alpha-vibecoded

> Archive of the **working legacy module** — the VynxVX tree. Kept as the known-good
> reference build; superseded by `module4` (`vir-r-base3-beta`) on `main`.

## What this is

A manually-mapped module for the Roblox client carrying a complete in-tree executor:
scheduler bind + job hooks, a 25-library environment, renderer/graphics hook, the
full HTTP/WebSocket stack (cpr / curl / cpp-httplib / ixwebsocket / mbedtls), a
Luau2 fork, and a file-driven script channel.

Builds as `Module.dll` (`VynxVX.vcxproj`, TargetName `Module`).

```
%LOCALAPPDATA%\Viridian\execute.lua          script inbox (250 ms poller)
        |
        v
execute_file_watcher -> task_scheduler::RequestExecution -> execution
```

## How it works

Boot (`entry.cpp` — `DllMain` -> `MainThread` on a detached thread):

1. **Single-instance guard** — a process-lifetime named mutex, so a second load in
   the same process bails out immediately.
2. **Ring status first** — `ViridianRing::Init()` / `MarkInjecting()` publishes UI
   liveness within milliseconds of `DllMain` (never gated behind the boot log).
3. **Boot log** — `%LOCALAPPDATA%\Viridian\boot.log` gets `BOOT pid=...`, then a
   phase line as each subsystem spawns.
4. **Workspace** — `%LOCALAPPDATA%\Viridian\workspace` created.
5. **Communication** — TCP control channel initialized (`phase=net spawned`).
6. **TpHandler** — teleport handling (`phase=tp spawned`).
7. **Watchers** — `execute_file_watcher` + `raknetwatcher` (`phase=watchers spawned`).
8. **Renderer hook** — `graphics->initialize()` on its own SEH-guarded thread
   (`phase=gfx spawned`); if graphics init faults, the module continues without the
   renderer hook.

The watchers:

- **execute.lua watcher** — 250 ms polling with a change-only baseline (size + mtime
  snapshot at boot, so a stale file from a previous session can never fire);
  write-settle delay; 8 MB cap; UTF-8 BOM stripped; source handed to
  `task_scheduler::RequestExecution`.
- **raknet watcher** — bounded 60 s wait for the `Net Peer Send` job (then slow 5 s
  background retries), installs `Raknet::InitializeHook`, and re-arms itself when
  the job disappears (teleport/rejoin).

Inside:

- `RBX/task_scheduler/` — job discovery, WHSJ finding, `RequestExecution` pipeline.
- `RBX/execution/` — compile + run path for queued scripts.
- `RBX/security/` — identity/capability handling, string utilities.
- `RBX/environment/libs/` — 25 libraries: actor, bit, cache, closures, crypt, debug,
  drawing (+ drawingimmediate), filedialog, filesystem, http, input, instance,
  luastateproxy, metatable, miscellaneous, oth, raknet, rconsole, reflection,
  regex, script, signals, websocket.
- `Renderer/` — graphics hook implementation (ImGui / drawing).
- `Utilitys/` — `Communication` (TCP), `RingStatus` (shared status ring),
  `TPHandler`, globals/utils.
- `managers/logging`, `Scripts/` (`send_flags.lua`).
- `Dependencies/` — Luau2 fork, cpp-httplib, cpr, curl, ixwebsocket, mbedtls,
  nlohmann, imgui, Zstd, psa.

`DllMain` detach is deliberately minimal (only `RConsole::OnProcessDetach()`): every
DLL thread is detached and dies with the process, and no joins, locks or ring
teardown run — a zombie-process incident is documented in the source and avoided by
design.

## Status

Known-working legacy module — kept as the reference build. Superseded by `module4`
(`vir-r-base3-beta`) on `main`.

## Build

- `VynxVX.sln` / `VynxVX.vcxproj` -> x64 Release -> `Module.dll`.
- Project toolset is `v145`; retarget to `v143` if your VS does not have it.
- Dependencies vendored under `Dependencies/`.

## Layout

```
entry.cpp            DllMain, MainThread, watchers, raknet re-arm
RBX/                 task_scheduler, execution, security, environment/libs
Renderer/            graphics/render hook
Utilitys/            Communication, RingStatus, TPHandler, utils
managers/            logging
Scripts/             built-in send_flags.lua
Dependencies/        Luau2, curl/cpr/cpp-httplib/ixwebsocket/mbedtls, imgui, Zstd, nlohmann
```

## Family

```
vir-alpha-vibecoded   working legacy module     (redundent/module — VynxVX tree)   <- this branch
vir-barium-vibecoded  Alpen-base port           (module3 — init faults)
main                  current base              (module4 — vir-r-base3-beta)
```

#pragma once
#include <Windows.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include <string>
#include <mutex>
#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <Utilitys/RingStatus.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <Renderer/Renderer.hpp>


namespace
{
    inline std::atomic<bool> g_SetupCrashed{ false };
    inline std::atomic<bool> g_Running{ false };
    inline std::atomic<bool> g_DmTimeoutLogged{ false };
    inline std::atomic<bool> g_GameTimeoutLogged{ false };
    // DataModel for which [tp] re-hook progress was already logged. RunIteration
    // retries a failing DataModel every 100ms, so progress lines are emitted
    // once per DataModel instead of spamming boot.log.
    inline std::atomic<uintptr_t> g_TpProgressDM{ 0 };

    // boot.log line for TPHandler timeout phases. Throttled to once per boot
    // per waiter: RunIteration retries continuously, so an unthrottled log
    // would spam boot.log at menu/loading. File I/O, no locks held.
    inline void TpBootLog(const char* msg)
    {
        char bb[MAX_PATH]{};
        DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
        if (!bn || bn >= MAX_PATH) return;
        std::string bp = std::string(bb) + "\\Viridian\\boot.log";
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf) return;
        SYSTEMTIME st{}; GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [tp] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
        fclose(bf);
        ViridianRing::RefreshLogTail();
    }

    // File-only switch marker for the UI (tails %LOCALAPPDATA%\Viridian\boot.log).
    // Format matches task_scheduler's ViridianLog("game", ...) line so the UI can
    // grep a single "[game] switched" pattern regardless of which thread detects
    // the teleport first. Logged BEFORE the new "[init] ... OK".
    inline void TpLogSwitch(uintptr_t oldDM, uintptr_t newDM)
    {
        char bb[MAX_PATH]{};
        DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
        if (!bn || bn >= MAX_PATH) return;
        std::string bp = std::string(bb) + "\\Viridian\\boot.log";
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf) return;
        SYSTEMTIME st{}; GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [game] switched datamodel (old=%p new=%p)\n",
            st.wHour, st.wMinute, st.wSecond, (void*)oldDM, (void*)newDM);
        fclose(bf);
        ViridianRing::OnSwitch(newDM);
        ViridianRing::RefreshLogTail();
    }

    // No-__try helper: MSVC forbids C++ unwindables (lock_guard) lexically
    // inside any function that also uses __try (C2712), so RAII work lives
    // here; RunIteration (which uses __try) just calls it.
    inline void ClearFireDispatchThreads()
    {
        std::lock_guard<std::mutex> lock(Shared::FireDispatchMutex);
        Shared::FireDispatchThreads.clear();
    }

    inline void TpLog(const char* msg, const char* level = ("INFO"))
    {
        char buf[256]{};
        snprintf(buf, sizeof(buf), ("[Viridian] TpHandler: %s"), msg);
        Roblox::Print(3, buf);
        OutputDebugStringA(buf);
        OutputDebugStringA(("\n"));
        Renderer::AddConsoleLog(level, ("TpHandler"), msg);
    }

    inline bool TrySetupExploit(uintptr_t DataModel)
    {
        g_SetupCrashed.store(false, std::memory_order_relaxed);
        Renderer::AddConsoleLog(("INFO"), ("initialize_exploit"), ("Starting exploit setup..."));
        __try
        {
            bool result = task_scheduler::initialize_exploit(DataModel);
            if (result)
                Renderer::AddConsoleLog(("INFO"), ("initialize_exploit"), ("Setup completed successfully"));
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
          
            Renderer::AddConsoleLog(("ERROR"), ("initialize_exploit"), ("Exception caught during setup"));
            g_SetupCrashed.store(true, std::memory_order_relaxed);
            return false;
        }
    }

    inline bool IsDataModelAlive(uintptr_t dm)
    {
        if (!dm) return false;
        __try
        {
            volatile uint32_t test = *reinterpret_cast<uint32_t*>(
                dm + Offsets::DataModel::GameLoaded);
            (void)test;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    inline uintptr_t WaitForDataModel(int maxAttempts = 10, int delayMs = 500)
    {
        for (int i = 0; i < maxAttempts; ++i)
        {
            uintptr_t dm = 0;
            __try
            {
                dm = task_scheduler::GetDataModel();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
         
                Sleep(delayMs);
                continue;
            }

            if (dm && IsDataModelAlive(dm))
                return dm;
            Sleep(delayMs);
        }
        return 0;
    }

    inline bool WaitForGameReady(uintptr_t dm, int maxAttempts = 20, int delayMs = 500)
    {
        for (int i = 0; i < maxAttempts; ++i)
        {
            if (!IsDataModelAlive(dm))
                return false;

            bool inGame = false;
            __try
            {
                inGame = Utils::IsInGame(dm);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            
                return false;
            }

            if (inGame)
            {
                Sleep(100);
                if (IsDataModelAlive(dm))
                {
                    bool inGame2 = false;
                    __try
                    {
                        inGame2 = Utils::IsInGame(dm);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                   
                        return false;
                    }
                    if (inGame2)
                        return true;
                }
            }
            Sleep(delayMs);
        }
        return false;
    }

    inline bool RunIteration()
    {
        // â”€â”€ Step 1: WaitForDataModel â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        uintptr_t DataModel = WaitForDataModel();
        if (!DataModel)
        {
            if (!g_DmTimeoutLogged.exchange(true, std::memory_order_relaxed))
                TpBootLog("timeout waiting for DataModel (5s), retrying in background");
            return false;
        }

        if (Shared::LastDataModel == DataModel)
        {
            // LastDataModel latches only after hook+init success on both
            // paths, so a match normally means live — but the latch may sit
            // on a DEAD job object (stepHook raced the swap: it Reset+inited
            // on the old ticking job without re-hooking the new one). A
            // fresh hook probe tells live from stale: live waits here, stale
            // falls through to the reset + re-hook + re-init below instead
            // of spinning forever with a dead hook. One job-list scan per
            // 100ms poll; never on the scheduler tick.
            if (task_scheduler::IsWHSJHookLive())
                return false;
            // Stale/absent hook: fall through to repair. LastDataModel stays
            // latched but no longer gates — every path below re-validates.
        }

        // Already re-initialized on this DataModel by the scheduler tick
        // (stepHook won the race: old job still ticked with the new DM).
        // Sync hook progress and skip so we don't reset a live ExploitThread
        // or emit a duplicate [game] switched marker. TeleportQueue was
        // already drained by the stepHook path.
        if (Shared::IsInitializedOnGame.load(std::memory_order_acquire) &&
            Shared::InitializedDataModel == DataModel)
        {
            // Same staleness guard as above: only skip when the hook is
            // provably live on the CURRENT job. A latch without a live hook
            // falls through to repair (old==new down there, so no duplicate
            // [game] switched marker is emitted).
            if (task_scheduler::IsWHSJHookLive())
            {
                Shared::LastDataModel = DataModel;
                return false;
            }
        }



        // â”€â”€ Step 2: WaitForGameReady â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!WaitForGameReady(DataModel))
        {
            if (!g_GameTimeoutLogged.exchange(true, std::memory_order_relaxed))
                TpBootLog("timeout waiting for game ready (10s), retrying in background");
            return false;
        }

        // â”€â”€ Step 3: Verify DataModel alive â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!IsDataModelAlive(DataModel))
        {

            return false;
        }

        // Re-check after the (slow) WaitForGameReady: stepHook may have
        // handled the switch while we were waiting for the game to load.
        // Hook-gated: a latch on a dead job object must repair, not skip.
        if (Shared::IsInitializedOnGame.load(std::memory_order_acquire) &&
            Shared::InitializedDataModel == DataModel)
        {
            if (task_scheduler::IsWHSJHookLive())
            {
                Shared::LastDataModel = DataModel;
                return false;
            }
            // else: fall through to reset + re-hook + re-init below.
        }

        // Log the switch BEFORE the new [init] so the UI (tails boot.log)
        // can show a brief "switching…" state. First boot (old==0) logs
        // nothing — the [init] ... OK alone signals readiness.
        uintptr_t oldDM = Shared::InitializedDataModel != 0
            ? Shared::InitializedDataModel
            : Shared::LastDataModel;
        const bool isTeleport = (oldDM != 0 && oldDM != DataModel);
        // One set of [tp] progress lines per DataModel (RunIteration retries a
        // failing DataModel every 100ms — never log per retry).
        const bool logProgress = isTeleport && (g_TpProgressDM.exchange(DataModel) != DataModel);
        if (isTeleport)
        {
            TpLogSwitch(oldDM, DataModel);
            Renderer::AddConsoleLog(("INFO"), ("TpHandler"), ("DataModel switched, re-hooking"));
            if (logProgress)
                TpBootLog("teleport re-hook start");
        }

        // â”€â”€ Step 4: Reset shared state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        __try
        {
            Shared::ExploitThread = nullptr;
            Shared::RobloxState = nullptr;
            Shared::ScriptContext = 0;
            Shared::IsInitializedOnGame.store(false, std::memory_order_release);
            Shared::InitializedDataModel = 0;
            // Bump here (not just at successful re-init): in-flight async
            // completions check this generation before resuming into Lua, so
            // stale HTTP yields for the dead DataModel are dropped promptly
            // even if the re-init below is delayed by a flood.
            Shared::StateGeneration.fetch_add(1);
            // NOTE: ExecutionRequests is intentionally PRESERVED here (queued
            // scripts survive the loading screen and run after re-init).
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        
            Sleep(100);
            return false;
        }

        // â”€â”€ Step 5: UnhookWHJSJob â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Old Lua threads die with the old DataModel; drop tracking so stale
        // pointers can never match a new thread. Helper (no __try there):
        // MSVC forbids C++ unwindables (lock_guard) inside __try (C2712).
        ClearFireDispatchThreads();

        __try
        {
            task_scheduler::UnhookWHJSJob();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
 
            Sleep(100);
            return false;
        }

        // â”€â”€ Step 6: initialize_exploit â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

        if (!TrySetupExploit(DataModel))
        {
            // Poison-freeze recovery: never latch LastDataModel on failure
            // (crashed or plain miss). Latching would satisfy the early-out
            // (LastDataModel == DataModel) forever and silently freeze all
            // later iterations. Leaving it clear keeps the 100ms retry alive.
            // Teleport/reset semantics preserved: Step 4 reset above already
            // ran, and the success path below still latches LastDataModel.
            bool crashed = g_SetupCrashed.load(std::memory_order_relaxed);
            (void)crashed;

            if (logProgress)
                TpBootLog("teleport re-hook failed, retrying in background");
            return false;
        }



        Shared::LastDataModel = DataModel;
        if (logProgress)
            TpBootLog("teleport re-hook ok");
        return true;
    }

    inline void TpHandlerThread()
    {
       

        while (true)
        {
            RunIteration();
            Sleep(100);
        }
    }
}

namespace TpHandler
{
    inline void Initialize()
    {
        if (g_Running.exchange(true))
            return;
        std::thread(TpHandlerThread).detach();
    }
}

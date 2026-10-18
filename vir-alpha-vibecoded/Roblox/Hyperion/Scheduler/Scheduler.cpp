#include "Scheduler.hpp"
#include <intrin.h>
#include <Roblox/Hyperion/Communication/BootLog.hpp>
#include <Roblox/Hyperion/Communication/StatusRing.hpp>
#include <Roblox/Environment/Environment.hpp>
#include <Roblox/Environment/Yielding/Yielding.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Roblox/Environment/Libraries/Drawing.hpp>
#include <Roblox/Environment/Libraries/Raknet.hpp>

void Scheduler::Reset()
{
    

    Shared::VoltState = nullptr;
    Shared::RobloxState = nullptr;
    Drawing::ClearAll();
}

static bool InitializeGameImpl()
{
    if (Shared::isInitializedOnGame)
        return true;

    uintptr_t ScriptContext =
        TaskScheduler::GetScriptContext(Shared::lastKnownDatamodel);

    if (!ScriptContext)
    {
        static LONG s_ctxLogged = 0;
        if (InterlockedCompareExchange(&s_ctxLogged, 1, 0) == 0)
            BootLog::WriteLine("init", "stage: ScriptContext null");
        return false;
    }

    lua_State* MainThread = nullptr;
    __try { MainThread = TaskScheduler::GetLuaStateForInstance(ScriptContext); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    if (!MainThread)
    {
        // ScriptContext scan came up short (member encoded or moved):
        // fall back to the state harvested from the WHSJ job object.
        MainThread = TaskScheduler::FindStateInJob(Shared::lastWHSJ);
    }

    if (!MainThread)
    {
        static LONG s_luaLogged = 0;
        if (InterlockedCompareExchange(&s_luaLogged, 1, 0) == 0)
        {
            char Msg[96]{};
            snprintf(Msg, sizeof(Msg), "stage: lua_State null sc=%llx",
                static_cast<unsigned long long>(ScriptContext));
            BootLog::WriteLine("init", Msg);
        }
        return false;
    }

    Shared::RobloxState = MainThread;
    if (!Shared::RobloxState)
        return false;

    // Precheck at the point of use: identity bytes survive on freed memory,
    // so demand the full liveness gate - live allocator machinery AND the
    // extraspace chain agreeing with this ScriptContext. lua_newthread on
    // VM-rebuild debris otherwise corrupts the engine's GC state and the
    // client dies moments later.
    if (!TaskScheduler::StateAlive(reinterpret_cast<uintptr_t>(MainThread),
            ScriptContext, reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))))
    {
        static LONG s_preCount = 0;
        const LONG n = InterlockedIncrement(&s_preCount);
        if (n <= 4 || (n & 31) == 0)
            BootLog::WriteLine("init", "stage: state precheck failed (not live)");
        return false;
    }

    Shared::VoltState = lua_newthread(Shared::RobloxState);
    if (!Shared::VoltState)
    {
        static LONG s_voltLogged = 0;
        if (InterlockedCompareExchange(&s_voltLogged, 1, 0) == 0)
            BootLog::WriteLine("init", "stage: lua_newthread null");
        return false;
    }

    luaL_sandboxthread(Shared::VoltState);
    BootLog::WriteLine("init", "sandbox done, building environment");
    Environment::Initialize(Shared::VoltState);

    Shared::isInitializedOnGame = true;

    {
        char Msg[96]{};
        snprintf(Msg, sizeof(Msg), "environment ready dm=%llx",
            static_cast<unsigned long long>(Shared::lastKnownDatamodel));
        BootLog::WriteLine("init", Msg);
    }
    StatusRing::OnReady(Shared::lastKnownDatamodel);

    return true;
}

// First-pass exception snapshots. The dispatcher mutates the exception
// record in place during the unwind (its code becomes STATUS_UNWIND and
// the stack context is torn down), so the real code / fault address /
// access address / return address must be copied at first sight.
static DWORD s_InitCode = 0;
static DWORD64 s_InitAt = 0;
static DWORD64 s_InitAcc = 0;
static DWORD64 s_InitRa = 0;
static DWORD64 s_InitRaws[4] = {};
static DWORD s_StepCode = 0;
static DWORD64 s_StepAt = 0;
static DWORD64 s_StepAcc = 0;
static DWORD64 s_StepRa = 0;
static DWORD64 s_StepRaws[4] = {};

// Memory-probe: the engine's allocation stats layer faults when its
// per-thread slot pointer [TLS+0x2F90] (or the fallback statics at
// engine+0x48CB7460/68/90) holds garbage. The engine's own writers can
// never produce a negative value there, so log the cell's contents at key
// points to catch what writes it (audit: ref\analysis\engine_cluster_49C.md).
static void LogMemProbe(const char* Tag)
{
    const uintptr_t EngBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const auto Rd = [](uintptr_t Address) -> uintptr_t
    {
        __try
        {
            return *reinterpret_cast<uintptr_t*>(Address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    };

    char Msg[192]{};
    snprintf(Msg, sizeof(Msg), "%s memprobe tls2F90=%llx s7460=%llx s7468=%llx s7490=%llx",
        Tag,
        static_cast<unsigned long long>(__readgsqword(0x2F90)),
        static_cast<unsigned long long>(Rd(EngBase + 0x48CB7460)),
        static_cast<unsigned long long>(Rd(EngBase + 0x48CB7468)),
        static_cast<unsigned long long>(Rd(EngBase + 0x48CB7490)));
    BootLog::WriteLine("probe", Msg);
}

// Filters run during first-pass exception dispatch: they must not touch
// anything that can fault (no logging, no file I/O). Snapshot and defer.
static void SnapshotRecord(EXCEPTION_POINTERS* EP, DWORD* Code, DWORD64* At, DWORD64* Acc, DWORD64* Ra, DWORD64* Raws)
{
    *Code = 0;
    *At = 0;
    *Acc = 0;
    *Ra = 0;
    for (int i = 0; i < 4; ++i)
        Raws[i] = 0;
    if (!EP || !EP->ExceptionRecord)
        return;

    // Plain reads only: filters must not run nested exception handling.
    // The record/context are valid by definition at first pass, and slots
    // above rsp are committed stack (the stack grows down), so none of
    // these reads can fault.
    *Code = EP->ExceptionRecord->ExceptionCode;
    *At = reinterpret_cast<DWORD64>(EP->ExceptionRecord->ExceptionAddress);
    if (*Code == 0xC0000005u && EP->ExceptionRecord->NumberParameters >= 2)
        *Acc = EP->ExceptionRecord->ExceptionInformation[1];
    if (EP->ContextRecord)
    {
        // The faulting instruction sits past its function prologue, so the
        // return address is NOT at [rsp]. Capture the first four slots above
        // rsp raw (unfiltered - attribution happens at log time), and pick
        // the first that lands inside a known module as the best guess.
        const DWORD64 Sp = EP->ContextRecord->Rsp;
        const DWORD64 EngBase = reinterpret_cast<DWORD64>(GetModuleHandleA(nullptr));
        for (int i = 0; i < 4; ++i)
        {
            const DWORD64 V = *reinterpret_cast<DWORD64*>(Sp + 0x20 + 8 * i);
            Raws[i] = V;
            if (!*Ra &&
                ((V >= EngBase && V < EngBase + 0x10000000) ||
                 (Shared::ModuleBase && V >= Shared::ModuleBase && V < Shared::ModuleBase + 0x400000)))
                *Ra = V;
        }
    }
}

static LONG InitExceptionFilter(EXCEPTION_POINTERS* EP)
{
    // The dispatcher re-enters filters with STATUS_UNWIND (0xC0000027)
    // during the unwind pass. That is the unwind asking permission, not an
    // exception to handle: claiming it interrupts the unwind and kills the
    // process. Always continue searching on that code.
    if (EP && EP->ExceptionRecord && EP->ExceptionRecord->ExceptionCode == 0xC0000027u)
        return EXCEPTION_CONTINUE_SEARCH;

    SnapshotRecord(EP, &s_InitCode, &s_InitAt, &s_InitAcc, &s_InitRa, s_InitRaws);
    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG StepExceptionFilter(EXCEPTION_POINTERS* EP)
{
    if (EP && EP->ExceptionRecord && EP->ExceptionRecord->ExceptionCode == 0xC0000027u)
        return EXCEPTION_CONTINUE_SEARCH;

    SnapshotRecord(EP, &s_StepCode, &s_StepAt, &s_StepAcc, &s_StepRa, s_StepRaws);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void LogException(DWORD Code, DWORD64 At, DWORD64 Acc, DWORD64 Ra, const DWORD64* Raws, const char* Tag)
{
    static LONG s_excCount = 0;
    LONG n = InterlockedIncrement(&s_excCount);
    if (n > 8)
        return;

    DWORD64 Mod = 0;
    DWORD64 Rva = 0;

    // Attribute the faulting instruction: engine module first, then our own
    // manually-mapped image (base discovered during unwind registration).
    const DWORD64 EngineBase = reinterpret_cast<DWORD64>(GetModuleHandleA(nullptr));
    if (At >= EngineBase && At < EngineBase + 0x10000000)
    {
        Mod = EngineBase;
        Rva = At - EngineBase;
    }
    else if (Shared::ModuleBase && At >= Shared::ModuleBase && At < Shared::ModuleBase + 0x400000)
    {
        Mod = Shared::ModuleBase;
        Rva = At - Shared::ModuleBase;
    }

    char Msg[192]{};
    snprintf(Msg, sizeof(Msg), "%s throw code=%08lx at=%llx acc=%llx ra=%llx mod=%llx rva=%llx n=%ld",
        Tag,
        static_cast<unsigned long>(Code),
        static_cast<unsigned long long>(At),
        static_cast<unsigned long long>(Acc),
        static_cast<unsigned long long>(Ra),
        static_cast<unsigned long long>(Mod),
        static_cast<unsigned long long>(Rva),
        static_cast<long>(n));
    BootLog::WriteLine("init", Msg);

    if (Raws)
    {
        char Msg2[160]{};
        snprintf(Msg2, sizeof(Msg2), "%s raw ra=%llx %llx %llx %llx",
            Tag,
            static_cast<unsigned long long>(Raws[0]),
            static_cast<unsigned long long>(Raws[1]),
            static_cast<unsigned long long>(Raws[2]),
            static_cast<unsigned long long>(Raws[3]));
        BootLog::WriteLine("init", Msg2);
    }

    LogMemProbe("at-fault");
}

static bool InitializeGame()
{
    __try
    {
        return InitializeGameImpl();
    }
    __except (InitExceptionFilter(GetExceptionInformation()))
    {
        LogException(s_InitCode, s_InitAt, s_InitAcc, s_InitRa, s_InitRaws, "init");
        Shared::isInitializedOnGame = false;
        Shared::RobloxState = nullptr;
        Shared::VoltState = nullptr;
        return false;
    }
}

static bool ExecuteGameImpl()
{
    std::vector<std::string> Pending;
    std::vector<uint32_t> PendingIds;

    {
        std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);

        if (Shared::VoltState && !Shared::ExecutionRequests.empty())
        {
            Pending.swap(Shared::ExecutionRequests);
            PendingIds.swap(Shared::ExecutionRequestIds);
        }
        else if (!Shared::VoltState && !Shared::ExecutionRequests.empty())
        {
            static LONG s_heldLogged = 0;
            if (InterlockedCompareExchange(&s_heldLogged, 1, 0) == 0)
            {
                char Msg[96]{};
                snprintf(Msg, sizeof(Msg), "queue held: %u waiting for init",
                    static_cast<unsigned>(Shared::ExecutionRequests.size()));
                BootLog::WriteLine("exec", Msg);
            }
        }
    }

    // One script per step made every queued execution cost its own frame, so a
    // batch of N took N frames before the last script even loaded. The other
    // extreme is worse: draining the whole queue costs one unbounded frame.
    // Run kMaxPerTick and roll the rest over to later ticks.
    constexpr size_t kMaxPerTick = 4;
    const size_t Batch = Pending.size() < kMaxPerTick ? Pending.size() : kMaxPerTick;

    for (size_t i = 0; i < Batch; ++i)
    {
        if (Pending[i].empty())
            continue;

        const uint32_t Bytes = static_cast<uint32_t>(Pending[i].size());
        const uint32_t ReqId = i < PendingIds.size() ? PendingIds[i] : 0u;

        StatusRing::OnExecBegin();
        const uint64_t T0 = GetTickCount64();
        const bool Ok = Execution::ExecuteScript(Shared::VoltState, std::move(Pending[i]));
        const uint64_t Ms = GetTickCount64() - T0;
        StatusRing::OnExecEnd(Ok, Bytes, static_cast<uint32_t>(Ms));
        if (ReqId)
            StatusRing::OnRequestExecuted(ReqId);

        char Msg[96]{};
        snprintf(Msg, sizeof(Msg), "id=%u bytes=%u ms=%llu ok=%d",
            ReqId, Bytes, static_cast<unsigned long long>(Ms), Ok ? 1 : 0);
        BootLog::WriteLine("exec", Msg);
    }

    // Everything past the batch goes back on the FRONT so the client still
    // sees its scripts in submission order, just spread over later ticks.
    // Inserted back-to-front: each insert(begin()) pushes the previous one
    // down, so walking the tail backwards lands the block in ORIGINAL order.
    if (Pending.size() > Batch)
    {
        std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
        for (size_t i = Pending.size(); i-- > Batch;)
        {
            Shared::ExecutionRequests.insert(Shared::ExecutionRequests.begin(), std::move(Pending[i]));
            Shared::ExecutionRequestIds.insert(Shared::ExecutionRequestIds.begin(),
                i < PendingIds.size() ? PendingIds[i] : 0u);
        }
    }

    Yielding::RunYield();
    return true;
}

static bool ExecuteGame()
{
    __try
    {
        return ExecuteGameImpl();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static uintptr_t stepHook(uintptr_t Job, uintptr_t Stats)
{
    if (!Shared::originalStepFunction)
        return 0;

    static LONG s_tickLogged = 0;
    if (InterlockedCompareExchange(&s_tickLogged, 1, 0) == 0)
    {
        char Msg[96]{};
        snprintf(Msg, sizeof(Msg), "step tick 0 job=%llx", static_cast<unsigned long long>(Job));
        BootLog::WriteLine("tp", Msg);
    }

    if (!Shared::JobState)
    {
        // One job scan per 64 ticks, and only for a grace window - it never
        // pays to VirtualQuery the same object every frame for minutes.
        static ULONG s_Ticks = 0;
        ULONG tick = ++s_Ticks;
        if (tick <= 240 && (tick & 0x3F) == 1)
        {
            lua_State* JobL = TaskScheduler::FindStateInJob(Job);
            if (JobL)
                Shared::JobState = reinterpret_cast<uintptr_t>(JobL);
        }
    }

    if (Shared::lockedStep)
        return Shared::originalStepFunction(Job, Stats);

    if (!Shared::isInitializedOnGame)
    {
        // A failed environment build used to retry every frame; after two
        // immediate attempts, retry at most every 128 ticks (~2s). Each
        // failed attempt mutates live engine state, so cap the total number
        // of attempts - a stuck init must not grind the client down.
        static ULONG s_InitTicks = 0;
        static LONG s_InitAttempts = 0;
        ULONG InitTick = ++s_InitTicks;
        if (InitTick <= 2 || (InitTick & 0x7F) == 0)
        {
            if (InterlockedIncrement(&s_InitAttempts) > 2)
            {
                static LONG s_gaveUp = 0;
                if (InterlockedCompareExchange(&s_gaveUp, 1, 0) == 0)
                    BootLog::WriteLine("init", "init abandoned after 2 attempts");
                return Shared::originalStepFunction(Job, Stats);
            }

            LogMemProbe("attempt");
            if (!InitializeGame())
                return Shared::originalStepFunction(Job, Stats);
        }
        else
        {
            return Shared::originalStepFunction(Job, Stats);
        }
    }
    else if (!ExecuteGame())
    {
        return Shared::originalStepFunction(Job, Stats);
    }

    return Shared::originalStepFunction(Job, Stats);
}

// Catch-all around the entire scheduler tick. An unhandled AV anywhere in
// the step - including inside the engine's own work - otherwise kills the
// process instantly and takes the diagnosis with it. Catching and sitting
// out one tick keeps the client alive and keeps the log accumulating.
static uintptr_t stepHookSafe(uintptr_t Job, uintptr_t Stats)
{
    __try
    {
        return stepHook(Job, Stats);
    }
    __except (StepExceptionFilter(GetExceptionInformation()))
    {
        LogException(s_StepCode, s_StepAt, s_StepAcc, s_StepRa, s_StepRaws, "step");
        return 0;
    }
}

LUAU_FASTFLAG(LuauCIProto)

void Scheduler::TeleportHandler()
{

    Shared::isInitializedOnGame = false;
    Scheduler::Reset();

    BootLog::WriteLine("tp", "handler entered");

    if (TaskScheduler::GetGameLoaded(TaskScheduler::GetDatamodel()) == 15) {
        while (TaskScheduler::GetGameLoaded(TaskScheduler::GetDatamodel()) == 15) {
            Sleep(100);
        }
    }

    if (!Shared::lastKnownDatamodel || Shared::lastKnownDatamodel != TaskScheduler::GetDatamodel()) {
        Shared::lastKnownDatamodel = TaskScheduler::GetDatamodel();
        char Msg[96]{};
        snprintf(Msg, sizeof(Msg), "stable datamodel dm=%llx",
            static_cast<unsigned long long>(Shared::lastKnownDatamodel));
        BootLog::WriteLine("tp", Msg);
    }

    uintptr_t GameWHSJ = TaskScheduler::GetJobByTypeName("WaitingHybridScriptsJob(Default;Ugc)");
    if (!GameWHSJ) {
        Sleep(50);
        return TeleportHandler();
    }

    {
        char Msg[96]{};
        snprintf(Msg, sizeof(Msg), "WHSJ job found job=%llx",
            static_cast<unsigned long long>(GameWHSJ));
        BootLog::WriteLine("tp", Msg);
    }
    StatusRing::BeginInit(Shared::lastKnownDatamodel);

    Shared::lastWHSJ = GameWHSJ;

    if (Shared::ourVFtable == nullptr)
    {
        const auto jobVFtable = *reinterpret_cast<uintptr_t**>(Shared::lastWHSJ);
        const auto newVFtable = new uintptr_t[9];
        memcpy(newVFtable, jobVFtable, sizeof(uintptr_t) * 9);

        Shared::originalStepFunction = reinterpret_cast<Shared::JobStep_t>(jobVFtable[8]);
        newVFtable[8] = reinterpret_cast<uintptr_t>(&stepHookSafe);

        Shared::ourVFtable = newVFtable;
    }

    Shared::lockedStep = false;

    *reinterpret_cast<uintptr_t**>(Shared::lastWHSJ) = static_cast<uintptr_t*>(Shared::ourVFtable);

    BootLog::WriteLine("tp", "hook installed, tick released");

    while (true)
    {
        uintptr_t currentDatamodel = TaskScheduler::GetDatamodel();

        if (currentDatamodel != Shared::lastKnownDatamodel) {
            char Msg[96]{};
            snprintf(Msg, sizeof(Msg), "switch dm=%llx, respawning",
                static_cast<unsigned long long>(currentDatamodel));
            BootLog::WriteLine("tp", Msg);
            StatusRing::OnSwitch(currentDatamodel);
            std::thread(&TeleportHandler).detach();
            break;
        }

        Sleep(10);
    }

}

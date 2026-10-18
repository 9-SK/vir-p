#include <cstdarg>
#include <cstdio>
#include <string>
#include <Utilitys/Globals.hpp>
#include <Utilitys/RingStatus.hpp>
#include <RBX/execution/execution.hpp>
#include <RBX/environment/env.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <RBX/environment/yielding/yielding.hpp>
#include <RBX/environment/libs/raknet/raknet.hpp>
#include <Renderer/Renderer.hpp>


namespace
{
	std::string LastSchedulerError;

	void ReportSchedulerError(const char* Message)
	{
		if (LastSchedulerError == Message)
			return;

		LastSchedulerError = Message;
		Roblox::Print(3, ("[Atlas] scheduler: %s"), Message);
		OutputDebugStringA(Message);
		OutputDebugStringA(("\n"));
	}
}

void task_scheduler::SetProtoCapabilities(Proto* Root, uintptr_t* Capabilities)
{
	if (!Root)
		return;

	std::vector<Proto*> Pending{ Root };
	while (!Pending.empty())
	{
		Proto* Current = Pending.back();
		Pending.pop_back();
		if (!Current)
			continue;

		Current->userdata = Capabilities;
		if (Current->sizep < 0 || Current->sizep > 100000 || (!Current->p && Current->sizep != 0))
			continue;

		for (int Index = 0; Index < Current->sizep; ++Index)
			if (Current->p[Index])
				Pending.push_back(Current->p[Index]);
	}
}

void task_scheduler::SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities)
{
	if (!L || !L->userdata)
		return;

	L->userdata->identity.identity = Level;
	L->userdata->capabilities = Capabilities;
}

void task_scheduler::SetIdentity(lua_State* Thread, int Identity, bool IsInstance)
{
	if (!Thread) {
		return;
	}
	if (!Thread->userdata) {
		return;
	}

	RobloxExtraSpace* basedData = reinterpret_cast<RobloxExtraSpace*>(Thread->userdata);
	basedData->identity.identity = Identity;
	basedData->capabilities = Roblox::GetCapabilities(&Identity);

	if (IsInstance) {
		try {
			uintptr_t IdentityPointer = *reinterpret_cast<uintptr_t*>(Offsets::RobloxThread::IdentityPtr);
			if (!IdentityPointer || IdentityPointer > 0xFFFFFFFFFFFF) {
				return;
			}
			uintptr_t IdentityStructure = Roblox::GetIdentityStruct(IdentityPointer);
			if (!IdentityStructure) {
				return;
			}
			*reinterpret_cast<int32_t*>(IdentityStructure) = Identity;
			*reinterpret_cast<uintptr_t*>(IdentityStructure + Offsets::Capabilities::GetCapabilities) =
				Roblox::GetCapabilities(&Identity);
		}
		catch (...) {
		}
	}
}

uintptr_t task_scheduler::GetDataModel()
{
	__try
	{
		uintptr_t FakeDataModel = *reinterpret_cast<uintptr_t*>(Offsets::DataModel::FakeDataModelPointer);
		if (!FakeDataModel)
			return 0;
		return *reinterpret_cast<uintptr_t*>(FakeDataModel + Offsets::DataModel::FakeDataModelToDataModel);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

uintptr_t task_scheduler::GetScriptContext(uintptr_t DataModel)
{
	if (!DataModel)
		return 0;
	__try
	{
		uintptr_t Children = *reinterpret_cast<uintptr_t*>(DataModel + Offsets::DataModel::Children);
		if (!Children)
			return 0;
		uintptr_t FirstChild = *reinterpret_cast<uintptr_t*>(Children);
		if (!FirstChild)
			return 0;
		return *reinterpret_cast<uintptr_t*>(FirstChild + Offsets::DataModel::ScriptContext);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

inline bool CheckMemory(uintptr_t address) {
	if (address < 0x10000 || address > 0x7FFFFFFFFFFF)
		return false;

	MEMORY_BASIC_INFORMATION mbi;
	if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0)
		return false;

	if (mbi.Protect & PAGE_NOACCESS || mbi.State != MEM_COMMIT)
		return false;

	return true;
}

inline lua_State* GetLuaStateForInstance(uintptr_t sc)
{
	if (!sc)
		return nullptr;

	__try
	{
		*reinterpret_cast<BOOLEAN*>(sc + Offsets::ExtraSpace::RequireBypass) = TRUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}

	auto vmval1 = [](uint32_t a, uint32_t b) -> uint32_t { return b - a; };
	auto vmval2 = [](uint32_t a, uint32_t b) -> uint32_t { return a + b; };
	auto vmval3 = [](uint32_t a, uint32_t b) -> uint32_t { return a ^ b; };
	auto vmval4 = [](uint32_t a, uint32_t b) -> uint32_t { return a - b; };

	auto enc_test = [&](uintptr_t address, uint32_t e0, uint32_t e1, auto&& op) -> uintptr_t
		{
			uint32_t low = op(static_cast<uint32_t>(address), e0);
			uint32_t high = op(static_cast<uint32_t>(address), e1);

			uintptr_t ptr =
				(static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);

			if (!ptr)
				return 0;

			if (!CheckMemory(ptr))
				return 0;

			__try
			{
				uintptr_t test = *reinterpret_cast<uintptr_t*>(ptr);
				if (!test)
					return 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return 0;
			}

			return ptr;
		};

	for (uintptr_t offset = 0; offset <= 0x1000; offset += 0x8)
	{
		uintptr_t address = sc + offset;

		if (!CheckMemory(address))
			continue;

		__try
		{
			uint32_t* encrypted = reinterpret_cast<uint32_t*>(address);
			uint32_t e0 = encrypted[0];
			uint32_t e1 = encrypted[1];

			if (e0 == 0 && e1 == 0)
				continue;

			if (uintptr_t p = enc_test(address, e0, e1, vmval1)) return reinterpret_cast<lua_State*>(p);
			if (uintptr_t p = enc_test(address, e0, e1, vmval2)) return reinterpret_cast<lua_State*>(p);
			if (uintptr_t p = enc_test(address, e0, e1, vmval3)) return reinterpret_cast<lua_State*>(p);
			if (uintptr_t p = enc_test(address, e0, e1, vmval4)) return reinterpret_cast<lua_State*>(p);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			continue;
		}
	}

	return nullptr;
}
lua_State* task_scheduler::GetLuaState(uintptr_t ScriptContext, uintptr_t Identity, uintptr_t Script)
{
	return Roblox::GetLuaStateForInstance(ScriptContext, &Identity, &Script);
}


static void ViridianLog(const char* tag, const char* fmt, ...)
{
    char bb[MAX_PATH]{};
    DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
    if (!bn || bn >= MAX_PATH) return;
    std::string bp = std::string(bb) + "\\Viridian\\boot.log";
    FILE* bf = nullptr;
    if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf) return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    fprintf(bf, "%02u:%02u:%02u [%s] ", st.wHour, st.wMinute, st.wSecond, tag);
    va_list ap; va_start(ap, fmt); vfprintf(bf, fmt, ap); va_end(ap);
    fprintf(bf, "\n");
    fclose(bf);
    ViridianRing::RefreshLogTail();
}
int ScriptsHandler(lua_State* L)
{
	static bool boundLogged = false;
	lua_State* BoundExecutorThread = static_cast<lua_State*>(
		lua_touserdata(L, lua_upvalueindex(1)));
	if (!BoundExecutorThread || BoundExecutorThread != Shared::ExploitThread)
		return 0;
	if (!boundLogged)
	{
		boundLogged = true;
		ViridianLog("handler", "ScriptsHandler bound, ticking");
	}

	Shared::ExecutionRequest Req;
	{
		std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
		if (!Shared::ExecutionRequests.empty())
		{
			Req = std::move(Shared::ExecutionRequests.front());
			Shared::ExecutionRequests.erase(Shared::ExecutionRequests.begin());
		}
	}

	if (!Req.Source.empty())
	{
		// Re-validate after the queue pop: a teleport reset between the entry
		// check and here leaves BoundExecutorThread dangling (old global
		// freed). Executing on it would fault with no log line. Same
		// behavior on the stable path (pointer still equal, executes).
		if (BoundExecutorThread != Shared::ExploitThread)
			return 0;
		ViridianLog("exec", "queue len pending, size=%zu", Req.Source.size());
		Execution::ExecuteScript(BoundExecutorThread, Req.Source);
		if (Req.FromRing)
			ViridianRing::OnRequestExecuted(Req.RingReqId);
	}

	// Drain queued RakNet hook callbacks on the Lua thread
	Raknet::ProcessCallbacks(BoundExecutorThread);

	Yielding::RunYield();

	return 0;
}


static bool InitializeGameImpl()
{
	if (Shared::IsInitializedOnGame)
		return true;

	uintptr_t DataModel = 0;
	__try { DataModel = task_scheduler::GetDataModel(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }

	if (!DataModel)
		return false;

	uintptr_t ScriptContext = 0;
	__try { ScriptContext = task_scheduler::GetScriptContext(DataModel); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }

	if (!ScriptContext)
		return false;

	lua_State* MainThread = nullptr;
	__try { MainThread = GetLuaStateForInstance(ScriptContext); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }

	if (!MainThread)
		return false;

	// Genuine init attempt (game objects exist): allow one ring-mapping retry
	// for a new DataModel value only, then mark status=injecting.
	ViridianRing::BeginInit(DataModel);

	Shared::RobloxState = MainThread;

	lua_State* newThread = nullptr;
	__try { newThread = lua_newthread(Shared::RobloxState); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Shared::RobloxState = nullptr;
		return false;
	}

	if (!newThread)
	{
		ViridianRing::OnError();
		return false;
	}

	Shared::ExploitThread = newThread;
	Shared::StateGeneration.fetch_add(1);

	__try { task_scheduler::SetThreadCapabilities(Shared::ExploitThread, 8, MaxCapabilities); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Renderer::AddConsoleLog(("ERROR"), ("WHSJ"), ("SetThreadCapabilities crashed"));
		ViridianRing::OnError();
		return false;
	}

	__try { Environment->initalizeEnvironment(Shared::ExploitThread); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Renderer::AddConsoleLog(("ERROR"), ("WHSJ"), ("initalizeEnvironment crashed"));
		ViridianRing::OnError();
		return false;
	}

	Shared::IsInitializedOnGame.store(true, std::memory_order_release);
	Shared::InitializedDataModel = DataModel;
	Shared::ScriptContext = ScriptContext;

	ViridianLog("init", "DM=%p SC=%p L=%p OK", (void*)DataModel, (void*)ScriptContext, (void*)MainThread);
	ViridianRing::OnReady(DataModel);
	Renderer::AddConsoleLog(("INFO"), ("WHSJ"), ("Initialized"));

	return true;
}

static bool InitializeGame()
{
	__try
	{
		return InitializeGameImpl();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		unsigned long code = GetExceptionCode();
		char msg[128];
		snprintf(msg, sizeof(msg), ("EXCEPTION in InitializeGame (code=0x%08lX)"), code);
		Renderer::AddConsoleLog(("ERROR"), ("WHSJ"), msg);
		OutputDebugStringA(msg);
		OutputDebugStringA(("\n"));
		Shared::IsInitializedOnGame = false;
		Shared::InitializedDataModel = 0;
		Shared::ScriptContext = 0;
		Shared::RobloxState = nullptr;
		Shared::ExploitThread = nullptr;
		ViridianRing::OnError();
		return false;
	}
}

static bool ExecuteGameImpl()
{
	lua_State* ExploitThread = Shared::ExploitThread;
	if (!ExploitThread)
		return false;

	// ViridianRing request slot: polled ONCE per scheduler tick, here — the
	// same tick-drain that services the execute.lua watcher queue below. A
	// valid request is funneled through RequestExecution (the same function
	// the watcher calls), so status/exec markers are identical on both paths.
	{
		std::string RingSrc;
		uint32_t RingId = 0;
		if (ViridianRing::PollRequestSlot(RingSrc, RingId))
			task_scheduler::RequestExecution(std::move(RingSrc), RingId);
	}

	Shared::ExecutionRequest Req;
	{
		std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
		if (!Shared::ExecutionRequests.empty())
		{
			Req = std::move(Shared::ExecutionRequests.front());
			Shared::ExecutionRequests.erase(Shared::ExecutionRequests.begin());
		}
	}

	if (!Req.Source.empty())
	{
		ViridianLog("exec", "run size=%zu", Req.Source.size());
		Execution::ExecuteScript(ExploitThread, std::move(Req.Source));
		if (Req.FromRing)
			ViridianRing::OnRequestExecuted(Req.RingReqId);
	}

	Raknet::ProcessCallbacks(ExploitThread);
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
		unsigned long code = GetExceptionCode();
		char msg[128];
		snprintf(msg, sizeof(msg), ("EXCEPTION in ExecuteGame (code=0x%08lX)"), code);
		Renderer::AddConsoleLog(("ERROR"), ("WHSJ"), msg);
		OutputDebugStringA(msg);
		OutputDebugStringA(("\n"));
		return false;
	}
}

// Per-DataModel teardown only. Process-wide pieces (execute.lua watcher,
// renderer, ExecutionRequests/TeleportQueue) stay alive so queued executions
// keep working across teleports.
static void ResetPerDataModelState()
{
	// Bump FIRST, before touching anything else: in-flight async completions
	// (HTTP yields, signals) check this generation before resuming into Lua.
	// Bumping at teardown-detect — not at successful re-init — drops
	// completions for the dead DataModel promptly instead of resuming freed
	// state if the re-init tick is delayed by a flood.
	Shared::StateGeneration.fetch_add(1);
	Shared::ExploitThread = nullptr;
	Shared::RobloxState = nullptr;
	Shared::ScriptContext = 0;
	Shared::IsInitializedOnGame.store(false, std::memory_order_release);
	// Old Lua threads die with the old DataModel; drop tracking so stale
	// pointers can never match a new thread.
	{
		std::lock_guard<std::mutex> lock(Shared::FireDispatchMutex);
		Shared::FireDispatchThreads.clear();
	}
	// NOTE: do NOT touch the old WHSJ job vtable here. On teleport the old
	// job object is freed by the game; restoring its vtable would be a
	// use-after-free write. The TP handler re-hooks the new job; our saved
	// vtable + OriginalStepFunction are reused.
}

// Drain queue:onteleport scripts into the live execution queue.
// Free function (no __try here): stepHook's body is __try, and MSVC forbids
// C++ unwindables (vector/lock_guard) directly inside it (C2712).
static void DrainTeleportQueueIntoExecution()
{
	std::vector<std::string> TeleportScripts;
	{
		std::lock_guard<std::mutex> lock(Shared::TeleportQueueMutex);
		TeleportScripts.swap(Shared::TeleportQueue);
	}
	for (std::string& Script : TeleportScripts)
		task_scheduler::RequestExecution(std::move(Script));
}

// Forward declarations: defined after HookWHJSJob (they need the full
// job-lookup chain). Called only on init/teleport paths, never on the
// steady-state tick.
static bool WHSJHookIsLive();
static bool EnsureWHSJHookForInit(uintptr_t dmForLog);

static uintptr_t stepHook(uintptr_t Job, uintptr_t Stats)
{
	__try
	{
		if (!Shared::OriginalStepFunction)
			return 0;

		if (Shared::JobStepLocked)
			return Shared::OriginalStepFunction(Job, Stats);

		// Detect DataModel change every scheduler tick so a teleport can
		// never leave us executing on a stale (freed) Lua state.
		uintptr_t curDM = 0;
		__try { curDM = task_scheduler::GetDataModel(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { curDM = 0; }

		if (!curDM)
			return Shared::OriginalStepFunction(Job, Stats);

		if (!Shared::IsInitializedOnGame || Shared::InitializedDataModel == 0)
		{
			// The hook may be absent here (first boot before TPHandler's hook
			// lands, or a teleport reset awaiting its re-hook). Creating the
			// ExploitThread without it latches a live thread nobody ticks,
			// so ensure the hook on the CURRENT job first; on failure leave
			// everything unlatched and retry next tick (TPHandler retries too).
			if (!EnsureWHSJHookForInit(curDM))
				return Shared::OriginalStepFunction(Job, Stats);
			if (!InitializeGame())
				return Shared::OriginalStepFunction(Job, Stats);
			// Keep TpHandler in sync (first boot: hook already installed by
			// TpHandler before this tick; teleport via TpHandler path clears
			// InitializedDataModel, so this branch runs and re-syncs Last).
			if (Shared::InitializedDataModel != 0)
				Shared::LastDataModel = Shared::InitializedDataModel;
		}
		else if (curDM != Shared::InitializedDataModel)
		{
			// Teleport: stop using the dead Lua state immediately.
			// Log [game] switched BEFORE the new [init] so the UI (which
			// tails boot.log) can show a brief "switching..." state.
			uintptr_t oldDM = Shared::InitializedDataModel;
			ViridianLog("game", "switched datamodel (old=%p new=%p)", (void*)oldDM, (void*)curDM);
			ViridianRing::OnSwitch(curDM);
			Renderer::AddConsoleLog(("INFO"), ("WHSJ"), ("DataModel switched, re-initializing"));
			ResetPerDataModelState();
			// This tick runs on the OLD job object (it still carries our
			// vtable — we never restore it, use-after-free otherwise), while
			// the new DataModel ticks a NEW, unhooked object. Re-hook the new
			// job BEFORE init: without this, init below latches ready on a
			// dead job, TPHandler sees the latch and skips, and nothing ever
			// ticks again. On re-hook failure leave everything unlatched so
			// both this tick and TPHandler retry; the [game] switched line
			// above already fired once and is not repeated.
			if (!EnsureWHSJHookForInit(curDM))
				return Shared::OriginalStepFunction(Job, Stats);
			// Re-run the per-DataModel init path (fresh ExploitThread + env
			// registration + per-DataModel hooks). InitializeGame appends
			// [init] ... OK on success, after the [game] switched line.
			if (!InitializeGame())
				return Shared::OriginalStepFunction(Job, Stats);
			// Sync hook progress so TpHandler (polls every 100ms) sees this
			// DataModel as handled and doesn't reset our live ExploitThread
			// or emit a duplicate [game] switched marker.
			Shared::LastDataModel = Shared::InitializedDataModel;
			// Replay queue:onteleport scripts on this path too. The
			// TpHandler path drains inside initialize_exploit; when stepHook
			// wins the race TpHandler skips entirely, so drain here.
			DrainTeleportQueueIntoExecution();
		}
		else if (!ExecuteGame())
		{
			return Shared::OriginalStepFunction(Job, Stats);
		}

		return Shared::OriginalStepFunction(Job, Stats);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		unsigned long code = GetExceptionCode();
		char msg[128];
		snprintf(msg, sizeof(msg), ("stepHook SEH: 0x%08lX"), code);
		OutputDebugStringA(msg);
		OutputDebugStringA(("\n"));
		return 0;
	}
}

static bool PatchWHJSVtable(uintptr_t whsj)
{
	if (!whsj)
	{

		return false;
	}

	__try
	{
		if (!Shared::HybridJobVtable)
		{
			uintptr_t* originalVtable = *reinterpret_cast<uintptr_t**>(whsj);
			if (!originalVtable)
			{

				return false;
			}

			Shared::OriginalHybridJobVtable = originalVtable;

			const auto newVtable = new uintptr_t[9];
			memcpy(newVtable, originalVtable, sizeof(uintptr_t) * 9);

			Shared::OriginalStepFunction = reinterpret_cast<Shared::JobStep_t>(originalVtable[8]);
			newVtable[8] = reinterpret_cast<uintptr_t>(&stepHook);

			Shared::HybridJobVtable = newVtable;
		}

		Shared::JobStepLocked = false;
		*reinterpret_cast<uintptr_t**>(whsj) = Shared::HybridJobVtable;

		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{


		return false;
	}
}

// Loose contains-match for the WHSJ fallback below. POD-only + SEH-guarded
// (C2712: no RAII inside __try). Silent on success and miss: the caller
// owns the once-per-boot miss log, and boot.log must not spam.
static uintptr_t FindJobByNameContains(const char* needle, size_t needleLen)
{
	if (!needle || needleLen == 0 || needleLen >= 0x100)
		return 0;

	static constexpr uintptr_t kNameOffsetsSub[] = { 0x18, 0xF8, 0x118 };

	__try
	{
		uintptr_t TaskScheduler =
			*reinterpret_cast<uintptr_t*>(
				Offsets::Scheduler::TaskScheduler
				);
		if (!TaskScheduler)
			return 0;

		uintptr_t JobsStart =
			*reinterpret_cast<uintptr_t*>(
				TaskScheduler + Offsets::Scheduler::JobStart
				);
		uintptr_t JobsEnd =
			*reinterpret_cast<uintptr_t*>(
				TaskScheduler + Offsets::Scheduler::JobStart + sizeof(uintptr_t)
				);
		if (!JobsStart || !JobsEnd || JobsStart >= JobsEnd)
			return 0;

		for (uintptr_t Current = JobsStart; Current < JobsEnd; Current += 0x10)
		{
			uintptr_t Job = 0;
			__try { Job = *reinterpret_cast<uintptr_t*>(Current); }
			__except (EXCEPTION_EXECUTE_HANDLER) { continue; }
			if (!Job)
				continue;

			for (size_t oi = 0; oi < sizeof(kNameOffsetsSub) / sizeof(kNameOffsetsSub[0]); ++oi)
			{
				uintptr_t off = kNameOffsetsSub[oi];
				if (!CheckMemory(Job + off))
					continue;

				const char* candidate = nullptr;
				__try { candidate = *reinterpret_cast<const char**>(Job + off); }
				__except (EXCEPTION_EXECUTE_HANDLER) { continue; }
				if (!candidate)
					continue;

				uintptr_t candAddr = (uintptr_t)candidate;
				if (candAddr < 0x10000 || candAddr > 0x7FFFFFFFFFFF)
					continue;
				if (!CheckMemory(candAddr))
					continue;

				// Bounded copy under SEH so the search below never touches game memory.
				char buf[128];
				__try
				{
					size_t i = 0;
					for (; i < sizeof(buf) - 1; ++i)
					{
						char c = candidate[i];
						buf[i] = c;
						if (c == '\0')
							break;
					}
					buf[sizeof(buf) - 1] = '\0';
				}
				__except (EXCEPTION_EXECUTE_HANDLER) { continue; }

				size_t bufLen = 0;
				while (bufLen < sizeof(buf) - 1 && buf[bufLen] != '\0')
					++bufLen;
				if (bufLen < needleLen)
					continue;
				for (size_t s = 0; s + needleLen <= bufLen; ++s)
				{
					size_t k = 0;
					for (; k < needleLen; ++k)
					{
						if (buf[s + k] != needle[k])
							break;
					}
					if (k == needleLen)
						return Job;
				}
			}
		}
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

bool task_scheduler::HookWHJSJob()
{
	

	uintptr_t whsj = GetJobByTypeName(("WaitingHybridScriptsJob(Default;Ugc)"));
	// Fallback spellings for games with different job sets: the exact string
	// above is a prefix match; try the bare prefix, then a contains-match,
	// before giving up for the tick. Silent on success (no boot.log) to keep
	// retry logging throttled; a miss is logged once-per-boot by the caller.
	if (!whsj)
		whsj = GetJobByTypeName(("WaitingHybridScriptsJob"));
	if (!whsj)
	{
		static const char kNeedle[] = "WaitingHybridScriptsJob";
		whsj = FindJobByNameContains(kNeedle, sizeof(kNeedle) - 1);
	}
	if (!whsj)
	{
		return false;
	}




	Shared::LastWaitingHybridScriptsJob = whsj;

	if (!PatchWHJSVtable(whsj))
	{
	
		return false;
	}



	return true;
}

void task_scheduler::UnhookWHJSJob()
{
	__try
	{
		// Never touch the old job object here: on teleport it may already be
		// freed by the game, so restoring its vtable would be a use-after-free
		// write that can corrupt the heap and wedge the client mid-teleport.
		// Just stop stepping through our hook; the saved vtable +
		// OriginalStepFunction are reused to patch the new job on re-hook, and
		// a still-ticking old job safely passes through to the original step.
		Shared::JobStepLocked = true;

		Shared::LastWaitingHybridScriptsJob = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		
	}
}


// Fresh-probe hook liveness: the CURRENT job-list object must be the one we
// patched, carrying our vtable with the step gate open. The old job keeps
// our vtable pointer after a teleport (we never restore it), so checking the
// saved pointer alone is NOT enough — a fresh lookup is required. No locks,
// no Lua, no allocation; callers run this only on init/teleport paths (never
// on the steady-state tick), so the scan cost stays inside boot/loading/
// switch windows. Throttled logging lives in the Ensure wrapper below.
// POD-only verifier (no RAII inside: C2712 forbids unwindables lexically in a
// __try function). Reads the candidate job's vtable pointer + gate under SEH.
static bool VerifyHookOnJob(uintptr_t cur)
{
	__try
	{
		if (cur != Shared::LastWaitingHybridScriptsJob)
			return false;
		if (!Shared::HybridJobVtable)
			return false;
		uintptr_t* v = *reinterpret_cast<uintptr_t**>(cur);
		if (v != Shared::HybridJobVtable)
			return false;
		if (Shared::JobStepLocked.load(std::memory_order_acquire))
			return false;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static bool WHSJHookIsLive()
{
	// Lookup stays OUTSIDE any __try (string temporaries would trip C2712);
	// GetJobByTypeName guards itself internally. Verification above is the
	// only part under SEH.
	uintptr_t cur = task_scheduler::GetJobByTypeName(("WaitingHybridScriptsJob(Default;Ugc)"));
	if (!cur)
		cur = task_scheduler::GetJobByTypeName(("WaitingHybridScriptsJob"));
	if (!cur)
	{
		static const char kNeedle[] = "WaitingHybridScriptsJob";
		cur = FindJobByNameContains(kNeedle, sizeof(kNeedle) - 1);
	}
	if (!cur)
		return false;
	return VerifyHookOnJob(cur);
}

bool task_scheduler::IsWHSJHookLive()
{
	return WHSJHookIsLive();
}

// Ensure the hook is live before any ExploitThread is created. Idempotent:
// a live hook costs one lookup and no log line. Misses log once per
// DataModel ("[tp] ..." via ViridianLog); a success that follows a miss for
// the same DataModel logs one recovery line so boot.log proves the post-swap
// re-hook actually re-ran and succeeded. No unbounded waits: failure just
// returns false and both the tick and TPHandler retry.
static bool EnsureWHSJHookForInit(uintptr_t dmForLog)
{
	static uintptr_t s_missDM = 0;
	if (WHSJHookIsLive())
	{
		if (s_missDM != 0 && s_missDM == dmForLog)
		{
			ViridianLog("tp", "WHSJ re-hooked for DM=%p after miss", (void*)dmForLog);
			s_missDM = 0;
		}
		return true;
	}
	if (!task_scheduler::HookWHJSJob() || !WHSJHookIsLive())
	{
		if (s_missDM != dmForLog)
		{
			s_missDM = dmForLog;
			ViridianLog("tp", "WHSJ hook not live for DM=%p, retrying in background", (void*)dmForLog);
		}
		return false;
	}
	if (s_missDM != 0 && s_missDM == dmForLog)
	{
		ViridianLog("tp", "WHSJ re-hooked for DM=%p after miss", (void*)dmForLog);
		s_missDM = 0;
	}
	return true;
}

// SEH-safe core: every step wrapped individually.
static bool initialize_whsjhook(uintptr_t DataModel)
{

	__try
	{
		if (!task_scheduler::HookWHJSJob())
		{
			ReportSchedulerError(("failed to hook WaitingHybridScriptsJob"));
		// First-boot hook misses are otherwise invisible in boot.log:
		// ReportSchedulerError only reaches the in-game console +
		// OutputDebugString, and TpHandler logs nothing on first boot
		// (no teleport marker). Throttled to once per DataModel: TpHandler
		// retries every 100ms, and a teleport lands a NEW DataModel whose
		// miss must be visible too (once-per-boot would swallow it).
		static uintptr_t hookMissDM = 0;
		if (hookMissDM != DataModel)
		{
			hookMissDM = DataModel;
			ViridianLog("tp", "WaitingHybridScriptsJob not found, retrying in background");
		}
			return false;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		unsigned long code = GetExceptionCode();
	
		ReportSchedulerError(("EXCEPTION in HookWHJSJob"));
		return false;
	}


	return true;
}
bool task_scheduler::initialize_exploit(uintptr_t DataModel)
{



	if (DataModel)
		ViridianRing::BeginInit(DataModel);

	if (!initialize_whsjhook(DataModel))
	{
		ViridianRing::OnError();
		return false;
	}



	LastSchedulerError.clear();


	if (Shared::LastDataModel)
	{
		std::vector<std::string> TeleportScripts;
		{
			std::lock_guard<std::mutex> lock(Shared::TeleportQueueMutex);
			TeleportScripts.swap(Shared::TeleportQueue);
		}
		for (std::string& Script : TeleportScripts)
			task_scheduler::RequestExecution(std::move(Script));
	}
	return true;
}

// Bound the execution queue: a flood (or a tick starved by one) must shed
// load with a log line instead of growing memory until death. Ring requests
// are never evicted while a non-ring script can take the hit, so UI-queued
// executions still run after teleport.
// REQUIRES: caller holds Shared::ExecutionRequestsMutex.
static constexpr size_t kMaxExecutionRequests = 256;
static bool EvictOldestExecutionRequestLocked()
{
	auto& q = Shared::ExecutionRequests;
	if (q.size() < kMaxExecutionRequests)
		return false;
	size_t idx = 0;
	while (idx < q.size() && q[idx].FromRing)
		++idx;
	if (idx >= q.size())
		idx = 0;
	q.erase(q.begin() + idx);
	return true;
}

void task_scheduler::RequestExecution(std::string Script)
{
	bool dropped = false;
	{
		std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
		if (EvictOldestExecutionRequestLocked())
			dropped = true;
		Shared::ExecutionRequest Req;
		Req.Source = std::move(Script);
		Shared::ExecutionRequests.push_back(std::move(Req));
	}
	if (dropped)
		ViridianLog("exec", "queue full, dropped oldest");
}

void task_scheduler::RequestExecution(std::string Script, uint32_t RingReqId)
{
	bool dropped = false;
	{
		std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
		if (EvictOldestExecutionRequestLocked())
			dropped = true;
		Shared::ExecutionRequest Req;
		Req.Source = std::move(Script);
		Req.RingReqId = RingReqId;
		Req.FromRing = true;
		Shared::ExecutionRequests.push_back(std::move(Req));
	}
	if (dropped)
		ViridianLog("exec", "queue full, dropped oldest");
}

uintptr_t task_scheduler::GetJobByTypeName(const std::string& TypeName)
{
	// Hoist non-POD access outside SEH: __try bodies below stay POD-only
	// (C2712 forbids RAII with destructors inside __try).
	const char* want = TypeName.c_str();
	const size_t nameLen = TypeName.size();
	if (nameLen == 0 || nameLen >= 0x100)
		return 0;

	static constexpr uintptr_t kNameOffsets[] = { 0x18, 0xF8, 0x118 };

	__try
	{
		uintptr_t TaskScheduler =
			*reinterpret_cast<uintptr_t*>(
				Offsets::Scheduler::TaskScheduler
				);

		if (!TaskScheduler)
			return 0;

		uintptr_t JobsStart =
			*reinterpret_cast<uintptr_t*>(
				TaskScheduler + Offsets::Scheduler::JobStart
				);

		uintptr_t JobsEnd =
			*reinterpret_cast<uintptr_t*>(
				TaskScheduler +
				Offsets::Scheduler::JobStart +
				sizeof(uintptr_t)
				);

		if (!JobsStart || !JobsEnd || JobsStart >= JobsEnd)
			return 0;

		for (uintptr_t Current = JobsStart;
			Current < JobsEnd;
			Current += 0x10)
		{
			uintptr_t Job = 0;
			__try { Job = *reinterpret_cast<uintptr_t*>(Current); }
			__except (EXCEPTION_EXECUTE_HANDLER) { continue; }

			if (!Job)
				continue;

			for (size_t oi = 0; oi < sizeof(kNameOffsets) / sizeof(kNameOffsets[0]); ++oi)
			{
				uintptr_t off = kNameOffsets[oi];
				if (!CheckMemory(Job + off))
					continue;

				const char* candidate = nullptr;
				__try { candidate = *reinterpret_cast<const char**>(Job + off); }
				__except (EXCEPTION_EXECUTE_HANDLER) { continue; }
				if (!candidate)
					continue;

				uintptr_t candAddr = (uintptr_t)candidate;
				if (candAddr < 0x10000 || candAddr > 0x7FFFFFFFFFFF)
					continue;

				if (!CheckMemory(candAddr))
					continue;

				bool match = false;
				__try { match = (memcmp(candidate, want, nameLen) == 0); }
				__except (EXCEPTION_EXECUTE_HANDLER) { continue; }
				if (match)
					return Job;
			}
		}

		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// Transient AV during scheduler/job-list deref: report not-found so
		// the caller retries cleanly next tick. Never propagates, never poisons.
		return 0;
	}
}

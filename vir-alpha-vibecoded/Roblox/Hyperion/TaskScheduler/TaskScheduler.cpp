#include "TaskScheduler.hpp"

#include <Roblox/Shared/Memory.hpp>
#include <Roblox/Hyperion/Communication/BootLog.hpp>

uintptr_t TaskScheduler::GetDatamodel()
{

	if (!Memory::CanRead(Offsets::DataModel::FakeDataModelPointer, sizeof(uintptr_t)))
		return 0;

	const uintptr_t cotch1 = *reinterpret_cast<uintptr_t*>(Offsets::DataModel::FakeDataModelPointer);
	if (!Memory::CanRead(cotch1 + Offsets::DataModel::FakeDataModelToDataModel, sizeof(uintptr_t)))
		return 0;

	return *reinterpret_cast<uintptr_t*>(cotch1 + Offsets::DataModel::FakeDataModelToDataModel);
}

uintptr_t TaskScheduler::GetScriptContext(uintptr_t DataModel)
{

	if (!DataModel || !Utils::CheckMemory(DataModel))
		return 0;
	
	__try
	{
		uintptr_t Children = *reinterpret_cast<uintptr_t*>(DataModel + Offsets::DataModel::Children);
		if (!Children)
			return 0;
		uintptr_t FirstChild = *reinterpret_cast<uintptr_t*>(Children);
		if (!FirstChild)
			return 0;
		uintptr_t Result = *reinterpret_cast<uintptr_t*>(FirstChild + Offsets::DataModel::ScriptContext);
		// One-shot probe: if init dies silently, this names the byte of the
		// datamodel chain that broke on this client build.
		static LONG s_probeLogged = 0;
		if (InterlockedCompareExchange(&s_probeLogged, 1, 0) == 0)
		{
			char Msg[160]{};
			snprintf(Msg, sizeof(Msg), "ctx probe dm=%llx children=%llx first=%llx sc=%llx",
				static_cast<unsigned long long>(DataModel),
				static_cast<unsigned long long>(Children),
				static_cast<unsigned long long>(FirstChild),
				static_cast<unsigned long long>(Result));
			BootLog::WriteLine("init", Msg);
		}
		return Result;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}

	return 0;
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

// Layout hypotheses under test for this build. The repo headers (0.743 dump
// annotations) and the luau fork source disagree on field order:
//   hypothesis "repo": tt@0, lua_State::global @ 0x18, mainthread @ 0x2E8
//   hypothesis "fork": tt@0, lua_State::global @ 0x50, mainthread @ 0x60
// Only the live client can referee, so the scan tries BOTH and the first
// match reports which one won. Returns 0 = not a state, -1 = thread-tagged
// but neither layout matched, 1 = repo, 2 = fork.
static int MatchLuaStateLayout(uintptr_t ptr)
{
	if (!ptr || !CheckMemory(ptr))
		return 0;

	__try
	{
		if (*reinterpret_cast<uint8_t*>(ptr) != LUA_TTHREAD)
			return 0;

		{
			uintptr_t Global = *reinterpret_cast<uintptr_t*>(ptr + 0x18);
			if (Global && CheckMemory(Global) && *reinterpret_cast<uintptr_t*>(Global + 0x2E8) == ptr)
				return 1;
		}

		{
			uintptr_t Global = *reinterpret_cast<uintptr_t*>(ptr + 0x50);
			if (Global && CheckMemory(Global))
			{
				uintptr_t Main = *reinterpret_cast<uintptr_t*>(Global + 0x60);
				if (Main == ptr)
					return 2;
				// Secondary threads (coroutines) share the global; accept them
				// too when the global's mainthread is itself a consistent thread.
				if (Main && CheckMemory(Main) && *reinterpret_cast<uint8_t*>(Main) == LUA_TTHREAD &&
					*reinterpret_cast<uintptr_t*>(Main + 0x50) == Global)
					return 3;
			}
		}

		return -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

lua_State* TaskScheduler::FindStateInJob(uintptr_t Job)
{
	if (!Job || !CheckMemory(Job))
		return nullptr;

	// The WaitingHybridScriptsJob references its Lua thread with a plain
	// pointer somewhere in its object head. Walk it for a value that passes
	// the lua_State layout identity (fork layout: global@0x50 and the
	// global's mainthread back-reference at 0x60, or a coroutine that shares
	// that global). One guarded read pass - a fault on an unreadable page
	// stops the walk instead of VirtualQuery-ing every slot.
	const uintptr_t EngineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

	for (uintptr_t offset = 0; offset <= 0x2000; offset += 0x8)
	{
		uintptr_t candidate = 0;
		__try
		{
			candidate = *reinterpret_cast<uintptr_t*>(Job + offset);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			break;
		}

		if (MatchLuaStateLayout(candidate) > 0 && TaskScheduler::StateAlive(candidate, 0, EngineBase))
		{
			static LONG s_jobLogged = 0;
			if (InterlockedCompareExchange(&s_jobLogged, 1, 0) == 0)
			{
				char Msg[128]{};
				snprintf(Msg, sizeof(Msg), "job state job=%llx off=0x%llx ptr=%llx",
					static_cast<unsigned long long>(Job),
					static_cast<unsigned long long>(offset),
					static_cast<unsigned long long>(candidate));
				BootLog::WriteLine("init", Msg);
			}
			return reinterpret_cast<lua_State*>(candidate);
		}
	}

	return nullptr;
}

bool TaskScheduler::StateAlive(uintptr_t L, uintptr_t sc, uintptr_t EngineBase)
{
	bool Live = false;
	__try
	{
		const uintptr_t Glob = *reinterpret_cast<uintptr_t*>(L + 0x50);
		const uintptr_t Esp = *reinterpret_cast<uintptr_t*>(L + 0x8);
		uintptr_t Frealloc = 0;
		uintptr_t RobloxShared = 0;
		uintptr_t ChainSc = 0;
		if (Glob)
			Frealloc = *reinterpret_cast<uintptr_t*>(Glob + 0x38);
		if (Esp)
			RobloxShared = *reinterpret_cast<uintptr_t*>(Esp + 0x18);
		if (RobloxShared)
			ChainSc = *reinterpret_cast<uintptr_t*>(RobloxShared + 0x30);

		Live = Glob != 0 && Esp != 0 && RobloxShared != 0
			&& Frealloc >= EngineBase && Frealloc < EngineBase + 0x10000000
			&& (sc == 0 || ChainSc == sc);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Live = false;
	}
	return Live;
}

namespace
{
	// One remembered hit: the full scan is expensive (guarded reads over
	// 0x1000 bytes) and the state is stable for the VM's lifetime, so retries
	// after a failed environment build must not rescan. Re-verified cheaply
	// on every use and dropped when the VM goes away.
	struct
	{
		LONG Valid = 0;
		uintptr_t Base = 0;
		uintptr_t Offset = 0;
		uintptr_t Value = 0;
	} s_StateCache;
}

lua_State* TaskScheduler::GetLuaStateForInstance(uintptr_t sc)
{
	if (!sc)
		return nullptr;

	__try
	{
		*reinterpret_cast<BOOLEAN*>(sc + Offsets::ScriptContext::RequireBypass) = TRUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}

	const uintptr_t EngineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

	if (s_StateCache.Valid && s_StateCache.Base == sc)
	{
		if (MatchLuaStateLayout(s_StateCache.Value) > 0 && TaskScheduler::StateAlive(s_StateCache.Value, sc, EngineBase))
			return reinterpret_cast<lua_State*>(s_StateCache.Value);
		s_StateCache.Valid = 0;
	}

	auto vmval1 = [](uint32_t a, uint32_t b) -> uint32_t { return b - a; };
	auto vmval2 = [](uint32_t a, uint32_t b) -> uint32_t { return a + b; };
	auto vmval3 = [](uint32_t a, uint32_t b) -> uint32_t { return a ^ b; };
	auto vmval4 = [](uint32_t a, uint32_t b) -> uint32_t { return a - b; };

	uint32_t Tagged = 0;

	auto consider = [&](uintptr_t candidate, uintptr_t offset, int shape) -> lua_State*
	{
		const int Match = MatchLuaStateLayout(candidate);
		if (Match == 0)
			return nullptr;
		if (Match == -1)
		{
			Tagged++;
			return nullptr;
		}

		{
			static LONG s_matchCount = 0;
			const LONG n = InterlockedIncrement(&s_matchCount);
			if (n <= 8)
			{
				char Msg[160]{};
				snprintf(Msg, sizeof(Msg), "state match offset=0x%llx shape=%d layout=%s ptr=%llx",
					static_cast<unsigned long long>(offset), shape, Match == 1 ? "repo" : (Match == 2 ? "fork" : "fork-weak"),
					static_cast<unsigned long long>(candidate));
				BootLog::WriteLine("init", Msg);
			}
		}

		// Dying threads and freed-VM debris pass the identity bytes but fail
		// the liveness gate. Reject and KEEP SCANNING - the first match is
		// often transient debris from a join/teleport VM rebuild.
		if (!TaskScheduler::StateAlive(candidate, sc, EngineBase))
		{
			static LONG s_rejCount = 0;
			const LONG n = InterlockedIncrement(&s_rejCount);
			if (n <= 4 || (n & 31) == 0)
			{
				char Msg[160]{};
				snprintf(Msg, sizeof(Msg), "candidate rejected off=0x%llx ptr=%llx (identity ok, not live)",
					static_cast<unsigned long long>(offset),
					static_cast<unsigned long long>(candidate));
				BootLog::WriteLine("init", Msg);
			}
			return nullptr;
		}

		s_StateCache.Base = sc;
		s_StateCache.Offset = offset;
		s_StateCache.Value = candidate;
		s_StateCache.Valid = 1;
		return reinterpret_cast<lua_State*>(candidate);
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

			auto decode = [&](auto&& op) -> uintptr_t
			{
				uint32_t low = op(static_cast<uint32_t>(address), e0);
				uint32_t high = op(static_cast<uint32_t>(address), e1);
				if (!low && !high)
					return 0;
				return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
			};

			if (lua_State* p = consider(decode(vmval1), offset, 1)) return p;
			if (lua_State* p = consider(decode(vmval2), offset, 2)) return p;
			if (lua_State* p = consider(decode(vmval3), offset, 3)) return p;
			if (lua_State* p = consider(decode(vmval4), offset, 4)) return p;

			// The state pointer may also be stored unencoded; test the raw
			// 8 bytes as-is.
			uintptr_t raw = (static_cast<uint64_t>(e1) << 32) | e0;
			if (lua_State* p = consider(raw, offset, 5)) return p;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			continue;
		}
	}

	{
		static LONG s_missCount = 0;
		const LONG n = InterlockedIncrement(&s_missCount);
		if (n <= 4 || (n & 15) == 0)
		{
			char Msg[128]{};
			snprintf(Msg, sizeof(Msg), "scan exhausted 0..0x1000: %u thread-tagged candidate(s), no layout match",
				static_cast<unsigned>(Tagged));
			BootLog::WriteLine("init", Msg);
		}
	}

	return nullptr;
}

lua_State* TaskScheduler::GetLuaState(uintptr_t ScriptContext, uintptr_t Identity, uintptr_t Script)
{
	if (!ScriptContext || !Utils::CheckMemory(ScriptContext))
		return nullptr;

	__try
	{
		return Hyperion::GetLuaStateForInstance(ScriptContext, &Identity, &Script);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

int TaskScheduler::GetGameLoaded(uintptr_t Datamodel)
{

	if (!Datamodel || !Utils::CheckMemory(Datamodel + Offsets::DataModel::GameLoaded)) {
		return 0;
	}

	return *reinterpret_cast<int*>(Datamodel + Offsets::DataModel::GameLoaded);
}

uintptr_t TaskScheduler::GetJobByName(const std::string& Name)
{

	uintptr_t Scheduler = *reinterpret_cast<uintptr_t*>(Offsets::TaskScheduler::TaskSchedulerPtr);
	if (!Scheduler || !Utils::CheckMemory(Scheduler + 0x200))
		return 0;

	uintptr_t JobsStart = *reinterpret_cast<uintptr_t*>(Scheduler + Offsets::TaskScheduler::JobStart);
	uintptr_t JobsEnd = *reinterpret_cast<uintptr_t*>(Scheduler + Offsets::TaskScheduler::JobStart + sizeof(uintptr_t));
	if (!JobsStart || JobsStart >= JobsEnd || JobsEnd - JobsStart > 0x100000)
		return 0;

	for (uintptr_t Job = JobsStart; Job < JobsEnd; Job += 0x10)
	{
		uintptr_t JobPtr = *reinterpret_cast<uintptr_t*>(Job);
		if (!JobPtr || !Utils::CheckMemory(JobPtr + Offsets::TaskScheduler::JobName + 0x20))
			continue;

		const uintptr_t nameAddr = JobPtr + Offsets::TaskScheduler::JobName;
		const size_t len = *reinterpret_cast<size_t*>(nameAddr + 0x10);
		if (len >= 0x100)
			continue;

		const char* data = len < 0x10
			? reinterpret_cast<const char*>(nameAddr)
			: *reinterpret_cast<const char**>(nameAddr);
		if (!data || !Memory::CanRead(reinterpret_cast<uintptr_t>(data), len ? len : 1))
			continue;

		if (len == Name.size() && memcmp(data, Name.data(), len) == 0)
			return JobPtr;
	}

	return 0;
}

uintptr_t TaskScheduler::GetJobByTypeName(const std::string& TypeName)
{
	

	uintptr_t TaskScheduler =
		*reinterpret_cast<uintptr_t*>(
			Offsets::TaskScheduler::TaskSchedulerPtr
			);

	if (!TaskScheduler)
		return 0;

	uintptr_t JobsStart =
		*reinterpret_cast<uintptr_t*>(
			TaskScheduler + Offsets::TaskScheduler::JobStart
			);

	uintptr_t JobsEnd =
		*reinterpret_cast<uintptr_t*>(
			TaskScheduler +
			Offsets::TaskScheduler::JobStart +
			sizeof(uintptr_t)
			);

	if (!JobsStart || !JobsEnd || JobsStart >= JobsEnd)
		return 0;

	for (uintptr_t Current = JobsStart;
		Current < JobsEnd;
		Current += 0x10)
	{
		uintptr_t Job =
			*reinterpret_cast<uintptr_t*>(Current);

		if (!Job)
			continue;

		const char* JobTypeName =
			*reinterpret_cast<const char**>(
				Job + 0xF8
				);

		if (!JobTypeName)
			continue;

		if (strcmp(JobTypeName, TypeName.c_str()) == 0)
			return Job;
	}

	return 0;
}

void TaskScheduler::SetProtoCapabilities(Proto* Root, uintptr_t* Capabilities)
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

void TaskScheduler::SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities)
{

	if (!L || !L->userdata)
		return;

	L->userdata->identity = Level;
	L->userdata->capabilities = Capabilities;
}

void TaskScheduler::SetIdentity(lua_State* Thread, int Identity, bool IsInstance)
{

	if (!Thread || !Thread->userdata)
		return;

	rbxextraspace* basedData = reinterpret_cast<rbxextraspace*>(Thread->userdata);
	basedData->identity = Identity;
	basedData->capabilities = Hyperion::GetCapabilities(&Identity);

	if (IsInstance)
	{
		uintptr_t IdentityPointer = *reinterpret_cast<uintptr_t*>(Offsets::Identity::Identity);
		if (!IdentityPointer || IdentityPointer > 0xFFFFFFFFFFFF)
			return;

		uintptr_t IdentityStructure = Hyperion::GetIdentityStruct(IdentityPointer);
		if (!IdentityStructure)
			return;

		*reinterpret_cast<int32_t*>(IdentityStructure) = Identity;
		*reinterpret_cast<uintptr_t*>(IdentityStructure + Offsets::Capabilities::Capabilities) = Hyperion::GetCapabilities(&Identity);
	}
}
void TaskScheduler::RequestExecution(std::string Script, uint32_t ReqId)
{
	std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
	Shared::ExecutionRequests.push_back(std::move(Script));
	Shared::ExecutionRequestIds.push_back(ReqId);
}
void TaskScheduler::SendScript(const std::string& Script, uint32_t ReqId)
{

	std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
	Shared::ExecutionRequests.push_back(Script);
	Shared::ExecutionRequestIds.push_back(ReqId);
}
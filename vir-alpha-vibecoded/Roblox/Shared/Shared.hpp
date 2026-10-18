#pragma once

#include <atomic>

#include <Windows.h>
#include <lua.h>
#include <queue>
#include <functional>
#include <filesystem>
#include <zstd.h>
#include <xxhash.h>
#include <lualib.h>
#include <mutex>
#include <Dependencies/Luau/VM/src/lgc.h>
#include <Dependencies/Luau/VM/src/lmem.h>
#include <unordered_set>
#include <Dependencies/Luau/VM/src/lapi.h>
#include <map>
#include <others/imgui_notify.h>
#include <Roblox/Environment/Environment.hpp>

#define lua_normalisestack(L, mxs) { if (lua_gettop(L) > mxs) lua_settop(L, mxs); }
#define luaL_trimstack(L, n) int top = lua_gettop(L); if (top > n || top < 0) lua_settop(L, n)
#define lua_pushoclosure(L, C) setclvalue(L, L->top, C); incr_top(L)
#define lua_toclosure(L, i) (Closure*)lua_topointer(L, i)

inline uintptr_t MaxCapabilities = 0xFFFFFFFFFFFFFFFF;

namespace Shared {
	inline lua_State* VoltState;
	inline lua_State* RobloxState;
	inline uintptr_t JobState = 0; // lua_State* harvested from the WHSJ job object; init falls back to it

	inline uintptr_t lastKnownDatamodel = 0;

	inline int RobloxPcallRef = LUA_NOREF;

	inline std::queue<std::function<void()>> YieldQueue;
	inline std::mutex YieldMutex;

	inline std::vector<std::string> ExecutionRequests;
	inline std::vector<uint32_t> ExecutionRequestIds; // parallel to ExecutionRequests; 0 = no status-ring echo
	inline std::mutex ExecutionRequestsMutex;
	inline std::vector<std::string> TeleportQueue;
	inline std::mutex TeleportQueueMutex;
    inline std::atomic<uint64_t> StateGeneration{ 0 };
	inline std::map<Closure*, Closure*> newcclosure_map;
	inline std::map<Closure*, Closure*> hooks_map;
	inline static std::unordered_set<Closure*> WrappedClosures;
	inline static std::unordered_set<Closure*> ExecutorClosures;
	inline std::vector<Closure*> FunctionVector;

	inline bool isInitializedOnGame = false;
	inline bool lockedStep = true;
	inline uintptr_t lastWHSJ = 0;
	inline void* ourVFtable = 0;

	using JobStep_t = uintptr_t(__fastcall*)(uintptr_t Job, uintptr_t Stats);
	inline JobStep_t originalStepFunction = nullptr;
	// Base the manual mapper gave this image; discovered during unwind-table
	// registration, used to attribute crash addresses to our RVAs.
	inline uintptr_t ModuleBase = 0;

	inline std::filesystem::path workspace_folder;
    inline std::filesystem::path autoexecute_folder;

}

namespace Utils {

    inline void TrackClosure(Closure* c)
    {
        if (!c) return;
        Shared::ExecutorClosures.insert(c);

        for (Closure* existing : Environment::function_array)
        {
            if (existing == c)
                return;
        }
        Environment::function_array.push_back(c);
    }


    inline void AddFunction(lua_State* L, const char* Name, lua_CFunction Function)
    {
        lua_pushcclosure(L, Function, nullptr, 0);
        TrackClosure(clvalue(index2addr(L, -1)));
        lua_setglobal(L, Name);
    
    }

    inline void AddTableFunction(lua_State* L, const char* Name, lua_CFunction Function)
    {
        lua_pushcclosure(L, Function, nullptr, 0);
        TrackClosure(clvalue(index2addr(L, -1)));
        lua_setfield(L, -2, Name);
    }

    inline bool CheckMemory(uintptr_t address) {
        if (address < 0x10000 || address > 0x7FFFFFFFFFFF) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.Protect & PAGE_NOACCESS || mbi.State != MEM_COMMIT) {
            return false;
        }

        return true;
    }
}
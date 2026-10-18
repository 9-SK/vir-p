#pragma once

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include <Windows.h>
#include <lua.h>
#include <lualib.h>
#include <Dependencies/Luau/VM/src/lstate.h>
#include <Dependencies/Luau/VM/src/lobject.h>
#include <Dependencies/Luau/VM/src/lgc.h>
#include <Dependencies/Luau/VM/src/lmem.h>
#include <Dependencies/Luau/VM/src/lapi.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Environment/Environment.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>

namespace LuaStateProxy
{
    struct ProxyData
    {
        lua_State* state;
        int id;
        bool isActorState;
        std::uint64_t generation;
    };

    inline int L_Proxy_Index(lua_State* L);
    inline int L_Proxy_Execute(lua_State* L);
    inline int L_Proxy_GetActors(lua_State* L);
}

namespace LuaStateProxy
{
    inline std::atomic<int> g_NextId{ 1 };
    inline const char* kProxyMeta = "LuaStateProxy";
    inline const char* kEventsRegistry = "LuaStateProxy_Events";
    inline const char* kAnchorRegistry = "LuaStateProxy_Anchors";
}
namespace LuaStateProxy
{
    inline ProxyData* GetProxy(lua_State* L, int idx)
    {
        if (!lua_isuserdata(L, idx))
            return nullptr;
        void* ud = lua_touserdata(L, idx);
        if (!ud) return nullptr;

        if (!lua_getmetatable(L, idx))
            return nullptr;
        luaL_getmetatable(L, kProxyMeta);
        bool ok = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 2);
        return ok ? static_cast<ProxyData*>(ud) : nullptr;
    }
    inline bool IsProxyUsable(const ProxyData* pd)
    {
        if (!pd || !pd->state) return false;
        if (pd->generation != Shared::StateGeneration.load(std::memory_order_relaxed))
            return false;
        if (!Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(pd->state)))
            return false;
        return true;
    }

    inline void EnsureMeta(lua_State* L)
    {
        luaL_getmetatable(L, kProxyMeta);
        if (!lua_isnil(L, -1)) { lua_pop(L, 1); return; }
        lua_pop(L, 1);

        luaL_newmetatable(L, kProxyMeta);

        lua_pushcfunction(L, L_Proxy_Index, "__index");
        lua_setfield(L, -2, "__index");

        lua_pushstring(L, "LuaStateProxy");
        lua_setfield(L, -2, "__type");
        lua_pushstring(L, "The metatable is locked");
        lua_setfield(L, -2, "__metatable");

        lua_pop(L, 1);
    }
    inline void AnchorState(lua_State* L, lua_State* target)
    {
        if (!target) return;
        if (target == L->global->mainthread) return;

        if (!lua_checkstack(L, 4)) return;

        lua_getfield(L, LUA_REGISTRYINDEX, kAnchorRegistry);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, kAnchorRegistry);
        }

        lua_pushlightuserdata(L, target);
        setthvalue(L, L->top, target);
        L->top++;

        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    inline void PushProxy(lua_State* L, lua_State* target, bool isActor)
    {
        EnsureMeta(L);
        AnchorState(L, target);

        ProxyData* pd = static_cast<ProxyData*>(lua_newuserdata(L, sizeof(ProxyData)));
        pd->state = target;
        pd->id = g_NextId.fetch_add(1, std::memory_order_relaxed);
        pd->isActorState = isActor;
        pd->generation = Shared::StateGeneration.load(std::memory_order_relaxed);

        luaL_getmetatable(L, kProxyMeta);
        lua_setmetatable(L, -2);
    }
    inline void GetProxyEvent(lua_State* L, int proxyIdx)
    {
        ProxyData* pd = GetProxy(L, proxyIdx);
        if (!pd) { lua_pushnil(L); return; }

        if (!lua_checkstack(L, 6)) { lua_pushnil(L); return; }

        lua_getfield(L, LUA_REGISTRYINDEX, kEventsRegistry);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, kEventsRegistry);
        }

        lua_pushlightuserdata(L, pd->state);
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1))
        {
            lua_remove(L, -2);
            return;
        }
        lua_pop(L, 1);
        lua_getglobal(L, "Signal");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_getglobal(L, "Instance");
            if (!lua_istable(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return; }

            lua_getfield(L, -1, "new");
            lua_remove(L, -2);
            if (!lua_isfunction(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return; }

            lua_pushstring(L, "BindableEvent");
            if (lua_pcall(L, 1, 1, 0) != LUA_OK || lua_isnil(L, -1))
            {
                lua_pop(L, 2);
                lua_pushnil(L);
                return;
            }
        }
        else
        {
            lua_getfield(L, -1, "new");
            lua_remove(L, -2);
            if (!lua_isfunction(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return; }

            if (lua_pcall(L, 0, 1, 0) != LUA_OK || lua_isnil(L, -1))
            {
                lua_pop(L, 2);
                lua_pushnil(L);
                return;
            }
        }
        lua_pushlightuserdata(L, pd->state);
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
        lua_remove(L, -2);
    }

    inline void deepCopy(lua_State* src, lua_State* dst, int idx)
    {
        if (!lua_checkstack(dst, 4)) { lua_pushnil(dst); return; }

        int absIdx = lua_absindex(src, idx);
        switch (lua_type(src, absIdx))
        {
        case LUA_TTABLE: {
            lua_newtable(dst);
            lua_pushnil(src);
            while (lua_next(src, absIdx)) {
                deepCopy(src, dst, -2);
                deepCopy(src, dst, -1);
                lua_settable(dst, -3);
                lua_pop(src, 1);
            }
            break;
        }
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(src, absIdx, &len);
            lua_pushlstring(dst, s, len);
            break;
        }
        case LUA_TNUMBER:  lua_pushnumber(dst, lua_tonumber(src, absIdx)); break;
        case LUA_TBOOLEAN: lua_pushboolean(dst, lua_toboolean(src, absIdx)); break;
        case LUA_TNIL:     lua_pushnil(dst); break;
        default: {
            if (src->global == dst->global) {
                lua_pushvalue(src, absIdx);
                lua_xmove(src, dst, 1);
            }
            else {
                lua_pushnil(dst);
            }
            break;
        }
        }
    }
}
namespace LuaStateProxy
{
    inline int L_Proxy_Index(lua_State* L)
    {
        ProxyData* pd = GetProxy(L, 1);
        if (!pd)
        {
            luaL_error(L, "invalid LuaStateProxy");
            return 0;
        }

        if (lua_type(L, 2) != LUA_TSTRING)
        {
            lua_pushnil(L);
            return 1;
        }
        const char* key = lua_tostring(L, 2);

        if (strcmp(key, "Id") == 0)
        {
            lua_pushinteger(L, pd->id);
            return 1;
        }

        if (strcmp(key, "IsActorState") == 0)
        {
            lua_pushboolean(L, pd->isActorState ? 1 : 0);
            return 1;
        }

        if (strcmp(key, "IsValid") == 0)
        {
            lua_pushboolean(L, IsProxyUsable(pd) ? 1 : 0);
            return 1;
        }

        if (strcmp(key, "Execute") == 0)
        {
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, L_Proxy_Execute, "Execute", 1);
            return 1;
        }

        if (strcmp(key, "GetActors") == 0)
        {
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, L_Proxy_GetActors, "GetActors", 1);
            return 1;
        }

        if (strcmp(key, "Event") == 0)
        {
            GetProxyEvent(L, 1);
            return 1;
        }

        lua_pushnil(L);
        return 1;
    }
}
namespace LuaStateProxy
{
    inline int L_Proxy_Execute(lua_State* L)
    {
        ProxyData* pd = GetProxy(L, lua_upvalueindex(1));
        if (!IsProxyUsable(pd))
        {
            luaL_error(L, "Execute: invalid or stale state");
            return 0;
        }

        const char* source = luaL_checkstring(L, 1);
        lua_State* target = pd->state;
        const int nargs = lua_gettop(L) - 1;

        std::string bytecode = Execution::CompileScript(source);
        if (bytecode.empty())
        {
            luaL_error(L, "Execute: compile failed");
            return 0;
        }

        if (!lua_checkstack(target, 4))
        {
            luaL_error(L, "Execute: target stack exhausted");
            return 0;
        }

        lua_State* thread = lua_newthread(target);
        if (!thread)
        {
            luaL_error(L, "Execute: failed to create thread");
            return 0;
        }

        const int threadRef = lua_ref(target, -1);
        lua_pop(target, 1);

        if (luau_load(thread, "=LuaStateProxy", bytecode.data(), bytecode.size(), 0) != LUA_OK)
        {
            const char* err = lua_tostring(thread, -1);
            std::string msg = err ? err : "unknown";
            lua_pop(thread, 1);
            lua_unref(target, threadRef);
            luaL_error(L, "Execute: load error: %s", msg.c_str());
            return 0;
        }

        if (Closure* cl = clvalue(luaA_toobject(thread, -1)))
            if (cl->l.p)
                TaskScheduler::SetProtoCapabilities(cl->l.p, &MaxCapabilities);

        TaskScheduler::SetThreadCapabilities(thread, 8, MaxCapabilities);

        if (!lua_checkstack(thread, nargs + 4))
        {
            lua_unref(target, threadRef);
            luaL_error(L, "Execute: cannot reserve stack for arguments");
            return 0;
        }

        for (int i = 2; i <= nargs + 1; i++)
            deepCopy(L, thread, i);

        const int result = lua_resume(thread, L, nargs);

        if (result != LUA_YIELD)
            lua_unref(target, threadRef);

        return 0;
    }

    inline int L_Proxy_GetActors(lua_State* L)
    {
        lua_newtable(L);
        return 1;
    }
}

namespace LuaStateProxy
{
    inline int L_ProxyNew(lua_State* L)
    {
        if (!L || !L->userdata)
        {
            lua_pushnil(L);
            return 1;
        }

        PushProxy(L, L, false);
        return 1;
    }

    inline int L_GetLuaState(lua_State* L)
    {
        if (lua_gettop(L) == 0 || lua_isnil(L, 1))
        {
            PushProxy(L, L, false);
            return 1;
        }

        lua_pushnil(L);
        return 1;
    }

    inline int L_GetGameState(lua_State* L)
    {
        lua_State* robloxState = Shared::RobloxState;

        if (!robloxState && Shared::VoltState)
            robloxState = Shared::VoltState->global->mainthread;

        if (!robloxState || !Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(robloxState)))
        {
            lua_pushnil(L);
            return 1;
        }

        PushProxy(L, robloxState, false);
        return 1;
    }

    inline int L_GetActorStates(lua_State* L)
    {
        global_State* g = L->global;

        std::vector<lua_State*> found;
        found.reserve(32);

        if (g->allgcopages)
        {
            lua_gc(L, LUA_GCSTOP, 0);

            for (lua_Page* page = g->allgcopages; page; page = luaM_getnextpage(page))
            {
                char* start;
                char* end;
                int busy;
                int block;
                luaM_getpagewalkinfo(page, &start, &end, &busy, &block);

                if (busy <= 0 || block <= 0 || !start || !end)
                    continue;

                for (char* pos = start; pos != end; pos += block)
                {
                    GCObject* gco = reinterpret_cast<GCObject*>(pos);

                    if (gco->gch.tt != LUA_TTHREAD)
                        continue;
                    if (isdead(g, gco))
                        continue;

                    lua_State* thread = reinterpret_cast<lua_State*>(pos);

                    if (!Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(thread)))
                        continue;
                    if (thread->global != g)
                        continue;
                    if (!thread->userdata)
                        continue;
                    if (thread == g->mainthread)
                        continue;
                    if (thread == Shared::VoltState)
                        continue;
                    if (Shared::RobloxState && thread == Shared::RobloxState)
                        continue;

                    found.push_back(thread);
                }
            }

            lua_gc(L, LUA_GCRESTART, 0);
        }

        lua_createtable(L, static_cast<int>(found.size()), 0);

        int count = 0;
        for (lua_State* thread : found)
        {
            if (!lua_checkstack(L, 6))
                break;

            count++;
            PushProxy(L, thread, true);
            lua_rawseti(L, -2, count);
        }

        return 1;
    }
}

namespace LuaStateProxy
{
    inline void RegisterLibrary(lua_State* L)
    {
        EnsureMeta(L);

        lua_newtable(L);
        lua_pushcfunction(L, L_ProxyNew, "new");
        lua_setfield(L, -2, "new");
        lua_setglobal(L, "LuaStateProxy");

        Utils::AddFunction(L, "getluastate", L_GetLuaState);
        Utils::AddFunction(L, "getgamestate", L_GetGameState);
        Utils::AddFunction(L, "getactorstates", L_GetActorStates);
    }
}
#include "luastateproxy.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/update.hpp>
#include <lgc.h>
#include <lmem.h>
#include <cstring>


namespace LuaStateProxy
{
    int L_Proxy_Execute(lua_State* L);
    int L_Proxy_GetActors(lua_State* L);
}

namespace LuaStateProxy
{
    std::atomic<int> g_NextId{ 1 };
    const char* kProxyMeta = ("LuaStateProxy");
}

// â”€â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace LuaStateProxy
{
    static ProxyData* GetProxy(lua_State* L, int idx)
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

    static void EnsureMeta(lua_State* L)
    {
        luaL_getmetatable(L, kProxyMeta);
        if (!lua_isnil(L, -1)) { lua_pop(L, 1); return; }
        lua_pop(L, 1);

        luaL_newmetatable(L, kProxyMeta);

        lua_pushcfunction(L, L_Proxy_Index, ("__index"));
        lua_setfield(L, -2, ("__index"));

        lua_pushstring(L, ("LuaStateProxy"));
        lua_setfield(L, -2, ("__type"));
        lua_pushstring(L, ("The metatable is locked"));
        lua_setfield(L, -2, ("__metatable"));

        lua_pop(L, 1);
    }

    static void PushProxy(lua_State* L, lua_State* target, bool isActor)
    {
        EnsureMeta(L);

        ProxyData* pd = static_cast<ProxyData*>(lua_newuserdata(L, sizeof(ProxyData)));
        pd->state = target;
        pd->id = g_NextId.fetch_add(1, std::memory_order_relaxed);
        pd->isActorState = isActor;

        luaL_getmetatable(L, kProxyMeta);
        lua_setmetatable(L, -2);
    }

    // Get or create a Signal Event for a proxy. Pushes Event on stack.
    static void GetProxyEvent(lua_State* L, int proxyIdx)
    {
        void* ud = lua_touserdata(L, proxyIdx);

        // Look up in registry
        lua_getfield(L, LUA_REGISTRYINDEX, ("LuaStateProxy_Events"));
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, ("LuaStateProxy_Events"));
        }

        lua_pushlightuserdata(L, ud);
        lua_rawget(L, -2);

        if (!lua_isnil(L, -1))
        {
            // Found existing Event â€” remove events table, keep Event
            lua_remove(L, -2);
            return;
        }
        lua_pop(L, 1); // pop nil

        // Create new Signal
        lua_getglobal(L, ("Signal"));
        lua_getfield(L, -1, ("new"));
        lua_remove(L, -2); // remove Signal table
        if (lua_isfunction(L, -1))
        {
            if (lua_pcall(L, 0, 1, 0) == LUA_OK)
            {
                // Store in events table (stack: eventsTable, newSignal)
                lua_pushlightuserdata(L, ud);
                lua_pushvalue(L, -2);
                lua_rawset(L, -4);
                lua_remove(L, -2); // remove events table
                return;
            }
        }
        // Fallback: push nil
        lua_pop(L, 1); // pop failed function or non-function
        lua_pop(L, 1); // pop events table
        lua_pushnil(L);
    }
}

// â”€â”€â”€ __index â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace LuaStateProxy
{
    int L_Proxy_Index(lua_State* L)
    {
        ProxyData* pd = GetProxy(L, 1);
        if (!pd)
        {
            luaL_error(L, ("invalid LuaStateProxy"));
            return 0;
        }

        const char* key = luaL_checkstring(L, 2);

        if (strcmp(key, ("Id")) == 0)
        {
            lua_pushinteger(L, pd->id);
            return 1;
        }

        if (strcmp(key, ("IsActorState")) == 0)
        {
            lua_pushboolean(L, pd->isActorState ? 1 : 0);
            return 1;
        }

        if (strcmp(key, ("Execute")) == 0)
        {
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, L_Proxy_Execute, nullptr, 1);
            return 1;
        }

        if (strcmp(key, ("GetActors")) == 0)
        {
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, L_Proxy_GetActors, nullptr, 1);
            return 1;
        }

        if (strcmp(key, ("Event")) == 0)
        {
            GetProxyEvent(L, 1);
            return 1;
        }

        lua_pushnil(L);
        return 1;
    }
}

// â”€â”€â”€ Execute â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace LuaStateProxy
{
    int L_Proxy_Execute(lua_State* L)
    {
        ProxyData* pd = GetProxy(L, lua_upvalueindex(1));
        if (!pd || !pd->state)
        {
            luaL_error(L, ("Execute: invalid or nil state"));
            return 0;
        }

        const char* source = luaL_checkstring(L, 1);
        lua_State* target = pd->state;
        int nargs = lua_gettop(L) - 1;

        lua_State* thread = lua_newthread(target);
        if (!thread)
        {
            luaL_error(L, ("Execute: failed to create thread"));
            return 0;
        }

        int threadRef = lua_ref(target, LUA_REGISTRYINDEX);

        if (luau_load(thread, ("=LuaStateProxy"), source, strlen(source), 0) != LUA_OK)
        {
            const char* err = lua_tostring(thread, -1);
            lua_unref(target, threadRef);
            luaL_error(L, ("Execute: load error: %s"), err ? err : ("unknown"));
            return 0;
        }

        for (int i = 2; i <= lua_gettop(L); i++)
        {
            lua_pushvalue(L, i);
            lua_xmove(L, thread, 1);
        }

        int result = lua_pcall(thread, nargs, 0, 0);
        lua_unref(target, threadRef);

        if (result != LUA_OK)
        {
            const char* err = lua_tostring(thread, -1);
            OutputDebugStringA(("[LuaStateProxy] Execute error: "));
            if (err) OutputDebugStringA(err);
            OutputDebugStringA(("\n"));
        }

        return 0;
    }

    int L_Proxy_GetActors(lua_State* L)
    {
        lua_newtable(L);
        return 1;
    }
}

// â”€â”€â”€ Global functions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace LuaStateProxy
{
    int L_ProxyNew(lua_State* L)
    {
        if (!L || !L->userdata)
        {
            lua_pushnil(L);
            return 1;
        }

        PushProxy(L, L, false);
        return 1;
    }

    int L_GetLuaState(lua_State* L)
    {
        if (lua_gettop(L) == 0 || lua_isnil(L, 1))
        {
            PushProxy(L, L, false);
            return 1;
        }

        lua_pushnil(L);
        return 1;
    }

    int L_GetGameState(lua_State* L)
    {
        if (!Shared::ExploitThread)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_State* robloxState = Shared::ExploitThread->global->mainthread;
        if (!robloxState)
        {
            lua_pushnil(L);
            return 1;
        }

        PushProxy(L, robloxState, false);
        return 1;
    }

    int L_GetActorStates(lua_State* L)
    {
        lua_newtable(L);
        int count = 0;

        if (!L->global->allgcopages)
            return 1;

        for (lua_Page* page = L->global->allgcopages; page; page = luaM_getnextpage(page))
        {
            char* start;
            char* end;
            int busy;
            int block;
            luaM_getpagewalkinfo(page, &start, &end, &busy, &block);

            for (char* pos = start; pos != end; pos += block)
            {
                GCObject* gco = reinterpret_cast<GCObject*>(pos);
                if (isdead(L->global, gco))
                    continue;
                if (gco->gch.tt != LUA_TTHREAD)
                    continue;

                lua_State* thread = reinterpret_cast<lua_State*>(pos);
                if (!thread->userdata)
                    continue;
                if (thread == L->global->mainthread)
                    continue;
                if (thread == Shared::ExploitThread)
                    continue;

                count++;
                PushProxy(L, thread, true);
                lua_rawseti(L, -2, count);
            }
        }

        return 1;
    }
}

// â”€â”€â”€ Registration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace LuaStateProxy
{
    static int g_FuncNameInc = 0;

    void register_lib(lua_State* L)
    {
        g_FuncNameInc = 0;

        EnsureMeta(L);

        lua_newtable(L);
        lua_pushcfunction(L, L_ProxyNew, ("new"));
        lua_setfield(L, -2, ("new"));
        lua_setglobal(L, ("LuaStateProxy"));

        Environment->make_global(L, ("getluastate"), L_GetLuaState, g_FuncNameInc++);
        Environment->make_global(L, ("getgamestate"), L_GetGameState, g_FuncNameInc++);
        Environment->make_global(L, ("getactorstates"), L_GetActorStates, g_FuncNameInc++);
    }
}

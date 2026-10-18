#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <imgui.h>
#include <lstate.h>
#include <lualib.h>

namespace LuaStateProxy
{
    struct ProxyData
    {
        lua_State* state = nullptr;
        int id = 0;
        bool isActorState = false;
    };

    extern std::atomic<int> g_NextId;
    extern const char* kProxyMeta;

    int L_ProxyNew(lua_State* L);
    int L_ProxyExecute(lua_State* L);
    int L_ProxyGetActors(lua_State* L);
    int L_Proxy_Index(lua_State* L);

    int L_GetLuaState(lua_State* L);
    int L_GetGameState(lua_State* L);
    int L_GetActorStates(lua_State* L);

    void register_lib(lua_State* L);
}

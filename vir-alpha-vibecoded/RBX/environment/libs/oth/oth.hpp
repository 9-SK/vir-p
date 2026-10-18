#pragma once

#include <lstate.h>
#include <lualib.h>
#include <Windows.h>

namespace Oth
{
    static int funcnameinc = 0;

    int api_hook(lua_State* L);
    int api_unhook(lua_State* L);
    int api_iht(lua_State* L);
    int api_got(lua_State* L);
    int api_grc(lua_State* L);
    int api_ih(lua_State* L);
    int api_gh(lua_State* L);
    int api_rh(lua_State* L);
    int tc(lua_State* L);

    void register_lib(lua_State* L);
    void ResetLibrary();

    // Anti-detection: check if a closure is hooked by oth, and get its original c.f
    bool IsHookedClosure(Closure* c);
    lua_CFunction GetOriginalCF(Closure* c);
}

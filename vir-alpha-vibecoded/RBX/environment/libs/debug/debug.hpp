#pragma once

#include <lstate.h>
#include <lualib.h>

#include <cstring>
#include <string>

#include <lapi.h>
#include <lstring.h>
#include <lgc.h>
#include <lmem.h>
#include <lobject.h>
#include <lfunc.h>
#include <ltable.h>
#include <ldebug.h>

class debug_library
{
public:
    static void initialize(lua_State* L);
};

namespace Debug
{
    static int funcnameinc = 0;

    // Lua-facing functions
    int debug_getupvalues(lua_State* L);
    int debug_getupvalue(lua_State* L);
    int debug_setupvalue(lua_State* L);
    int debug_getconstants(lua_State* L);
    int debug_getconstant(lua_State* L);
    int debug_setconstant(lua_State* L);
    int debug_getprotos(lua_State* L);
    int debug_getproto(lua_State* L);
    int debug_getstack(lua_State* L);
    int debug_setstack(lua_State* L);
    int debug_getinfo(lua_State* L);
    int debug_getregistry(lua_State* L);
    int debug_setinfo(lua_State* L);
    int debug_validlevel(lua_State* L);

    void register_lib(lua_State* L);
}

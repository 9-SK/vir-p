#pragma once

#include <string>
#include <vector>
#include <lstate.h>
#include <lualib.h>
#include <luacode.h>

namespace Environment
{
    void Initialize(lua_State* L);
    extern std::vector<Closure*> function_array;
}
// елда
#pragma once

#include <Dependencies/Luau/VM/src/lstate.h>
#include <Roblox/Shared/Shared.hpp>

#include <thread>
#include <functional>

struct YieldState
{
    uintptr_t State; 
    uintptr_t TimeUsed; 
    uintptr_t Useless; 
};

struct YieldingLuaThread
{
    char Pad0[0x28];
    lua_State* L; 
};

using Yielded = std::function<int(lua_State*)>;

namespace Yielding
{
    int YieldExecution(lua_State* L, const std::function<Yielded()>& Function);
    void RunYield();
}
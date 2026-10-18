#pragma once

#include "core/Roblox/Update.h"

struct lua_State;

namespace Offsets
{
    namespace Luau
    {
        const uintptr_t LuaO_NilObject = Update::LuaU::LuaO_nilobject;
        const uintptr_t LuaH_DummyNode = Update::LuaU::LuaH_dummynode;
    }
}

namespace Roblox
{
    inline void LuaD_Throw(lua_State* L, int errcode)
    {
        using LuaDThrow_t = void(__fastcall*)(lua_State*, int);
        LuaDThrow_t fn = reinterpret_cast<LuaDThrow_t>(Update::Roblox::LuaDThrow);
        fn(L, errcode);
    }

    inline void Luau_Execute(lua_State* L)
    {
        Update::Roblox::Functions::Luau_Execute(L);
    }
}

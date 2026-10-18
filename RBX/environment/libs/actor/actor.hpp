#pragma once

#include <lstate.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace Actor
{
    static int funcnameinc = 0;

    int getactors(lua_State* L);
    int isparallel(lua_State* L);
    int run_on_actor(lua_State* L);
    int runonactor(lua_State* L);
    int create_comm_channel(lua_State* L);
    int get_comm_channel(lua_State* L);

    void register_lib(lua_State* L);
}

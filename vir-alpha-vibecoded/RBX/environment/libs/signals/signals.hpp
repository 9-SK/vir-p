#pragma once

#include <Windows.h>
#include <lua.h>
#include <lualib.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <lapi.h>
#include <lobject.h>
#include <ldo.h>
#include <lstate.h>

namespace Signals {
    static int funcnameinc = 0;

    struct TrackedConnection
    {
        int FunctionRef = -2;
        int ThreadRef = -2;
        uint64_t Generation = 0;
    };

    extern std::unordered_map<uintptr_t, std::vector<TrackedConnection>> TrackedConnectionsBySignal;
    extern std::unordered_map<uintptr_t, TrackedConnection> TrackedConnectionsByConn;

    uintptr_t GetSignalIdentity(lua_State* L, int index);
    void TrackConnection(lua_State* L, int signalIndex, uintptr_t conn_ptr, int funcRef);
    void PushTrackedConnections(lua_State* L, int signalIndex);

    struct RenderStepBinding
    {
        std::string Name;
        double Priority = 0.0;
        int FunctionRef = LUA_NOREF;
        int ThreadRef = LUA_NOREF;
    };

    struct RenderStepBinding2
    {
        std::string Name;
        double Priority = 0.0;
        int FunctionRef = LUA_NOREF;
        int ThreadRef = LUA_NOREF;
    };

    extern std::mutex RenderStepMutex;
    extern std::vector<RenderStepBinding> RenderStepBindings;
    extern std::vector<RenderStepBinding2> RenderStepBindings2;

    void ForgetRenderStepBinding(lua_State* L, const std::string& name);
    void RecordRenderStepBinding(lua_State* L);
    void ForgetRenderStepBindingByArg(lua_State* L);

    int fireproximityprompt(lua_State* L);
    int fireclickdetector(lua_State* L);
    int firetouchinterest(lua_State* L);
    int getconnectionsV2(lua_State* L);
    int getconnections(lua_State* L);
    int getconnection(lua_State* L);
    int firesignal(lua_State* L);
    int replicatesignal(lua_State* L);
    int setproximitypromptduration(lua_State* L);
    int getproximitypromptduration(lua_State* L);
    int getsignalwhitelist(lua_State* L);
    int cansignalreplicate(lua_State* L);
    int getsignalargumentsinfo(lua_State* L);
    int getsignalarguments(lua_State* L);
    int getrendersteppedlist(lua_State* L);
    int Defersignal(lua_State* L);

    void register_lib(lua_State* L);
    void ResetLibraryState();
}

#pragma once

#include <Windows.h>
#include <lstate.h>
#include <lgc.h>
#include <lualib.h>
#include <string>
#include <sstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <bitset>
#include <vector>
#include <cmath>
#include <cstring>

namespace HelperFuncs
{
    __forceinline bool CheckMemory(uintptr_t address);
}

namespace Script
{
    static int funcnameinc = 0;

    extern std::map<void*, std::string> ScriptHashes;

    struct ScriptableCacheEntry {
        uintptr_t instance;
        std::string property_name;
        bool is_scriptable;

        ScriptableCacheEntry(uintptr_t inst, const char* prop, bool scriptable)
            : instance(inst), property_name(prop), is_scriptable(scriptable) {
        }
    };

    extern std::vector<ScriptableCacheEntry> s_ScriptableCache;
    extern std::unordered_map<std::string, bool> s_DefaultPropStates;

    void add_defualtprop_state(const char* prop_name, bool was_scriptable);
    bool find_upd_scriptablecache(uintptr_t instance, const char* prop_name, bool new_state);
    int get_cachedscriptable_prop(uintptr_t instance, const char* prop_name);
    int getcacheddefscriptableprop(const char* prop_name);

    bool IsValidScriptClass(const char* className);
    std::string Blake2Hash(const char* data, size_t len);
    bool IsScriptClass(const std::string& className);

    int ScriptError(lua_State* L, const char* message);
    bool FetchBytecode(std::uintptr_t script, bool moduleScript, std::string& out);
    int getscriptbytecode(lua_State* L);
    int getscripthash(lua_State* L);
    bool IsModuleScriptValue(lua_State* L, int index);
    int getloadedmodules(lua_State* L);
    int getscripts(lua_State* L);
    void PushGCObjects(lua_State* L, bool includeTables);
    int getgc(lua_State* L);
    int getrenv(lua_State* L);
    int getcallingscript(lua_State* L);
    int getsenv(lua_State* L);
    int checkcaller(lua_State* L);
    bool ReadProtectedString(uintptr_t address, std::string& out);
    bool GetInstanceClassName(lua_State* L, int index, std::string& className);
    int getscriptclosure(lua_State* L);
    int getscriptfromthread(lua_State* L);
    int getscriptthread(lua_State* L);
    int getrunningscriptsv2(lua_State* L);
    int getallthreads(lua_State* L);

    void register_lib(lua_State* L);
}

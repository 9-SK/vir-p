#pragma once

#include <lstate.h>
#include <lualib.h>
#include <lgc.h>
#include <lapi.h>
#include <lobject.h>
#include <ldo.h>

#include <Windows.h>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <cstring>
#include <memory>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Shared/Memory.hpp>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>

#include <Roblox/Environment/Libraries/Closures.hpp>
#include <Roblox/Environment/Libraries/Signals.hpp>

namespace Actor
{
    namespace
    {
// ai shit from latestv6 skidding hq
        inline std::string s_isparallelBytecode;
        inline bool s_isparallelBytecodeReady = false;

        static std::string BuildIsparallelSource(bool PublishToGenv)
        {
            std::string src = R"LUA(
local ok, realtask = pcall(function()
    return task
end)
if not ok or typeof(realtask) ~= "table" then
    return
end

local threads = setmetatable({}, { __mode = "k" })
local copy = table.clone(realtask)

local okd, orig_desync = pcall(function()
    return rawget(copy, "desynchronize")
end)
local oks, orig_sync = pcall(function()
    return rawget(copy, "synchronize")
end)

if okd and typeof(orig_desync) == "function" then
    copy.desynchronize = function(...)
        orig_desync(...)
        threads[coroutine.running()] = true
    end
end

if oks and typeof(orig_sync) == "function" then
    copy.synchronize = function(...)
        orig_sync(...)
        threads[coroutine.running()] = nil
    end
end

local function current()
    return threads[coroutine.running()] == true
end
)LUA";

            if (PublishToGenv)
            {
                src += R"LUA(
local genv = getgenv and getgenv()
if genv then
    genv.task = copy
    genv.isparallel = current
end
)LUA";
            }
            else
            {
                src += R"LUA(
task = copy
isparallel = current
)LUA";
            }

            return src;
        }

        void QueueMainIsparallelBootstrap()
        {
            TaskScheduler::RequestExecution(BuildIsparallelSource(true));
        }

        static void inject_isparallel_wrapper(lua_State* thread, lua_State* L)
        {

            if (!s_isparallelBytecodeReady)
            {
                s_isparallelBytecode = Execution::CompileScript(BuildIsparallelSource(false));
                s_isparallelBytecodeReady = !s_isparallelBytecode.empty();
            }
            if (!s_isparallelBytecodeReady) return;
            if (luau_load(thread, "=isparallel_init", s_isparallelBytecode.data(), s_isparallelBytecode.size(), NULL) != LUA_OK)
                return;
            TaskScheduler::SetProtoCapabilities(clvalue(luaA_toobject(thread, -1))->l.p, &MaxCapabilities);
            lua_resume(thread, L, 0);
            lua_settop(thread, 0);
        }
        static inline bool signal_safe_read(const void* address, void* output, size_t size)
        {
            if (!address || !output || !size) return false;
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                return false;
            const uintptr_t start = reinterpret_cast<uintptr_t>(address);
            const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (start + size < start || start + size > end) return false;
            __try { memcpy(output, address, size); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }
    inline int getactors(lua_State* L)
    {
        lua_newtable(L);
        int resultIdx = lua_gettop(L);
        int index = 0;

        lua_getglobal(L, "game");
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return 1; }

        lua_getfield(L, -1, "GetDescendants");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 1; }

        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 2); return 1; }
        if (!lua_istable(L, -1)) { lua_pop(L, 2); return 1; }

        int len = (int)lua_objlen(L, -1);
        for (int i = 1; i <= len; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_isuserdata(L, -1)) {
                lua_getfield(L, -1, "ClassName");
                if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "Actor") == 0) {
                    lua_pop(L, 1);
                    lua_pushinteger(L, ++index);
                    lua_insert(L, -2);
                    lua_settable(L, resultIdx);
                }
                else { lua_pop(L, 2); }
            }
            else { lua_pop(L, 1); }
        }
        lua_pop(L, 2);
        return 1;
    }
    int isparallel(lua_State* L)
    {
        if (!L) { lua_pushboolean(L, false); return 1; }

        const auto extra = reinterpret_cast<RobloxExtraSpace*>(
            reinterpret_cast<std::uintptr_t>(L->userdata));
        if (!extra) { lua_pushboolean(L, false); return 1; }

        auto actorPtr = extra->actor.lock();
        if (!actorPtr) { lua_pushboolean(L, false); return 1; }

        const auto actor = *actorPtr;
        if (!actor) { lua_pushboolean(L, false); return 1; }

        std::uintptr_t inner = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(actor + 0x18),
            &inner, sizeof(inner)) || !inner)
        {
            lua_pushboolean(L, false); return 1;
        }

        std::uintptr_t v8 = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(inner + 0x28),
            &v8, sizeof(v8)) || !v8)
        {
            lua_pushboolean(L, false); return 1;
        }

        std::uint8_t parallel = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(v8 + 0x100),
            &parallel, sizeof(parallel)))
        {
            lua_pushboolean(L, false); return 1;
        }

        lua_pushboolean(L, parallel != 0);
        return 1;
    }

    inline int create_comm_channel(lua_State* L);
    inline int get_comm_channel(lua_State* L);
    inline int run_on_actor(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TSTRING);

        lua_getfield(L, 1, "ClassName");
        const auto class_name = std::string_view(lua_tolstring(L, -1, nullptr));
        lua_pop(L, 1);

        if (class_name != "Actor") {
            luaL_argerror(L, 1, "Actor expected");
            return 0;
        }

        const auto actor = *reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 1));
        const std::string Code = lua_tostring(L, 2);

        lua_remove(L, 2);
        lua_remove(L, 1);
        const int Args = lua_gettop(L);
        lua_getglobal(L, "Instance");
        lua_getfield(L, -1, "new");
        lua_pushstring(L, "LocalScript");
        lua_call(L, 1, 1);

        if (!lua_isuserdata(L, -1)) {
            lua_pop(L, 2);
            luaL_error(L, "Failed to create LocalScript");
            return 0;
        }

        const auto LocalScript = *reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, -1));
        if (!LocalScript) {
            lua_pop(L, 2);
            luaL_error(L, "Invalid LocalScript");
            return 0;
        }
        *reinterpret_cast<std::uintptr_t*>(LocalScript + Offsets::Instance::Parent) = actor;
        const std::uintptr_t ScriptContext =
            reinterpret_cast<std::uintptr_t>(L->userdata->shared->scriptContext);

        std::uintptr_t unused = 0;
        lua_State* ActorState = reinterpret_cast<lua_State*>(
            TaskScheduler::GetLuaState(ScriptContext, unused, LocalScript));

        if (!ActorState) {
            lua_pop(L, 2);
            luaL_error(L, "Failed to get actor lua state");
            return 0;
        }

        lua_State* NL = lua_newthread(ActorState);
        lua_pop(ActorState, 1);

        NL->userdata->actor =
            std::make_shared<std::uintptr_t>(static_cast<std::uintptr_t>(actor));

        TaskScheduler::SetThreadCapabilities(NL, 8, MaxCapabilities);
        luaL_sandboxthread(NL);
        lua_settop(NL, 0);

        lua_newtable(NL); lua_setglobal(NL, "_G");
        lua_newtable(NL); lua_setglobal(NL, "shared");
        Closures::RegisterLibrary(NL);
        Signals::RegisterLibrary(NL);
        Debug::RegisterLibrary(NL);
        Http::RegisterLibrary(NL);
        Crypt::RegisterLibrary(NL);
        Input::RegisterLibrary(NL);
        Cache::RegisterLibrary(NL);
        Script::RegisterLibrary(NL);
        Instance::RegisterLibrary(NL);
        Websocket::RegisterLibrary(NL);
        Metatable::RegisterLibrary(NL);
        Filesystem::RegisterLibrary(NL);
        Reflection::RegisterLibrary(NL);
        Miscellaneous::RegisterLibrary(NL);
        // Regex::RegisterLibrary(NL); breaks code idk why
        FileDialog::RegisterLibrary(NL);

        Utils::AddFunction(NL, "get_comm_channel", Actor::get_comm_channel);
        Utils::AddFunction(NL, "create_comm_channel", Actor::create_comm_channel);
        inject_isparallel_wrapper(NL, L);
        std::function<void(lua_State*, lua_State*, int)> deepCopy =
            [&](lua_State* src, lua_State* dst, int idx)
            {
                int absIdx = lua_absindex(src, idx);
                switch (lua_type(src, absIdx))
                {
                case LUA_TTABLE: {
                    lua_newtable(dst);
                    lua_pushnil(src);
                    while (lua_next(src, absIdx)) {
                        deepCopy(src, dst, -2);
                        deepCopy(src, dst, -1);
                        lua_settable(dst, -3);
                        lua_pop(src, 1);
                    }
                    break;
                }
                case LUA_TUSERDATA: {
                    if (strcmp(luaL_typename(src, absIdx), "Instance") == 0)
                        Hyperion::InstanceBridge::Void2(dst, lua_touserdata(src, absIdx));
                    else { lua_pushvalue(src, absIdx); lua_xmove(src, dst, 1); }
                    break;
                }
                case LUA_TSTRING: {
                    size_t len = 0;
                    const char* str = lua_tolstring(src, absIdx, &len);
                    lua_pushlstring(dst, str, len);
                    break;
                }
                case LUA_TNUMBER: lua_pushnumber(dst, lua_tonumber(src, absIdx)); break;
                case LUA_TBOOLEAN: lua_pushboolean(dst, lua_toboolean(src, absIdx)); break;
                case LUA_TNIL: lua_pushnil(dst); break;
                default: lua_pushvalue(src, absIdx); lua_xmove(src, dst, 1); break;
                }
            };

        std::string Bytecode = Execution::CompileScript(Code);
        if (Bytecode.empty()) {
            luaL_error(L, "Failed to compile actor script");
            return 0;
        }

        if (luau_load(NL, "Xploits", Bytecode.data(), Bytecode.size(), 0) == LUA_OK) {
            if (Closure* Cl = clvalue(luaA_toobject(NL, -1)))
                if (Cl->l.p)
                    TaskScheduler::SetProtoCapabilities(Cl->l.p, &MaxCapabilities);

            for (int i = 1; i <= Args; i++)
                deepCopy(L, NL, i);

            Hyperion::TaskDefer(NL);
        }
        else {
            const char* err = lua_tostring(NL, -1);
            std::string msg = err ? err : "unknown load error";
            lua_pop(NL, 1);
            luaL_error(L, "%s", msg.c_str());
            return 0;
        }
        return 0;
    }

    inline int runonactor(lua_State* L) { return run_on_actor(L); }
    namespace
    {
        struct CommArg {
            enum Type { NIL, BOOLEAN, NUMBER, STRING, USERDATA };
            Type type = NIL;
            bool boolVal = false;
            double numVal = 0;
            std::string strVal;
            void* udPtr = nullptr;
        };

        struct CommChannelData {
            int channelRef;
            int signalRef;
            lua_State* mainL;
        };

        inline std::mutex commMutex;
        inline std::unordered_map<int, CommChannelData> commIdToData;
        inline int nextCommId = 1;

        inline void DispatchFire(int commId, lua_State* callingL, int argStart, int nargs)
        {
            CommChannelData channelData;
            {
                std::lock_guard<std::mutex> lock(commMutex);
                auto it = commIdToData.find(commId);
                if (it == commIdToData.end()) return;
                channelData = it->second;
            }

            auto args = std::make_shared<std::vector<CommArg>>();
            for (int i = 0; i < nargs; i++) {
                int idx = argStart + i;
                CommArg arg;
                switch (lua_type(callingL, idx)) {
                case LUA_TNIL:     arg.type = CommArg::NIL; break;
                case LUA_TBOOLEAN: arg.type = CommArg::BOOLEAN; arg.boolVal = lua_toboolean(callingL, idx) != 0; break;
                case LUA_TNUMBER:  arg.type = CommArg::NUMBER; arg.numVal = lua_tonumber(callingL, idx); break;
                case LUA_TSTRING: {
                    size_t len = 0;
                    const char* s = lua_tolstring(callingL, idx, &len);
                    arg.type = CommArg::STRING;
                    arg.strVal.assign(s, len);
                    break;
                }
                case LUA_TUSERDATA: {
                    arg.type = CommArg::USERDATA;
                    arg.udPtr = lua_touserdata(callingL, idx);
                    break;
                }
                default: arg.type = CommArg::NIL; break;
                }
                args->push_back(arg);
            }

            lua_State* mainL = channelData.mainL;
            int signalRef = channelData.signalRef;

            std::lock_guard<std::mutex> ylock(Shared::YieldMutex);
            Shared::YieldQueue.emplace([mainL, signalRef, args]() {
                lua_getref(mainL, signalRef);
                if (!lua_istable(mainL, -1)) { lua_pop(mainL, 1); return; }

                lua_getfield(mainL, -1, "Fire");
                if (!lua_isfunction(mainL, -1)) { lua_pop(mainL, 2); return; }

                lua_insert(mainL, -2);

                for (auto& arg : *args) {
                    switch (arg.type) {
                    case CommArg::NIL:     lua_pushnil(mainL); break;
                    case CommArg::BOOLEAN: lua_pushboolean(mainL, arg.boolVal); break;
                    case CommArg::NUMBER:  lua_pushnumber(mainL, arg.numVal); break;
                    case CommArg::STRING:  lua_pushlstring(mainL, arg.strVal.data(), arg.strVal.size()); break;
                    case CommArg::USERDATA: Hyperion::InstanceBridge::Void2(mainL, arg.udPtr); break;
                    }
                }
                lua_pcall(mainL, (int)args->size() + 1, 0, 0);
                });
        }

        inline int comm_fire(lua_State* L)
        {
            int commId = (int)lua_tointeger(L, lua_upvalueindex(1));
            int nargs = lua_gettop(L) - 1;
            if (nargs < 0) nargs = 0;

            DispatchFire(commId, L, 2, nargs);
            return 0;
        }
    }
    inline int create_comm_channel(lua_State* L)
    {
        int commId;
        {
            std::lock_guard<std::mutex> lock(commMutex);
            commId = nextCommId++;
        }

        lua_newtable(L);
        int objIdx = lua_gettop(L);

        lua_getglobal(L, "Signal");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "new");
            if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == LUA_OK) {
                lua_setfield(L, objIdx, "Event");
            }
            else {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_setfield(L, objIdx, "Event");
            }
        }
        else {
            lua_newtable(L);
            lua_setfield(L, objIdx, "Event");
        }
        lua_settop(L, objIdx);

        lua_getfield(L, objIdx, "Event");
        int signalRef = lua_ref(L, -1);
        lua_pop(L, 1);

        lua_pushvalue(L, objIdx);
        int channelRef = lua_ref(L, -1);
        lua_pop(L, 1);

        {
            std::lock_guard<std::mutex> lock(commMutex);
            commIdToData[commId] = { channelRef, signalRef, L };
        }

        lua_pushinteger(L, commId);
        lua_pushcclosure(L, comm_fire, "CommFire", 1);
        lua_setfield(L, objIdx, "Fire");

        lua_pushinteger(L, commId);
        lua_pushvalue(L, objIdx);
        return 2;
    }
    inline int get_comm_channel(lua_State* L)
    {
        if (lua_gettop(L) < 1 || lua_isnil(L, 1)) {
            lua_pushnil(L);
            return 1;
        }

        int commId = (int)lua_tointeger(L, 1);

        std::lock_guard<std::mutex> lock(commMutex);
        auto it = commIdToData.find(commId);
        if (it == commIdToData.end()) {
            lua_pushnil(L);
            return 1;
        }

        if (L == it->second.mainL) {
            lua_getref(L, it->second.channelRef);
            return 1;
        }

        lua_newtable(L);
        lua_pushinteger(L, commId);
        lua_pushcclosure(L, comm_fire, "CommFire", 1);
        lua_setfield(L, -2, "Fire");
        return 1;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, "getactors", Actor::getactors);
        Utils::AddFunction(L, "isparallel", Actor::isparallel);
        Utils::AddFunction(L, "checkparallel", Actor::isparallel);
        Utils::AddFunction(L, "run_on_actor", Actor::run_on_actor);
        Utils::AddFunction(L, "runonactor", Actor::runonactor);
        Utils::AddFunction(L, "create_comm_channel", Actor::create_comm_channel);
        Utils::AddFunction(L, "get_comm_channel", Actor::get_comm_channel);
    }
}
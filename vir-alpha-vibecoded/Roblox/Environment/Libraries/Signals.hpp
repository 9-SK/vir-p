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
#include <lapi.h>
#include <lobject.h>
#include <ldo.h>
#include <lstate.h>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
namespace Signals {
    struct TrackedConnection
    {
        int FunctionRef = -2;
        int ThreadRef = -2;
        uint64_t Generation = 0;
    };

    inline std::unordered_map<uintptr_t, std::vector<TrackedConnection>> TrackedConnectionsBySignal;
    inline std::unordered_map<uintptr_t, TrackedConnection> TrackedConnectionsByConn;
}

template <typename T>
__forceinline static bool IsPtrValid(T value)
{
    const void* ptr = reinterpret_cast<const void*>(value);
    MEMORY_BASIC_INFORMATION buffer{};
    SIZE_T read = VirtualQuery(ptr, &buffer, sizeof(buffer));
    if (read == 0 || read != sizeof(buffer))
        return false;
    if (buffer.RegionSize < sizeof(T))
        return false;
    if ((buffer.State & MEM_FREE) == MEM_FREE)
        return false;
    const DWORD valid_prot = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((buffer.Protect & valid_prot) != 0)
        return true;
    if ((buffer.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    return true;
}

inline bool signal_safe_read(const void* address, void* output, size_t size)
{
    if (!address || !output || !size)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (start + size < start || start + size > end)
        return false;
    __try
    {
        memcpy(output, address, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline bool signal_safe_write(void* address, const void* value, size_t size)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !value || !VirtualQuery(address, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    __try
    {
        memcpy(address, value, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

namespace ConnOffsets {
    namespace Connection = Offsets::signal_offsets::connection;
    namespace SlotWrapper = Offsets::signal_offsets::slot_wrapper;

    constexpr uintptr_t signature = Offsets::signal_offsets::signature;

    constexpr uintptr_t weak_object_ref_legacy = 0x50;
}

static std::uintptr_t read_weak_object_field(std::uintptr_t slot_wrapper) {
    if (!slot_wrapper || !IsPtrValid(slot_wrapper)) return 0;

    for (const uintptr_t candidate : {
        ConnOffsets::SlotWrapper::weak_object_ref,
            ConnOffsets::SlotWrapper::weak_object_ref_fallback,
            ConnOffsets::weak_object_ref_legacy }) {
        std::uintptr_t ptr = 0;
        if (signal_safe_read(reinterpret_cast<void*>(slot_wrapper + candidate), &ptr, sizeof(ptr))) {
            if (ptr && IsPtrValid(ptr)) return ptr;
        }
    }

    return 0;
}

struct connection_struct {
    std::uintptr_t connection;
    std::uintptr_t oldEnabled;
    bool IsValid;
    bool ForeignState;
};

struct CachedConnectionTable {
    int ref;
    uint64_t generation;
};
inline std::unordered_map<std::uintptr_t, CachedConnectionTable> g_connection_cache;
inline std::unordered_set<std::uintptr_t> g_fired_connections;

static std::uintptr_t get_weak_object_via_chain(std::uintptr_t signalPtr) {
    std::uintptr_t val0 = 0;
    if (!signal_safe_read(reinterpret_cast<void*>(signalPtr + 0x68), &val0, sizeof(val0)))
        return 0;
    if (!val0 || !IsPtrValid(val0)) return 0;

    std::uintptr_t val1 = 0;
    if (!signal_safe_read(reinterpret_cast<void*>(val0 + 8), &val1, sizeof(val1)))
        return 0;
    if (!val1 || !IsPtrValid(val1)) return 0;

    return read_weak_object_field(val1);
}

static std::uintptr_t get_weak_object_ptr(std::uintptr_t signalPtr) {
    std::uintptr_t slot_wrapper = 0;
    if (!signal_safe_read(reinterpret_cast<void*>(signalPtr + ConnOffsets::Connection::slot_wrapper), &slot_wrapper, sizeof(slot_wrapper)))
        return 0;
    if (!slot_wrapper) return 0;

    std::uintptr_t slot_wrapper_self = 0;
    signal_safe_read(reinterpret_cast<void*>(signalPtr + ConnOffsets::Connection::slot_wrapper_self), &slot_wrapper_self, sizeof(slot_wrapper_self));

    const bool is_self = slot_wrapper_self && IsPtrValid(slot_wrapper_self) &&
        (slot_wrapper == (slot_wrapper_self + 0x18));

    if (!is_self)
        return get_weak_object_via_chain(signalPtr);

    if (const auto direct = read_weak_object_field(slot_wrapper))
        return direct;

    return get_weak_object_via_chain(signalPtr);
}

static lua_State* get_connection_thread(std::uintptr_t weak_obj) {
    lua_State* thread = nullptr;
    if (!signal_safe_read(reinterpret_cast<void*>(weak_obj + ConnOffsets::SlotWrapper::thread), &thread, sizeof(thread)))
        return nullptr;
    if (!thread || !IsPtrValid(reinterpret_cast<uintptr_t>(thread)))
        return nullptr;

    uint8_t tt = 0;
    if (!signal_safe_read(&thread->tt, &tt, sizeof(tt)) || tt != LUA_TTHREAD)
        return nullptr;

    return thread;
}


inline std::unordered_map<std::uintptr_t, int> g_slot_function_ref_cache;

inline std::unordered_set<std::uintptr_t> g_slot_sweep_failed;

static Closure* get_connection_function(std::uintptr_t weak_obj) {
    lua_State* thread = get_connection_thread(weak_obj);
    if (!thread) return nullptr;

    auto try_get_closure = [&](int ref) -> Closure* {
        if (ref <= 0) return nullptr;
        lua_getref(thread, ref);
        if (lua_isfunction(thread, -1)) {
            auto cl = clvalue(luaA_toobject(thread, -1));
            lua_pop(thread, 1);
            return cl;
        }
        lua_pop(thread, 1);
        return nullptr;
        };

    const auto cached = g_slot_function_ref_cache.find(weak_obj);
    if (cached != g_slot_function_ref_cache.end()) {
        if (auto cl = try_get_closure(cached->second))
            return cl;
        g_slot_function_ref_cache.erase(cached);
    }

    std::int32_t function_id = 0;
    if (signal_safe_read(reinterpret_cast<void*>(weak_obj + ConnOffsets::SlotWrapper::function_id), &function_id, sizeof(function_id))) {
        if (auto cl = try_get_closure(function_id)) {
            g_slot_function_ref_cache[weak_obj] = function_id;
            return cl;
        }
    }

    if (g_slot_sweep_failed.count(weak_obj))
        return nullptr;

    std::int32_t base_function_id = 0;
    if (signal_safe_read(reinterpret_cast<void*>(weak_obj), &base_function_id, sizeof(base_function_id))) {
        for (int offset = 0; offset <= 0xffc; ++offset) {
            const int candidate = base_function_id + offset;
            if (auto cl = try_get_closure(candidate)) {
                g_slot_function_ref_cache[weak_obj] = candidate;
                return cl;
            }
        }
    }

    g_slot_sweep_failed.insert(weak_obj);
    return nullptr;
}

static connection_struct* get_con_from_table(lua_State* L, int idx) {
    lua_rawgetfield(L, idx, "Object");
    if (!lua_islightuserdata(L, -1)) {
        lua_pop(L, 1);
        return nullptr;
    }
    auto* con = reinterpret_cast<connection_struct*>(lua_tolightuserdata(L, -1));
    lua_pop(L, 1);
    return con;
}

static bool con_enabled(connection_struct* con) {
    if (!con || !con->connection) return false;
    std::uintptr_t val = 0;
    signal_safe_read(reinterpret_cast<void*>(con->connection + ConnOffsets::Connection::enabled), &val, sizeof(val));
    return val > 0;
}

static void set_enabled(connection_struct* con, uintptr_t value) {
    if (!con || !con->connection) return;
    signal_safe_write(reinterpret_cast<void*>(con->connection + ConnOffsets::Connection::enabled), &value, sizeof(value));
}

static int conn_noop(lua_State* L) { return 0; }

static int conn_fire(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* con = get_con_from_table(L, 1);
    if (!con || !con->connection) return 0;

    const auto weak_ref = get_weak_object_ptr(con->connection);
    Closure* function = nullptr;
    if (weak_ref) {
        lua_State* thread = get_connection_thread(weak_ref);
        const bool foreign = thread
            ? (thread->global->mainthread != L->global->mainthread)
            : false;
        if (foreign) return 0;
        function = get_connection_function(weak_ref);
    }

    if (function && function->tt == LUA_TFUNCTION) {
        lua_remove(L, 1);
        const int argc = lua_gettop(L);
        lua_State* co = lua_newthread(L);
        setclvalue(co, co->top, function);
        incr_top(co);
        for (int i = 1; i <= argc; ++i) {
            lua_pushvalue(L, i);
            lua_xmove(L, co, 1);
        }
        lua_resume(co, L, argc);
        lua_settop(L, 0);
        g_fired_connections.insert(con->connection);
        return 0;
    }

    int ref = LUA_NOREF;
    lua_rawgetfield(L, 1, "_TFR");
    if (lua_isnumber(L, -1)) {
        ref = (int)lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    if (ref == LUA_NOREF && con && con->connection) {
        auto found = Signals::TrackedConnectionsByConn.find(con->connection);
        if (found != Signals::TrackedConnectionsByConn.end()) {
            ref = found->second.FunctionRef;
        }
    }

    if (ref != LUA_NOREF) {
        lua_remove(L, 1);
        const int argc = lua_gettop(L);
        lua_getref(L, ref);
        if (lua_isfunction(L, -1)) {
            lua_State* co = lua_newthread(L);
            lua_pushvalue(L, -2);
            lua_xmove(L, co, 1);
            lua_pop(L, 1);
            for (int i = 1; i <= argc; ++i) {
                lua_pushvalue(L, i);
                lua_xmove(L, co, 1);
            }
            lua_resume(co, L, argc);
            lua_settop(L, 0);
            g_fired_connections.insert(con->connection);
            return 0;
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int conn_defer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* con = get_con_from_table(L, 1);
    if (!con || !con->connection) return 0;

    const auto weak_ref = get_weak_object_ptr(con->connection);
    if (!weak_ref) return 0;

    const auto function = get_connection_function(weak_ref);
    if (!function || function->tt != LUA_TFUNCTION) return 0;

    lua_remove(L, 1);
    const int argc = lua_gettop(L);

    lua_getglobal(L, "task");
    lua_getfield(L, -1, "defer");
    lua_remove(L, -2);

    setclvalue(L, L->top, function);
    incr_top(L);

    for (int i = 1; i <= argc; ++i)
        lua_pushvalue(L, i);

    lua_call(L, argc + 1, 0);
    return 0;
}

static int conn_disable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* con = get_con_from_table(L, 1);
    if (con && con->connection) set_enabled(con, 0);
    return 0;
}

static int conn_disconnect(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* con = get_con_from_table(L, 1);
    if (!con || !con->connection) return 0;
    set_enabled(con, 0);
    con->IsValid = false;
    const auto cached = g_connection_cache.find(con->connection);
    if (cached != g_connection_cache.end()) {
        if (cached->second.generation == Shared::StateGeneration.load() && cached->second.ref > 0)
            lua_unref(L, cached->second.ref);
        g_connection_cache.erase(cached);
    }
    return 0;
}

static int conn_enable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto* con = get_con_from_table(L, 1);
    if (con && con->connection) set_enabled(con, con->oldEnabled ? con->oldEnabled : 1);
    return 0;
}

static lua_State* get_wait_thread(std::uintptr_t signalPtr) {
    uintptr_t slot_wrapper_self = 0;
    signal_safe_read(reinterpret_cast<void*>(signalPtr + ConnOffsets::Connection::slot_wrapper_self), &slot_wrapper_self, sizeof(slot_wrapper_self));
    if (!slot_wrapper_self || !IsPtrValid(slot_wrapper_self)) return nullptr;

    uintptr_t container = 0;
    if (!signal_safe_read(reinterpret_cast<void*>(slot_wrapper_self + 0x50), &container, sizeof(container)))
        return nullptr;
    if (!container || !IsPtrValid(container)) return nullptr;

    lua_State* thread = nullptr;
    if (!signal_safe_read(reinterpret_cast<void*>(container + 0x28), &thread, sizeof(thread)))
        return nullptr;
    if (!thread || !IsPtrValid(reinterpret_cast<uintptr_t>(thread))) return nullptr;

    uint8_t tt = 0;
    if (!signal_safe_read(&thread->tt, &tt, sizeof(tt)) || tt != LUA_TTHREAD)
        return nullptr;

    global_State* g = nullptr;
    if (!signal_safe_read(&thread->global, &g, sizeof(g)) || !g)
        return nullptr;

    return thread;
}

static int connection_index(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TSTRING);

    const char* key = lua_tostring(L, 2);
    auto* con = get_con_from_table(L, 1);

    if (con && con->connection && !con->IsValid) {
        if (strcmp(key, "Enabled") == 0 ||
            strcmp(key, "LuaConnection") == 0) {
            lua_pushboolean(L, false);
            return 1;
        }
        if (strcmp(key, "ForeignState") == 0) {
            lua_pushboolean(L, con->ForeignState);
            return 1;
        }
        if (strcmp(key, "Function") == 0 ||
            strcmp(key, "Thread") == 0) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushcfunction(L, conn_noop, "conn_noop");
        return 1;
    }

    if (!con || !con->connection) {
        if (strcmp(key, "Enabled") == 0 || strcmp(key, "ForeignState") == 0 ||
            strcmp(key, "LuaConnection") == 0) {
            lua_pushboolean(L, false);
            return 1;
        }
        if (strcmp(key, "Function") == 0 || strcmp(key, "Thread") == 0) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushcfunction(L, conn_noop, "conn_noop");
        return 1;
    }

    if (strcmp(key, "Enabled") == 0) {
        lua_pushboolean(L, con_enabled(con));
        return 1;
    }
    if (strcmp(key, "Disable") == 0) {
        lua_pushcfunction(L, conn_disable, "Connection.Disable");
        return 1;
    }
    if (strcmp(key, "Disconnect") == 0) {
        lua_pushcfunction(L, conn_disconnect, "Connection.Disconnect");
        return 1;
    }
    if (strcmp(key, "Enable") == 0) {
        lua_pushcfunction(L, conn_enable, "Connection.Enable");
        return 1;
    }
    if (strcmp(key, "Fire") == 0) {
        lua_pushcfunction(L, conn_fire, "Connection.Fire");
        return 1;
    }
    if (strcmp(key, "Defer") == 0) {
        lua_pushcfunction(L, conn_defer, "Connection.Defer");
        return 1;
    }

    const auto weak_ref = get_weak_object_ptr(con->connection);
    lua_State* nativeThread = weak_ref ? get_connection_thread(weak_ref) : nullptr;

    Closure* nativeFunction = nullptr;
    bool nativeFunctionResolved = false;
    auto native_function = [&]() -> Closure* {
        if (!nativeFunctionResolved) {
            nativeFunctionResolved = true;
            if (weak_ref)
                nativeFunction = get_connection_function(weak_ref);
        }
        return nativeFunction;
        };

    int trackedFuncRef = LUA_NOREF;
    int trackedThreadRef = LUA_NOREF;
    lua_rawgetfield(L, 1, "_TFR");
    if (lua_isnumber(L, -1)) trackedFuncRef = (int)lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_rawgetfield(L, 1, "_TTR");
    if (lua_isnumber(L, -1)) trackedThreadRef = (int)lua_tonumber(L, -1);
    lua_pop(L, 1);

    if (trackedFuncRef == LUA_NOREF && con && con->connection) {
        auto found = Signals::TrackedConnectionsByConn.find(con->connection);
        if (found != Signals::TrackedConnectionsByConn.end()) {
            trackedFuncRef = found->second.FunctionRef;
            trackedThreadRef = found->second.ThreadRef;
        }
    }

    if (nativeThread) {
        global_State* nativeGlobal = nullptr;
        global_State* currentGlobal = nullptr;
        signal_safe_read(&nativeThread->global, &nativeGlobal, sizeof(nativeGlobal));
        signal_safe_read(&L->global, &currentGlobal, sizeof(currentGlobal));
        con->ForeignState = (nativeGlobal && currentGlobal && nativeGlobal != currentGlobal);
    }
    else {
        con->ForeignState = false;
    }
    auto is_lua_connection = [&]() -> bool {
        lua_State* waitThread = get_wait_thread(con->connection);
        if (waitThread && waitThread->tt == LUA_TTHREAD) {
            global_State* waitGlobal = nullptr;
            global_State* currentGlobal = nullptr;
            signal_safe_read(&waitThread->global, &waitGlobal, sizeof(waitGlobal));
            signal_safe_read(&L->global, &currentGlobal, sizeof(currentGlobal));

            if (waitGlobal && currentGlobal && waitGlobal == currentGlobal) {
                return true;
            }
        }

        bool hasFuncRef = false;
        if (weak_ref) {
            int32_t func_id = 0;
            if (signal_safe_read(reinterpret_cast<void*>(weak_ref + ConnOffsets::SlotWrapper::function_id), &func_id, sizeof(func_id)))
                hasFuncRef = (func_id > 0);
            if (!hasFuncRef) {
                int32_t base_id = 0;
                if (signal_safe_read(reinterpret_cast<void*>(weak_ref), &base_id, sizeof(base_id)))
                    hasFuncRef = (base_id > 0);
            }
        }

        if (hasFuncRef && !con->ForeignState)
            return true;

        Closure* fn = native_function();
        if (fn && !fn->isC)
            return true;

        if (trackedFuncRef != LUA_NOREF) {
            lua_getref(L, trackedFuncRef);
            const bool tracked = lua_type(L, -1) == LUA_TFUNCTION;
            lua_pop(L, 1);
            if (tracked)
                return true;
        }

        return false;
        };
    if (strcmp(key, "ForeignState") == 0) {
        lua_pushboolean(L, con->ForeignState);
    }
    else if (strcmp(key, "LuaConnection") == 0) {
        lua_pushboolean(L, is_lua_connection());
    }
    else if (strcmp(key, "Function") == 0) {
        Closure* fn = native_function();
        if (fn && fn->tt == LUA_TFUNCTION) {
            setclvalue(L, L->top, fn);
            incr_top(L);
        }
        else if (trackedFuncRef != LUA_NOREF) {
            lua_getref(L, trackedFuncRef);
        }
        else {
            lua_pushnil(L);
        }
    }
    else if (strcmp(key, "Thread") == 0) {
        lua_State* thread = nativeThread;

        if (!thread || thread->tt != LUA_TTHREAD)
            thread = get_wait_thread(con->connection);

        if (thread && thread->tt == LUA_TTHREAD) {
            global_State* nativeGlobal = nullptr;
            global_State* currentGlobal = nullptr;
            signal_safe_read(&thread->global, &nativeGlobal, sizeof(nativeGlobal));
            signal_safe_read(&L->global, &currentGlobal, sizeof(currentGlobal));
            if (nativeGlobal == currentGlobal) {
                setthvalue(L, L->top, thread);
                incr_top(L);
            }
            else {
                lua_pushnil(L);
            }
        }
        else if (trackedThreadRef != LUA_NOREF) {
            lua_getref(L, trackedThreadRef);
            if (!lua_isthread(L, -1)) {
                lua_pop(L, 1);
                lua_pushnil(L);
            }
        }
        else {
            lua_pushnil(L);
        }
    }
    else {
        lua_pushnil(L);
    }

    return 1;
}

static int connection_newindex(lua_State* L) {
    return 0;
}

static int push_connection_mt(lua_State* L, uintptr_t signalPtr) {
    auto* conn = static_cast<connection_struct*>(lua_newuserdata(L, sizeof(connection_struct)));
    lua_ref(L, -1);
    lua_pop(L, 1);

    conn->connection = signalPtr;

    std::uintptr_t enabled_val = 0;
    signal_safe_read(reinterpret_cast<void*>(signalPtr + ConnOffsets::Connection::enabled), &enabled_val, sizeof(enabled_val));
    conn->oldEnabled = enabled_val;
    conn->IsValid = true;
    conn->ForeignState = false;

    lua_newtable(L);
    lua_pushlightuserdata(L, conn);
    lua_setfield(L, -2, "Object");

    lua_getfield(L, LUA_REGISTRYINDEX, "connection_signal_object");
    lua_setmetatable(L, -2);

    return 1;
}

static int push_connection_mt_safe(lua_State* L, uintptr_t signalPtr) {
    int (* const body)(lua_State*, uintptr_t) = push_connection_mt;
    __try {
        return body(L, signalPtr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

namespace Signals {

    inline uintptr_t GetSignalIdentity(lua_State* L, int index)
    {
        uintptr_t userdata = reinterpret_cast<uintptr_t>(lua_touserdata(L, index));
        uintptr_t dummy = 0;
        if (!userdata || !signal_safe_read(reinterpret_cast<void*>(userdata), &dummy, sizeof(dummy)))
            return 0;

        uintptr_t signal = 0;
        signal_safe_read(reinterpret_cast<void*>(userdata + 16), &signal, sizeof(signal));

        if (!signal)
            signal_safe_read(reinterpret_cast<void*>(userdata), &signal, sizeof(signal));

        return signal;
    }

    inline void TrackConnection(lua_State* L, int signalIndex, uintptr_t conn_ptr, int funcRef)
    {
        if (!L || !conn_ptr || funcRef == -2)
            return;
        const uintptr_t identity = GetSignalIdentity(L, signalIndex);

        lua_pushthread(L);
        const int threadRef = lua_ref(L, -1);
        lua_pop(L, 1);

        TrackedConnection tc = {
            funcRef,
            threadRef,
            Shared::StateGeneration.load()
        };

        uintptr_t internal_conn = reinterpret_cast<connection_struct*>(conn_ptr)->connection;
        TrackedConnectionsByConn[internal_conn] = tc;

        if (identity) {
            TrackedConnectionsBySignal[identity].push_back(tc);
        }
    }

    static int tracked_fire_connection(lua_State* ls)
    {
        if (lua_gettop(ls) >= 1 && lua_istable(ls, 1))
            lua_remove(ls, 1);

        const auto nargs = ls->top - ls->base;

        lua_pushvalue(ls, lua_upvalueindex(1));
        if (lua_isfunction(ls, -1))
        {
            lua_State* co = lua_newthread(ls);
            lua_pushvalue(ls, -2);
            lua_xmove(ls, co, 1);
            lua_pop(ls, 1);

            for (int i = 1; i <= nargs; ++i)
                lua_pushvalue(ls, i);
            if (nargs > 0)
                lua_xmove(ls, co, nargs);

            lua_resume(co, ls, nargs);
        }
        lua_pop(ls, 1);

        return 0;
    }

    static int tracked_defer_connection(lua_State* ls)
    {
        if (lua_gettop(ls) >= 1 && lua_istable(ls, 1))
            lua_remove(ls, 1);

        const auto nargs = lua_gettop(ls);

        lua_getglobal(ls, "task");
        lua_getfield(ls, -1, "defer");
        lua_remove(ls, -2);

        lua_pushvalue(ls, lua_upvalueindex(1));
        if (!lua_isfunction(ls, -1))
            return 0;

        lua_insert(ls, 1);
        lua_insert(ls, 1);

        if (lua_pcall(ls, nargs + 1, 1, 0) != LUA_OK)
            luaL_error(ls, "Error in defer_connection: %s", lua_tostring(ls, -1));

        return 1;
    }

    inline void PushTrackedConnections(lua_State* L, int signalIndex)
    {
        const uintptr_t identity = GetSignalIdentity(L, signalIndex);
        const uint64_t generation = Shared::StateGeneration.load();
        lua_newtable(L);
        int outputIndex = 1;

        const auto found = TrackedConnectionsBySignal.find(identity);
        if (found == TrackedConnectionsBySignal.end())
            return;
        for (const TrackedConnection& connection : found->second)
        {
            if (connection.Generation != generation)
                continue;

            lua_newtable(L);
            lua_pushboolean(L, true);
            lua_setfield(L, -2, "Enabled");
            lua_pushboolean(L, false);
            lua_setfield(L, -2, "ForeignState");
            lua_pushboolean(L, true);
            lua_setfield(L, -2, "LuaConnection");

            lua_getref(L, connection.FunctionRef);
            lua_setfield(L, -2, "Function");
            lua_getref(L, connection.ThreadRef);
            lua_setfield(L, -2, "Thread");

            lua_getref(L, connection.FunctionRef);
            lua_pushcclosure(L, tracked_fire_connection, "Connection.Fire", 1);
            lua_setfield(L, -2, "Fire");
            lua_getref(L, connection.FunctionRef);
            lua_pushcclosure(L, tracked_defer_connection, "Connection.Defer", 1);
            lua_setfield(L, -2, "Defer");

            lua_pushcfunction(L, conn_noop, "Connection.Disable");
            lua_setfield(L, -2, "Disable");
            lua_pushcfunction(L, conn_noop, "Connection.Enable");
            lua_setfield(L, -2, "Enable");
            lua_pushcfunction(L, conn_noop, "Connection.Disconnect");
            lua_setfield(L, -2, "Disconnect");

            lua_rawseti(L, -2, outputIndex++);
        }
    }

    void checkIsA(lua_State* L, const char* idk) {
        lua_getfield(L, 1, "IsA");
        lua_pushvalue(L, 1);
        lua_pushstring(L, idk);
        lua_call(L, 2, 1);
    }

    int fireproximityprompt(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        lua_getglobal(L, "typeof");
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        const bool inst = (strcmp(lua_tolstring(L, -1, 0), "Instance") == 0);
        lua_pop(L, 1);

        if (!inst)
            luaL_argerror(L, 1, "Expected an Instance");

        lua_getfield(L, 1, "IsA");
        lua_pushvalue(L, 1);
        lua_pushstring(L, "ProximityPrompt");
        lua_call(L, 2, 1);
        const bool expected = lua_toboolean(L, -1);
        lua_pop(L, 1);

        if (!expected)
            luaL_argerror(L, 1, "Expected a ProximityPrompt");

        Hyperion::FireProximityPrompt(*reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 1)));
        return 0;
    }

    int fireclickdetector(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        auto FireDistance = luaL_optnumber(L, 2, 0);
        auto EventName = luaL_optstring(L, 3, "MouseClick");

        lua_getglobal(L, "game");
        lua_getfield(L, -1, "GetService");
        lua_insert(L, -2);
        lua_pushstring(L, "Players");
        lua_pcall(L, 2, 1, 0);
        lua_getfield(L, -1, "LocalPlayer");

        if (strcmp(EventName, "MouseClick") == 0)
        {
            Hyperion::FireMouseClick(*static_cast<void**>(lua_touserdata(L, 1)), FireDistance, *static_cast<void**>(lua_touserdata(L, -1)));
        }
        else if (strcmp(EventName, "RightMouseClick") == 0)
        {
            Hyperion::FireRightMouseClick(*static_cast<void**>(lua_touserdata(L, 1)), FireDistance, *static_cast<void**>(lua_touserdata(L, -1)));
        }
        else if (strcmp(EventName, "MouseHoverEnter") == 0)
        {
            Hyperion::FireMouseHover(*static_cast<void**>(lua_touserdata(L, 1)), *static_cast<void**>(lua_touserdata(L, -1)));
        }
        else if (strcmp(EventName, "MouseHoverLeave") == 0)
        {
            Hyperion::FireMouseHoverLeave(*static_cast<void**>(lua_touserdata(L, 1)), *static_cast<void**>(lua_touserdata(L, -1)));
        }

        return 0;
    }

    struct WalkResult {
        uintptr_t connections[256];
        int count;
    };

    static WalkResult walk_connections_safe(uintptr_t sentinelBase) {
        WalkResult result = {};
        result.count = 0;

        uintptr_t regionBase = 0;
        uintptr_t regionEnd = 0;
        __try {
            uintptr_t cur = *reinterpret_cast<uintptr_t*>(sentinelBase + ConnOffsets::Connection::next);
            while (cur && cur != sentinelBase && result.count < 256) {
                if (cur < regionBase ||
                    cur + ConnOffsets::Connection::next + sizeof(uintptr_t) > regionEnd) {
                    MEMORY_BASIC_INFORMATION info{};
                    if (!VirtualQuery(reinterpret_cast<void*>(cur), &info, sizeof(info)) ||
                        info.State != MEM_COMMIT ||
                        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                        break;
                    regionBase = reinterpret_cast<uintptr_t>(info.BaseAddress);
                    regionEnd = regionBase + info.RegionSize;
                    if (cur + ConnOffsets::Connection::next + sizeof(uintptr_t) > regionEnd)
                        break;
                }

                result.connections[result.count++] = cur;
                cur = *reinterpret_cast<uintptr_t*>(cur + ConnOffsets::Connection::next);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return result;
    }









    static uintptr_t resolve_signal_base(uintptr_t ud, int kind) {
        uintptr_t a = 0, b = 0;
        switch (kind) {
        case 0: signal_safe_read(reinterpret_cast<void*>(ud), &a, sizeof(a)); return a;
        case 1: signal_safe_read(reinterpret_cast<void*>(ud + 0x10), &a, sizeof(a)); return a;
        case 2: signal_safe_read(reinterpret_cast<void*>(ud + 0x18), &a, sizeof(a)); return a;
        case 3: signal_safe_read(reinterpret_cast<void*>(ud + 0x08), &a, sizeof(a)); return a;
        case 4:
            signal_safe_read(reinterpret_cast<void*>(ud + 0x10), &a, sizeof(a));
            if (a) signal_safe_read(reinterpret_cast<void*>(a), &b, sizeof(b));
            return b;
        case 5:
            signal_safe_read(reinterpret_cast<void*>(ud), &a, sizeof(a));
            if (a) signal_safe_read(reinterpret_cast<void*>(a + 0x08), &b, sizeof(b));
            return b;
        case 6:
            signal_safe_read(reinterpret_cast<void*>(ud), &a, sizeof(a));
            if (a) signal_safe_read(reinterpret_cast<void*>(a + 0x10), &b, sizeof(b));
            return b;
        case 7:
            signal_safe_read(reinterpret_cast<void*>(ud + 0x10), &a, sizeof(a));
            if (a) signal_safe_read(reinterpret_cast<void*>(a + 0x08), &b, sizeof(b));
            return b;
        case 8:
            signal_safe_read(reinterpret_cast<void*>(ud + 0x10), &a, sizeof(a));
            if (a) signal_safe_read(reinterpret_cast<void*>(a + 0x10), &b, sizeof(b));
            return b;
        }
        return 0;
    }
    static WalkResult walk_connections_fast(uintptr_t head) {
        WalkResult result = {};
        result.count = 0;
        uintptr_t regionBase = 0;
        uintptr_t regionEnd = 0;
        __try {
            uintptr_t cur = head;
            while (cur && result.count < 256) {
                if (cur < regionBase || cur + ConnOffsets::Connection::next + sizeof(uintptr_t) > regionEnd) {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (!VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi)) ||
                        mbi.State != MEM_COMMIT ||
                        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                        break;
                    regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                    regionEnd = regionBase + mbi.RegionSize;
                    if (cur + ConnOffsets::Connection::next + sizeof(uintptr_t) > regionEnd)
                        break;
                }
                result.connections[result.count++] = cur;
                cur = *reinterpret_cast<uintptr_t*>(cur + ConnOffsets::Connection::next);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return result;
    }

    struct SignalHeadLocation {
        int baseKind = -1;
        uintptr_t offset = 0;
        int attempts = 0;
        bool valid = false;
    };
    static const int kSignalBaseKinds = 9;
    static const int kMaxCalibrationAttempts = 4;
    inline SignalHeadLocation g_signalHead;

    struct HeadCandidate {
        int kind;
        uintptr_t offset;
    };
    static int collect_head_candidates(uintptr_t ud, uintptr_t sentinelBase, HeadCandidate* out, int maxOut) {
        int count = 0;
        for (int kind = 0; kind < kSignalBaseKinds && count < maxOut; kind++) {
            const uintptr_t base = resolve_signal_base(ud, kind);
            if (!base) continue;
            for (uintptr_t off = 0; off < 0x200 && count < maxOut; off += sizeof(uintptr_t)) {
                uintptr_t value = 0;
                if (!signal_safe_read(reinterpret_cast<void*>(base + off), &value, sizeof(value)))
                    break;
                if (value == sentinelBase)
                    out[count++] = { kind, off };
            }
        }
        return count;
    }
    static void finalize_head_calibration(uintptr_t ud, const HeadCandidate* candidates, int candidateCount,
        uintptr_t prevHead, int walkedCount) {
        int verified = 0;
        int verifiedKind = -1;
        uintptr_t verifiedOffset = 0;
        for (int i = 0; i < candidateCount; i++) {
            const uintptr_t base = resolve_signal_base(ud, candidates[i].kind);
            if (!base) continue;
            uintptr_t slot = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(base + candidates[i].offset), &slot, sizeof(slot)))
                continue;
            if (slot != prevHead)
                continue;
            if (walk_connections_fast(slot).count != walkedCount)
                continue;
            verified++;
            verifiedKind = candidates[i].kind;
            verifiedOffset = candidates[i].offset;
        }
        if (verified == 1) {
            g_signalHead.baseKind = verifiedKind;
            g_signalHead.offset = verifiedOffset;
            g_signalHead.valid = true;
        }
    }
    int getconnectionsV2(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const char* typeName = luaL_typename(L, 1);
        if (strcmp(typeName, "RBXScriptSignal") != 0)
            luaL_typeerror(L, 1, "RBXScriptSignal");

        g_slot_sweep_failed.clear();

        const uint64_t generation = Shared::StateGeneration.load();

        __try
        {
            WalkResult walked = {};
            bool walkedOk = false;

            if (g_signalHead.valid)
            {
                const uintptr_t ud = reinterpret_cast<uintptr_t>(lua_touserdata(L, 1));
                const uintptr_t base = resolve_signal_base(ud, g_signalHead.baseKind);
                uintptr_t head = 0;
                if (base &&
                    signal_safe_read(reinterpret_cast<void*>(base + g_signalHead.offset), &head, sizeof(head)) &&
                    (!head || IsPtrValid(head)))
                {
                    walked = walk_connections_fast(head);
                    walkedOk = true;
                }
            }

            if (!walkedOk)
            {
                lua_getfield(L, 1, "Connect");
                lua_pushvalue(L, 1);
                lua_pushcfunction(L, conn_noop, "getconn_stub");

                if (lua_pcall(L, 2, 1, 0) != 0) {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    __leave;
                }

                const auto sentinelUD = lua_touserdata(L, -1);
                if (!sentinelUD) {
                    lua_pop(L, 1);
                    PushTrackedConnections(L, 1);
                    __leave;
                }

                std::uintptr_t sentinelBase = 0;
                signal_safe_read(sentinelUD, &sentinelBase, sizeof(sentinelBase));
                if (!sentinelBase || !IsPtrValid(sentinelBase)) {
                    lua_getfield(L, -1, "Disconnect");
                    lua_insert(L, -2);
                    lua_pcall(L, 1, 0, 0);
                    PushTrackedConnections(L, 1);
                    __leave;
                }

                walked = walk_connections_safe(sentinelBase);

                const bool canCalibrate = !g_signalHead.valid && g_signalHead.attempts < kMaxCalibrationAttempts;
                uintptr_t prevHead = 0;
                HeadCandidate candidates[16];
                int candidateCount = 0;

                if (canCalibrate) {
                    g_signalHead.attempts++;
                    signal_safe_read(reinterpret_cast<void*>(sentinelBase + ConnOffsets::Connection::next), &prevHead, sizeof(prevHead));
                    candidateCount = collect_head_candidates(
                        reinterpret_cast<uintptr_t>(lua_touserdata(L, 1)), sentinelBase, candidates, 16);
                }

                lua_getfield(L, -1, "Disconnect");
                lua_insert(L, -2);
                lua_pcall(L, 1, 0, 0);

                if (canCalibrate && candidateCount > 0) {
                    finalize_head_calibration(
                        reinterpret_cast<uintptr_t>(lua_touserdata(L, 1)), candidates, candidateCount,
                        prevHead, walked.count);
                }
            }

            lua_createtable(L, walked.count, 0);
            int count = 1;

            const bool firedEmpty = g_fired_connections.empty();
            for (int i = 0; i < walked.count; i++) {
                const uintptr_t signal = walked.connections[i];
                if (!firedEmpty && g_fired_connections.count(signal)) continue;

                bool pushed = false;
                const auto cached = g_connection_cache.find(signal);
                if (cached != g_connection_cache.end()) {
                    if (cached->second.generation == generation && cached->second.ref > 0) {
                        lua_getref(L, cached->second.ref);
                        if (lua_istable(L, -1)) {
                            pushed = true;
                        }
                        else {
                            lua_pop(L, 1);
                            lua_unref(L, cached->second.ref);
                            g_connection_cache.erase(cached);
                        }
                    }
                    else {
                        g_connection_cache.erase(cached);
                    }
                }

                if (!pushed) {
                    if (!push_connection_mt_safe(L, signal))
                        continue;
                    const int ref = lua_ref(L, -1);
                    g_connection_cache[signal] = { ref, generation };
                }

                const auto tracked = TrackedConnectionsByConn.find(signal);
                if (tracked != TrackedConnectionsByConn.end() && tracked->second.Generation == generation) {
                    lua_pushnumber(L, tracked->second.FunctionRef);
                    lua_rawsetfield(L, -2, "_TFR");
                    lua_pushnumber(L, tracked->second.ThreadRef);
                    lua_rawsetfield(L, -2, "_TTR");
                }

                lua_rawseti(L, -2, count++);
            }

            if (count == 1) {
                lua_pop(L, 1);
                PushTrackedConnections(L, 1);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            lua_settop(L, lua_gettop(L) - (lua_gettop(L) > 1 ? 1 : 0));
            lua_newtable(L);
            return 1;
        }

        return 1;
    }















    int getconnections(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        if (strcmp(luaL_typename(L, 1), "RBXScriptSignal") != 0)
            luaL_typeerror(L, 1, "RBXScriptSignal");

        g_slot_sweep_failed.clear();

        const uint64_t generation = Shared::StateGeneration.load();

        lua_getfield(L, 1, "Connect");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, "Signal does not have 'Connect' method");
        }

        lua_pushvalue(L, 1);
        lua_pushcfunction(L, conn_noop, "getconnections_sentinel");
        if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            luaL_error(L, "%s", err ? err : "Connect failed");
        }

        void* ud = lua_touserdata(L, -1);
        if (!ud) {
            lua_pop(L, 1);
            luaL_error(L, "Connect did not return a valid connection object");
        }

        uintptr_t sentinel = 0;
        if (lua_type(L, -1) == LUA_TLIGHTUSERDATA)
            sentinel = reinterpret_cast<uintptr_t>(ud);
        else
            signal_safe_read(ud, &sentinel, sizeof(sentinel));

        if (!sentinel) {
            lua_getfield(L, -1, "Disconnect");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 0, 0);
            }
            else {
                lua_pop(L, 2);
            }
            lua_newtable(L);
            return 1;
        }

        WalkResult walked = walk_connections_safe(sentinel);

        lua_getfield(L, -1, "Disconnect");
        if (lua_isfunction(L, -1)) {
            lua_insert(L, -2);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                lua_pop(L, 1);
        }
        else {
            lua_pop(L, 2);
        }

        lua_createtable(L, walked.count, 0);
        int index = 1;
        for (int i = 0; i < walked.count; ++i) {
            const uintptr_t node = walked.connections[i];
            if (!node)
                continue;

            bool pushed = false;
            auto cached = g_connection_cache.find(node);
            if (cached != g_connection_cache.end()) {
                if (cached->second.generation == generation && cached->second.ref > 0) {
                    lua_getref(L, cached->second.ref);
                    if (lua_istable(L, -1)) {
                        pushed = true;
                    }
                    else {
                        lua_pop(L, 1);
                        lua_unref(L, cached->second.ref);
                        g_connection_cache.erase(cached);
                    }
                }
                else {
                    g_connection_cache.erase(cached);
                }
            }

            if (!pushed) {
                if (!push_connection_mt_safe(L, node))
                    continue;
                const int ref = lua_ref(L, -1);
                g_connection_cache[node] = { ref, generation };
            }

            lua_rawseti(L, -2, index++);
        }

        return 1;
    }

    struct CrossStateCopy
    {
        std::unordered_map<const void*, int> tableRefs;
        std::vector<int> refs;
    };

    static bool copy_signal_value(lua_State* from, int index, lua_State* to,
        CrossStateCopy& copy, int depth = 0)
    {
        index = lua_absindex(from, index);

        switch (lua_type(from, index))
        {
        case LUA_TNIL:
            lua_pushnil(to);
            return true;
        case LUA_TBOOLEAN:
            lua_pushboolean(to, lua_toboolean(from, index));
            return true;
        case LUA_TNUMBER:
            lua_pushnumber(to, lua_tonumber(from, index));
            return true;
        case LUA_TINTEGER:
            lua_pushinteger(to, lua_tointeger(from, index));
            return true;
        case LUA_TVECTOR:
        {
            const float* value = lua_tovector(from, index);
#if LUA_VECTOR_SIZE == 4
            lua_pushvector(to, value[0], value[1], value[2], value[3]);
#else
            lua_pushvector(to, value[0], value[1], value[2]);
#endif
            return true;
        }
        case LUA_TSTRING:
        {
            size_t length = 0;
            const char* value = lua_tolstring(from, index, &length);
            lua_pushlstring(to, value, length);
            return true;
        }
        case LUA_TBUFFER:
        {
            size_t length = 0;
            const void* value = lua_tobuffer(from, index, &length);
            void* result = lua_newbuffer(to, length);
            if (length)
                memcpy(result, value, length);
            return true;
        }
        case LUA_TUSERDATA:
            if (strcmp(luaL_typename(from, index), "Instance") == 0)
            {
                Hyperion::PushInstance(to, lua_touserdata(from, index));
                return !lua_isnil(to, -1);
            }
            lua_pushnil(to);
            return false;
        case LUA_TTABLE:
        {
            if (depth >= 32)
            {
                lua_pushnil(to);
                return false;
            }

            const void* identity = lua_topointer(from, index);
            const auto existing = copy.tableRefs.find(identity);
            if (existing != copy.tableRefs.end())
            {
                lua_getref(to, existing->second);
                return true;
            }

            lua_newtable(to);
            const int destination = lua_gettop(to);
            const int ref = lua_ref(to, destination);
            copy.tableRefs.emplace(identity, ref);
            copy.refs.push_back(ref);

            lua_pushnil(from);
            while (lua_next(from, index) != 0)
            {
                const int valueIndex = lua_gettop(from);
                const int keyIndex = valueIndex - 1;
                copy_signal_value(from, keyIndex, to, copy, depth + 1);
                copy_signal_value(from, valueIndex, to, copy, depth + 1);
                lua_settable(to, destination);
                lua_pop(from, 1);
            }
            return true;
        }
        default:
            // Functions, threads and engine-specific opaque userdata cannot be
            // shared safely between distinct global states.
            lua_pushnil(to);
            return false;
        }
    }

    static void release_cross_state_refs(lua_State* state, CrossStateCopy& copy)
    {
        for (const int ref : copy.refs)
            lua_unref(state, ref);
    }

    static bool is_foreign_state(lua_State* source, lua_State* target)
    {
        if (!source || !target)
            return false;

        global_State* sourceGlobal = nullptr;
        global_State* targetGlobal = nullptr;
        return signal_safe_read(&source->global, &sourceGlobal, sizeof(sourceGlobal)) &&
            signal_safe_read(&target->global, &targetGlobal, sizeof(targetGlobal)) &&
            sourceGlobal && targetGlobal && sourceGlobal != targetGlobal;
    }

    static bool fire_foreign_function(lua_State* source, lua_State* owner,
        Closure* function, int firstArg, int argCount)
    {
        if (!owner || !function || function->tt != LUA_TFUNCTION)
            return false;

        const int ownerTop = lua_gettop(owner);
        lua_State* callback = lua_newthread(owner);
        if (!callback)
            return false;

        setclvalue(callback, callback->top, function);
        incr_top(callback);

        CrossStateCopy copy;
        for (int i = 0; i < argCount; ++i)
            copy_signal_value(source, firstArg + i, callback, copy);

        const int status = lua_resume(callback, owner, argCount);
        if (status != LUA_OK && status != LUA_YIELD)
        {
            const char* error = lua_tostring(callback, -1);
            Hyperion::Print(3, "Foreign signal callback error: %s",
                error ? error : "unknown error");
        }

        release_cross_state_refs(callback, copy);
        lua_settop(owner, ownerTop);
        return status == LUA_OK || status == LUA_YIELD;
    }

    static bool resume_foreign_wait(lua_State* source, lua_State* waiting,
        int firstArg, int argCount)
    {
        if (!waiting || lua_status(waiting) != LUA_YIELD)
            return false;

        CrossStateCopy copy;
        for (int i = 0; i < argCount; ++i)
            copy_signal_value(source, firstArg + i, waiting, copy);

        lua_State* parent = waiting->global ? waiting->global->mainthread : nullptr;
        const int status = lua_resume(waiting, parent, argCount);
        if (status != LUA_OK && status != LUA_YIELD)
        {
            const char* error = lua_tostring(waiting, -1);
            Hyperion::Print(3, "Foreign signal wait error: %s",
                error ? error : "unknown error");
        }

        release_cross_state_refs(waiting, copy);
        return status == LUA_OK || status == LUA_YIELD;
    }

    int firesignal(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        if (strcmp(luaL_typename(L, 1), "RBXScriptSignal") != 0)
            luaL_typeerror(L, 1, "RBXScriptSignal");

        const int argCount = lua_gettop(L);

        getconnections(L);
        const int conns_idx = lua_gettop(L);

        if (!lua_istable(L, conns_idx)) {
            lua_pop(L, 1);
            return 0;
        }

        int fired = 0;
        lua_pushnil(L);
        while (lua_next(L, conns_idx)) {
            auto* con = lua_istable(L, -1) ? get_con_from_table(L, -1) : nullptr;
            if (con && con->connection)
            {
                lua_State* waiting = get_wait_thread(con->connection);
                if (waiting && waiting != L && lua_status(waiting) == LUA_YIELD)
                {
                    set_enabled(con, 0);
                    con->IsValid = false;

                    bool resumed = false;
                    if (is_foreign_state(L, waiting))
                    {
                        resumed = resume_foreign_wait(
                            L, waiting, 2, argCount - 1);
                    }
                    else
                    {
                        for (int i = 2; i <= argCount; ++i)
                        {
                            lua_pushvalue(L, i);
                            lua_xmove(L, waiting, 1);
                        }
                        const int status = lua_resume(
                            waiting, L, argCount - 1);
                        resumed = status == LUA_OK || status == LUA_YIELD;
                    }

                    if (resumed)
                    {
                        g_fired_connections.insert(con->connection);
                        fired++;
                    }
                    lua_pop(L, 1);
                    continue;
                }

                const uintptr_t weakRef = get_weak_object_ptr(con->connection);
                lua_State* owner = weakRef ? get_connection_thread(weakRef) : nullptr;
                Closure* function = weakRef ? get_connection_function(weakRef) : nullptr;

                if (owner && function && is_foreign_state(L, owner))
                {
                    if (fire_foreign_function(L, owner, function, 2, argCount - 1))
                    {
                        g_fired_connections.insert(con->connection);
                        fired++;
                    }
                    lua_pop(L, 1);
                    continue;
                }
            }

            lua_getfield(L, -1, "Function");
            bool hasFunction = !lua_isnil(L, -1) && lua_isfunction(L, -1);
            lua_pop(L, 1);

            if (hasFunction) {
                lua_getfield(L, -1, "Fire");
                if (lua_isfunction(L, -1)) {
                    lua_pushvalue(L, -2);
                    for (int i = 2; i <= argCount; i++)
                        lua_pushvalue(L, i);
                    lua_pcall(L, argCount, 0, 0);
                    fired++;
                }
                else {
                    lua_pop(L, 1);
                }
            }
            else {
                lua_getfield(L, -1, "Thread");
                if (lua_isthread(L, -1)) {
                    lua_State* co = lua_tothread(L, -1);
                    if (co && co != L && lua_status(co) == LUA_YIELD) {
                        for (int i = 2; i <= argCount; i++) {
                            lua_pushvalue(L, i);
                            lua_xmove(L, co, 1);
                        }
                        lua_resume(co, L, argCount - 1);
                        fired++;
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        lua_pop(L, 1);
        return 0;
    }

    int getconnection(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const int index = luaL_checkinteger(L, 2);
        if (index < 1)
            luaL_argerror(L, 2, "index must be greater than zero");

        getconnections(L);
        lua_rawgeti(L, -1, index);
        lua_remove(L, -2);
        return 1;
    }

    inline int setproximitypromptduration(lua_State* L)
    {
        if (!lua_isuserdata(L, 1) || !lua_isnumber(L, 2))
            return 0;

        lua_getfield(L, 1, "ClassName");
        const char* cn = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
        lua_pop(L, 1);

        if (!cn || strcmp(cn, "ProximityPrompt") != 0)
            return 0;

        double duration = lua_tonumber(L, 2);
        lua_pushnumber(L, duration);
        lua_setfield(L, 1, "HoldDuration");

        return 0;
    }

    inline int getproximitypromptduration(lua_State* L)
    {
        if (!lua_isuserdata(L, 1)) {
            lua_pushnumber(L, 0);
            return 1;
        }

        lua_getfield(L, 1, "ClassName");
        const char* cn = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
        lua_pop(L, 1);

        if (!cn || strcmp(cn, "ProximityPrompt") != 0) {
            lua_pushnumber(L, 0);
            return 1;
        }

        lua_getfield(L, 1, "HoldDuration");
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            lua_pushnumber(L, 0);
            return 1;
        }

        return 1;
    }

    namespace Roadblocks
    {
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

        enum class GameLoadedState : int32_t
        {
            Uninitialized = 0,
            Loading = 15,
            Loaded = 31,
        };

        enum MessageType {
            Output = 0,
            Info = 1,
            Warning = 2,
            Error = 3
        };

        struct TypeHolder
        {
            void(*construct)(const char*, char*);
            void(*moveConstruct)(char*, char*);
            void(*destruct)(char*);
        };

        struct RobloxType
        {
            uintptr_t* virtualTable;
            std::string* name;
            uintptr_t paddingZero[4];
            int typeIdentifier;
            uintptr_t paddingOne[2];
        };

        struct Variant
        {
            struct Storage
            {
                const TypeHolder* holder;
                const char data[64];
            };

            const RobloxType* type;
            Storage value;
        };

        enum class EventTargetInclusion : int32_t
        {
            OnlyTarget = 0,
            ExcludeTarget = 1
        };

        struct SystemAddress
        {
            struct PeerIdentifier
            {
                unsigned int peerIdentifier;
            };

            PeerIdentifier remoteIdentifier;
        };

        struct RemoteEventInvocationTargetOptions
        {
            const SystemAddress* target;
            EventTargetInclusion isExcludeTarget;
        };
    }

    inline bool IsPointerValid(const void* ptr, size_t size = sizeof(uintptr_t))
    {
        if (!ptr) return false;
        MEMORY_BASIC_INFORMATION buffer{};
        SIZE_T read = VirtualQuery(ptr, &buffer, sizeof(buffer));
        if (read != sizeof(buffer)) return false;
        if (buffer.State != MEM_COMMIT) return false;
        if ((buffer.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
        const DWORD valid_prot = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((buffer.Protect & valid_prot) == 0) return false;
        const uintptr_t region_start = reinterpret_cast<uintptr_t>(buffer.BaseAddress);
        const uintptr_t region_end = region_start + buffer.RegionSize;
        const uintptr_t ptr_value = reinterpret_cast<uintptr_t>(ptr);
        return ptr_value >= region_start && ptr_value + size <= region_end;
    }

    inline bool IsPointerValid(uintptr_t address, size_t size = sizeof(uintptr_t))
    {
        return IsPointerValid(reinterpret_cast<const void*>(address), size);
    }

    int replicatesignal(lua_State* luaState)
    {
        luaL_checktype(luaState, 1, LUA_TUSERDATA);

        if (strcmp(luaL_typename(luaState, 1), "RBXScriptSignal") != 0)
            luaL_typeerror(luaState, 1, "RBXScriptSignal");

        const uintptr_t eventInstance = *static_cast<uintptr_t*>(lua_touserdata(luaState, 1));
        const uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(eventInstance);
        const uintptr_t signature = *reinterpret_cast<uintptr_t*>(descriptor + 0x50);

        std::vector<Roadblocks::RobloxType*> parameterTypes;

        for (uintptr_t current = signature + 0x8; current; current += 0x70)
        {
            if (!IsPointerValid(reinterpret_cast<uintptr_t*>(current)))
                break;

            uintptr_t typePtr = *reinterpret_cast<uintptr_t*>(current);

            if (!typePtr || !IsPointerValid(reinterpret_cast<uintptr_t*>(typePtr)))
                break;

            parameterTypes.push_back(reinterpret_cast<Roadblocks::RobloxType*>(typePtr));
        }

        if (lua_gettop(luaState) > static_cast<int>(parameterTypes.size() + 1))
            lua_settop(luaState, static_cast<int>(parameterTypes.size() + 1));

        std::vector<Roadblocks::Variant> argumentValues;
        argumentValues.reserve(parameterTypes.size());

        int currentArg = 2;

        for (const auto* engineType : parameterTypes)
        {
            Roadblocks::Variant value{};
            Hyperion::CastArgs(luaState, currentArg, &value, true, 0);

            if (!value.type || value.type->typeIdentifier != engineType->typeIdentifier)
            {
                switch (engineType->typeIdentifier)
                {
                case 2:
                    value.type = engineType;
                    *reinterpret_cast<int32_t*>(const_cast<char*>(value.value.data)) = static_cast<int32_t>(lua_tonumber(luaState, currentArg));
                    break;
                case 3:
                    value.type = engineType;
                    *reinterpret_cast<int64_t*>(const_cast<char*>(value.value.data)) = llround(lua_tonumber(luaState, currentArg));
                    break;
                case 4:
                    value.type = engineType;
                    *reinterpret_cast<float*>(const_cast<char*>(value.value.data)) = static_cast<float>(lua_tonumber(luaState, currentArg));
                    break;
                default:
                    break;
                }
            }

            argumentValues.push_back(value);
            ++currentArg;
        }

        Roadblocks::RemoteEventInvocationTargetOptions options{};
        options.target = nullptr;
        options.isExcludeTarget = Roadblocks::EventTargetInclusion::OnlyTarget;

        const uintptr_t owningInstance = *reinterpret_cast<uintptr_t*>(eventInstance + 0x18);
        const uintptr_t owningVTable = *reinterpret_cast<uintptr_t*>(owningInstance);

        const auto raiseEventInvocation = *reinterpret_cast<void(**)(
            uintptr_t*,
            uintptr_t*,
            std::vector<Roadblocks::Variant>*,
            Roadblocks::RemoteEventInvocationTargetOptions*
            )>(owningVTable + 0x18);

        raiseEventInvocation(
            reinterpret_cast<uintptr_t*>(owningInstance),
            reinterpret_cast<uintptr_t*>(descriptor),
            &argumentValues,
            &options
        );

        return 0;
    }

    static void dispatch_touch_signal(lua_State* L, int ownerIndex,
        int otherIndex, const char* signalName)
    {
        const int originalTop = lua_gettop(L);
        lua_State* dispatchThread = lua_newthread(L);

        lua_getfield(L, ownerIndex, signalName);
        if (strcmp(luaL_typename(L, -1), "RBXScriptSignal") != 0)
        {
            lua_settop(L, originalTop);
            return;
        }
        lua_xmove(L, dispatchThread, 1);

        lua_pushvalue(L, otherIndex);
        lua_xmove(L, dispatchThread, 1);

        firesignal(dispatchThread);
        lua_settop(dispatchThread, 0);
        lua_settop(L, originalTop);
    }

    int firetouchinterest(lua_State* L) {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TUSERDATA);

        const bool touchEnded = lua_isboolean(L, 3)
            ? lua_toboolean(L, 3) != 0
            : luaL_checkinteger(L, 3) != 0;
        const char* signalName = touchEnded ? "TouchEnded" : "Touched";

        dispatch_touch_signal(L, 1, 2, signalName);
        dispatch_touch_signal(L, 2, 1, signalName);
        return 0;
    }
    int getsignalwhitelist(lua_State* L)
    {
        lua_newtable(L);
        int idx = 1;

        const char* signals[][2] = {
            {("Changed"), ("BasePart")},
            {("Touched"), ("BasePart")},
            {("TouchEnded"), ("BasePart")},
            {("ChildAdded"), ("Instance")},
            {("ChildRemoved"), ("Instance")},
            {("DescendantAdded"), ("Instance")},
            {("DescendantRemoving"), ("Instance")},
            {("AncestryChanged"), ("Instance")},
            {("PropertyChanged"), ("Instance")},
            {("GetPropertyChangedSignal"), ("Instance")},
            {("AttributeChanged"), ("Instance")},
            {("AttributeChanged"), ("Instance")},
            {("InputBegan"), ("UserInputService")},
            {("InputEnded"), ("UserInputService")},
            {("InputChanged"), ("UserInputService")},
            {("RenderStepped"), ("RunService")},
            {("Heartbeat"), ("RunService")},
            {("Stepped"), ("RunService")},
            {("Button1Down"), ("Mouse")},
            {("Button1Up"), ("Mouse")},
            {("Button2Down"), ("Mouse")},
            {("Button2Up"), ("Mouse")},
            {("Idle"), ("Mouse")},
            {("Move"), ("Mouse")},
            {("WheelForward"), ("Mouse")},
            {("WheelBackward"), ("Mouse")},
            {("CharacterAdded"), ("Players")},
            {("CharacterRemoving"), ("Players")},
            {("PlayerAdded"), ("Players")},
            {("PlayerRemoving"), ("Players")},
            {("Chatted"), ("Players")},
            {("MembershipChanged"), ("Players")},
        };

        for (auto& sig : signals) {
            lua_newtable(L);
            lua_pushstring(L, sig[0]);
            lua_setfield(L, -2, ("Event"));
            lua_pushstring(L, sig[1]);
            lua_setfield(L, -2, ("Parent"));
            lua_rawseti(L, -2, idx++);
        }

        return 1;
    }

    static const char* GetRobloxTypeName(int typeIdentifier)
    {
        switch (typeIdentifier)
        {
        case 1: return "string";
        case 2: return "bool";
        case 3: return "int";
        case 4: return "float";
        case 5: return "double";
        case 6: return "UDim";
        case 7: return "UDim2";
        case 8: return "BrickColor";
        case 9: return "Color3";
        case 10: return "Vector2";
        case 11: return "Vector3";
        case 12: return "CFrame";
        case 13: return "Ray";
        case 14: return "Enum";
        case 15: return "Axes";
        case 16: return "Faces";
        case 17: return "Instance";
        case 18: return "Dictionary";
        case 19: return "Array";
        case 20: return "NumberSequence";
        case 21: return "NumberSequenceKeypoint";
        case 22: return "ColorSequence";
        case 23: return "ColorSequenceKeypoint";
        case 24: return "NumberRange";
        case 25: return "Rect";
        case 26: return "Region3";
        case 27: return "Region3int16";
        case 28: return "Vector2int16";
        case 29: return "Vector3int16";
        case 30: return "PhysicalProperties";
        case 31: return "DockWidgetPluginGuiInfo";
        case 32: return "NumberSequenceKeypoint";
        case 33: return "rotation";
        case 34: return "PathWaypoint";
        case 35: return "TweenInfo";
        case 36: return "RaycastParams";
        case 37: return "Color3uint8";
        case 38: return "UniqueId";
        case 39: return "Font";
        default: return "Unknown";
        }
    }
    int cansignalreplicate(lua_State* L)
    {
        if (!lua_isuserdata(L, 1)) {
            lua_pushboolean(L, false);
            return 1;
        }

        if (strcmp(luaL_typename(L, 1), ("RBXScriptSignal")) != 0) {
            lua_pushboolean(L, false);
            return 1;
        }

        __try {
            uintptr_t userdata = reinterpret_cast<uintptr_t>(lua_touserdata(L, 1));
            uintptr_t eventInstance = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(userdata), &eventInstance, sizeof(eventInstance)) || !eventInstance || !IsPointerValid(eventInstance)) {
                lua_pushboolean(L, false);
                return 1;
            }

            uintptr_t owningInstance = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(eventInstance + 0x18), &owningInstance, sizeof(owningInstance)) || !owningInstance || !IsPointerValid(owningInstance)) {
                lua_pushboolean(L, false);
                return 1;
            }

            Hyperion::PushInstance(L, reinterpret_cast<void*>(owningInstance));
            lua_getfield(L, -1, ("ClassName"));
            const char* className = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
            lua_pop(L, 1);
            lua_pop(L, 1);

            if (!className) {
                lua_pushboolean(L, false);
                return 1;
            }

            bool replicable = (strcmp(className, ("RemoteEvent")) == 0 ||
                strcmp(className, ("RemoteFunction")) == 0 ||
                strcmp(className, ("UnvalidatedRemoteEvent")) == 0);
            lua_pushboolean(L, replicable);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            lua_pushboolean(L, false);
            return 1;
        }
    }
    static bool ReadSignalDescriptor(lua_State* L, int index, uintptr_t& outEventInstance, uintptr_t& outDescriptor, uintptr_t& outSignature)
    {
        if (!lua_isuserdata(L, index))
            return false;

        uintptr_t userdata = reinterpret_cast<uintptr_t>(lua_touserdata(L, index));

        uintptr_t eventInstance = 0;
        if (!signal_safe_read(reinterpret_cast<void*>(userdata), &eventInstance, sizeof(eventInstance)))
            return false;
        if (!eventInstance || !IsPointerValid(eventInstance))
            return false;

        uintptr_t descriptor = 0;
        if (!signal_safe_read(reinterpret_cast<void*>(eventInstance), &descriptor, sizeof(descriptor)))
            return false;
        if (!descriptor || !IsPointerValid(descriptor))
            return false;

        uintptr_t signature = 0;
        if (!signal_safe_read(reinterpret_cast<void*>(descriptor + 0x50), &signature, sizeof(signature)))
            return false;
        if (!signature || !IsPointerValid(signature))
            return false;

        outEventInstance = eventInstance;
        outDescriptor = descriptor;
        outSignature = signature;
        return true;
    }

    int getsignalargumentsinfo(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TUSERDATA);

        if (strcmp(luaL_typename(L, 1), ("RBXScriptSignal")) != 0)
            luaL_typeerror(L, 1, (("RBXScriptSignal")));

        uintptr_t eventInstance, descriptor, signature;
        if (!ReadSignalDescriptor(L, 1, eventInstance, descriptor, signature)) {
            lua_newtable(L);
            return 1;
        }

        lua_newtable(L);
        int idx = 1;

        for (uintptr_t current = signature + 0x8; current; current += 0x70)
        {
            if (!IsPointerValid(reinterpret_cast<uintptr_t*>(current)))
                break;

            uintptr_t typePtr = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(current), &typePtr, sizeof(typePtr)))
                break;
            if (!typePtr || !IsPointerValid(typePtr))
                break;

            int typeIdentifier = 0;
            signal_safe_read(reinterpret_cast<void*>(typePtr + 0x30), &typeIdentifier, sizeof(typeIdentifier));

            uintptr_t namePtr = 0;
            signal_safe_read(reinterpret_cast<void*>(typePtr + 0x08), &namePtr, sizeof(namePtr));

            char nameBuf[256] = { 0 };
            if (namePtr && IsPointerValid(namePtr)) {
                signal_safe_read(reinterpret_cast<void*>(namePtr), nameBuf, sizeof(nameBuf) - 1);
            }

            lua_newtable(L);
            lua_pushstring(L, nameBuf[0] ? nameBuf : ("Unknown"));
            lua_setfield(L, -2, ("Name"));
            lua_pushstring(L, GetRobloxTypeName(typeIdentifier));
            lua_setfield(L, -2, ("Type"));
            lua_rawseti(L, -2, idx++);
        }

        return 1;
    }

    int getsignalarguments(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TUSERDATA);

        if (strcmp(luaL_typename(L, 1), ("RBXScriptSignal")) != 0)
            luaL_typeerror(L, 1, (("RBXScriptSignal")));

        uintptr_t eventInstance, descriptor, signature;
        if (!ReadSignalDescriptor(L, 1, eventInstance, descriptor, signature)) {
            lua_newtable(L);
            return 1;
        }

        lua_newtable(L);
        int idx = 1;

        for (uintptr_t current = signature + 0x8; current; current += 0x70)
        {
            if (!IsPointerValid(reinterpret_cast<uintptr_t*>(current)))
                break;

            uintptr_t typePtr = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(current), &typePtr, sizeof(typePtr)))
                break;
            if (!typePtr || !IsPointerValid(typePtr))
                break;

            uintptr_t namePtr = 0;
            if (!signal_safe_read(reinterpret_cast<void*>(typePtr + 0x08), &namePtr, sizeof(namePtr)))
                break;

            if (namePtr && IsPointerValid(namePtr)) {
                char nameBuf[256] = { 0 };
                if (signal_safe_read(reinterpret_cast<void*>(namePtr), nameBuf, sizeof(nameBuf) - 1)) {
                    lua_pushstring(L, nameBuf);
                    lua_rawseti(L, -2, idx++);
                }
            }
        }

        return 1;
    }
    struct RenderStepBinding
    {
        std::string Name;
        double Priority = 0.0;
        int FunctionRef = LUA_NOREF;
        int ThreadRef = LUA_NOREF;
    };

    static std::vector<RenderStepBinding> RenderStepBindings;
    void ForgetRenderStepBinding(lua_State* L, const std::string& name)
    {
        for (auto it = RenderStepBindings.begin(); it != RenderStepBindings.end(); ++it)
        {
            if (it->Name == name)
            {
                lua_unref(L, it->FunctionRef);
                lua_unref(L, it->ThreadRef);
                RenderStepBindings.erase(it);
                return;
            }
        }
    }


    void RecordRenderStepBinding(lua_State* L)
    {
        int nameIdx = 2;
        int prioIdx = 3;
        int funcIdx = 4;

        if (lua_isstring(L, 1) && (lua_isfunction(L, 3) || lua_isfunction(L, 4)))
        {
            nameIdx = 1;
            prioIdx = 2;
            funcIdx = 3;
        }

        if (!lua_isstring(L, nameIdx) || !lua_isfunction(L, funcIdx))
            return;

        RenderStepBinding binding;
        binding.Name = lua_tostring(L, nameIdx);

        double priority = 0.0;
        if (lua_isnumber(L, prioIdx))
            priority = lua_tonumber(L, prioIdx);
        else if (lua_isuserdata(L, prioIdx))
        {
            lua_getfield(L, prioIdx, "Value");
            if (lua_isnumber(L, -1))
                priority = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        binding.Priority = priority;

        ForgetRenderStepBinding(L, binding.Name);

        lua_pushvalue(L, funcIdx);
        binding.FunctionRef = lua_ref(L, -1);
        lua_pop(L, 1);

        lua_pushthread(L);
        binding.ThreadRef = lua_ref(L, -1);
        lua_pop(L, 1);

        RenderStepBindings.push_back(std::move(binding));
    }

    void ForgetRenderStepBindingByArg(lua_State* L)
    {
        if (lua_isstring(L, 2))
            ForgetRenderStepBinding(L, lua_tostring(L, 2));
        else if (lua_isstring(L, 1))
            ForgetRenderStepBinding(L, lua_tostring(L, 1));
    }

    int getrendersteppedlist(lua_State* L)
    {
        lua_newtable(L);
        const int resultIndex = lua_gettop(L);
        int counter = 1;

        for (const auto& binding : RenderStepBindings)
        {
            lua_getref(L, binding.FunctionRef);
            if (!lua_isfunction(L, -1))
            {
                lua_pop(L, 1);
                continue;
            }

            lua_newtable(L);

            lua_pushlstring(L, binding.Name.data(), binding.Name.size());
            lua_setfield(L, -2, "Name");

            lua_pushnumber(L, binding.Priority);
            lua_setfield(L, -2, "Priority");

            lua_pushvalue(L, -2);
            lua_setfield(L, -2, "Function");

            lua_getref(L, binding.ThreadRef);
            if (lua_isthread(L, -1))
                lua_setfield(L, -2, "Thread");
            else
                lua_pop(L, 1);

            lua_rawseti(L, resultIndex, counter++);
            lua_pop(L, 1);
        }

        lua_settop(L, resultIndex);
        return 1;
    }
    uintptr_t get_weak_object_ptr_signal(uintptr_t signalPtr) {
        const auto slot_wrapper = *reinterpret_cast<std::uintptr_t*>(signalPtr + Offsets::connection::slot_wrapper);
        if (!slot_wrapper)
            return NULL;

        std::uintptr_t weak_object_ptr = NULL;
        if (slot_wrapper == (*reinterpret_cast<std::uintptr_t*>(signalPtr + Offsets::connection::slot_wrapper_self) + 0x18)) {
            weak_object_ptr = *reinterpret_cast<std::uintptr_t*>(slot_wrapper + Offsets::slot_wrapper::weak_object_ref);
        }

        return weak_object_ptr;
    }
    lua_State* get_connection_thread_signal(std::uintptr_t weak_obj) {
        const auto thread = *reinterpret_cast<lua_State**>(weak_obj + Offsets::slot_wrapper::thread);

        if (!thread)
            return NULL;

        if (thread->tt != LUA_TTHREAD)
            return NULL;

        return thread;
    };
    Closure* get_connection_function_signal(std::uintptr_t weak_obj) {
        const auto function_id = *reinterpret_cast<std::int32_t*>(weak_obj + Offsets::slot_wrapper::function_id);
        if (!function_id) return 0;

        lua_State* thread = get_connection_thread_signal(weak_obj);
        if (!thread) return 0;

        lua_getref(thread, function_id);
        if (lua_isfunction(thread, -1)) {
            auto cl = clvalue(luaA_toobject(thread, -1));
            lua_pop(thread, 1);
            return cl;
        }

        lua_pop(thread, 1);

        return 0;
    };

    int Defersignal(lua_State* L) {

        luaL_checktype(L, 1, LUA_TUSERDATA);

        std::string Check = luaL_typename(L, 1);
        if (Check != ("RBXScriptSignal"))
            luaL_argerror(L, 1, (("Expected RBXScriptSignal got ") + Check).c_str());

        lua_getfield(L, 1, ("Connect"));
        lua_pushvalue(L, 1);
        lua_pushcclosure(L, [](lua_State*) -> int { return 0; }, 0, 0);
        lua_pcall(L, 2, 1, 0);

        const auto UserData = *reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, -1));
        auto signal = *reinterpret_cast<std::uintptr_t*>(UserData + Offsets::connection::next);

        lua_getfield(L, -1, ("Disconnect"));
        lua_insert(L, -2);
        lua_pcall(L, 1, 0, 0);
        lua_remove(L, 1);

        if (!signal)
            return 0;

        int ArgumentCount = lua_gettop(L);

        auto count = 1;

        do {
            const auto weak_ref = get_weak_object_ptr_signal(signal);
            if (weak_ref) {
                const auto function = get_connection_function_signal(weak_ref);
                if (function) {
                    setclvalue(L, L->top, function);
                    L->top++;

                    for (int i = 1; i <= ArgumentCount; i++)
                        lua_pushvalue(L, i);

                    Hyperion::TaskDeferT(L);
                }
            }

            signal = *reinterpret_cast<std::uintptr_t*>(signal + Offsets::connection::next);
        } while (signal != 0);

        return 0;
    };

    inline std::mutex RenderStepMutex;


    inline void RegisterLibrary(lua_State* L)
    {
        if (!L) return;

        luaL_newmetatable(L, "connection_signal_object");
        lua_pushcfunction(L, connection_index, "connection_index");
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, connection_newindex, "connection_newindex");
        lua_setfield(L, -2, "__newindex");
        lua_pushstring(L, "Connection");
        lua_setfield(L, -2, "__type");
        lua_pop(L, 1);

        Utils::AddFunction(L, "fireproximityprompt", fireproximityprompt);
        Utils::AddFunction(L, "fireclickdetector", fireclickdetector);
        Utils::AddFunction(L, "firetouchinterest", firetouchinterest);
        Utils::AddFunction(L, "getconnections", getconnectionsV2);
        Utils::AddFunction(L, "get_signal_cons", getconnections);
        Utils::AddFunction(L, "getconnection", getconnection);
        Utils::AddFunction(L, "firesignal", firesignal);
        Utils::AddFunction(L, "replicatesignal", replicatesignal);
        Utils::AddFunction(L, "setproximitypromptduration", setproximitypromptduration);
        Utils::AddFunction(L, "getproximitypromptduration", getproximitypromptduration);
        Utils::AddFunction(L, "getsignalwhitelist", getsignalwhitelist);
        Utils::AddFunction(L, "cansignalreplicate", cansignalreplicate);
        Utils::AddFunction(L, "getsignalargumentsinfo", getsignalargumentsinfo);
        Utils::AddFunction(L, "getsignalarguments", getsignalarguments);
        Utils::AddFunction(L, "getrendersteppedlist", getrendersteppedlist);

        Utils::AddFunction(L, "defersignal", Defersignal);
    }
}
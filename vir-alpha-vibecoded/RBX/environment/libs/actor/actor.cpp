#include "actor.hpp"

#include <lstate.h>
#include <lualib.h>
#include <lgc.h>
#include <lapi.h>
#include <lobject.h>
#include <Windows.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>

#include <RBX/environment/env.hpp>
#include <RBX/execution/execution.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/update.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>

#include <RBX/environment/libs/http/http.hpp>
#include <RBX/environment/libs/crypt/crypt.hpp>
#include <RBX/environment/libs/input/input.hpp>
#include <RBX/environment/libs/debug/debug.hpp>
#include <RBX/environment/libs/cache/cache.hpp>
#include <RBX/environment/libs/signals/signals.hpp>
#include <RBX/environment/libs/script/script.hpp>
#include <RBX/environment/libs/closures/closures.hpp>
#include <RBX/environment/libs/instance/instance.hpp>
#include <RBX/environment/libs/websocket/websocket.hpp>
#include <RBX/environment/libs/metatable/metatable.hpp>
#include <RBX/environment/libs/filesystem/filesystem.hpp>
#include <RBX/environment/libs/reflection/reflection.hpp>
#include <RBX/environment/libs/miscellaneous/miscellaneous.hpp>
#include <RBX/environment/libs/regex/regex.hpp>
#include <RBX/environment/libs/filedialog/filedialog.hpp>

// ── signal_safe_read ──────────────────────────────────────────
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

// POD-only guarded Instance push (C2712 forbids __try in functions owning
// RAII; deepCopy below owns unordered_map/vector so it cannot use __try
// directly). Probes the userdata payload first; a stale/freed payload fails
// closed (returns false, caller pushes nil) instead of AVing inside the
// game's PushInstance bridge.
static bool push_actor_instance_guarded(lua_State* dst, void* udPayload, int dstTopBefore)
{
    __try {
        Roblox::Void2(dst, udPayload);
        return !lua_isnil(dst, -1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // PushInstance faulted mid-push: restore balance then normalize to
        // a single nil so the caller's stack discipline holds.
        __try {
            lua_settop(dst, dstTopBefore);
            lua_pushnil(dst);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

// TMP stage marker (temporary diagnostics for the get_comm_channel crash).
// File-append, no locks, mirrors the ViridianLog pattern in
// task_scheduler.cpp. Prefix TMP: so the next live run names the exact
// tail stage. Remove once the crash is pinned.
static inline void tmp_actor_stage(const char* msg)
{
    char bb[MAX_PATH]{};
    DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
    if (!bn || bn >= MAX_PATH) return;
    std::string bp = std::string(bb) + "\\Viridian\\boot.log";
    FILE* bf = nullptr;
    if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf) return;
    fprintf(bf, "TMP: [actor] run_on_actor: %s\n", msg);
    fclose(bf);
}

namespace Actor
{
    // ── Pre-compiled isparallel Lua wrapper (hooks task.desynchronize/synchronize) ──
    static std::string s_isparallelBytecode;
    static bool s_isparallelBytecodeReady = false;

    static const char* isparallel_wrapper_src = R"LUA(
local parallel_threads = setmetatable({}, { __mode = "k" })

local ok1, _orig_desync = pcall(function()
    return task.desynchronize
end)
local ok2, _orig_sync = pcall(function()
    return task.synchronize
end)

if ok1 and _orig_desync then
    task.desynchronize = function(...)
        _orig_desync(...)
        parallel_threads[coroutine.running()] = true
    end
end

if ok2 and _orig_sync then
    task.synchronize = function(...)
        _orig_sync(...)
        parallel_threads[coroutine.running()] = nil
    end
end

isparallel = function()
    return parallel_threads[coroutine.running()] == true
end
)LUA";

    static void inject_isparallel_wrapper(lua_State* thread, lua_State* from)
    {
        if (!thread || !from) return;
        // On-demand compile: s_isparallelBytecode is never pre-filled, so the
        // old early-return disabled the wrapper entirely and actor threads
        // kept the C isparallel (fail-closed false), failing Myriad's
        // `task.desynchronize(); if isparallel() then <make Part>` true-path.
        // Compile once here (pure compute, no locks, no Lua API under locks);
        // on failure return and keep the C global (false, safe).
        if (!s_isparallelBytecodeReady || s_isparallelBytecode.empty()) {
            try {
                std::string bc = Execution::CompileScript(isparallel_wrapper_src);
                if (bc.empty()) return;
                s_isparallelBytecode = std::move(bc);
                s_isparallelBytecodeReady = true;
            }
            catch (...) { return; }
        }
        if (s_isparallelBytecode.empty()) return;
        if (!thread->userdata) return;
        const int baseTop = lua_gettop(thread);
        if (luau_load(thread, "=isparallel_init", s_isparallelBytecode.data(), s_isparallelBytecode.size(), NULL) != LUA_OK) {
            // Balance: load failure pushes an error string; drop it so the
            // actor thread stack is unchanged for the caller.
            lua_pop(thread, 1);
            return;
        }
        // luau_load success pushes one value; prove Lclosure before touching
        // Closure internals (clvalue on a non-closure misreads the TValue
        // and AVs on ->l.p).
        if (!lua_isfunction(thread, -1)) { lua_pop(thread, 1); return; }
        const TValue* o = luaA_toobject(thread, -1);
        Closure* cl = o ? clvalue(o) : nullptr;
        if (!cl || cl->isC || !cl->l.p) { lua_pop(thread, 1); return; }
        task_scheduler::SetProtoCapabilities(cl->l.p, &MaxCapabilities);
        // Resume status ignored: wrapper only installs hooks; a runtime error
        // inside it must not fail run_on_actor. Stack always reset.
        lua_resume(thread, from, 0);
        lua_settop(thread, baseTop);
    }

    // ═══════════════════════════════════════════════════════════
    //  getactors
    // ═══════════════════════════════════════════════════════════
    int getactors(lua_State* L)
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

    // ═══════════════════════════════════════════════════════════
    //  isparallel — C-level read of actor parallel flag
    // ═══════════════════════════════════════════════════════════
    int isparallel(lua_State* L)
    {
        // L is never null on a real call; if it somehow is, there is no
        // stack to push to — return with no values instead of AVing.
        if (!L) return 0;

        // Prove every hop readable before touching it. Never call .lock()
        // on the foreign shared_ptr (its control block may be garbage on a
        // non-actor thread) — read only the stored pointer, then the actor
        // value behind it. Anything unpro readable fails closed to false.
        RobloxExtraSpace* extra = nullptr;
        if (!signal_safe_read(&L->userdata, &extra, sizeof(extra)) || !extra) {
            lua_pushboolean(L, false);
            return 1;
        }
        std::uintptr_t cell = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(&extra->actor), &cell, sizeof(cell)) || !cell) {
            lua_pushboolean(L, false);
            return 1;
        }
        std::uintptr_t actor = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(cell), &actor, sizeof(actor)) || !actor) {
            lua_pushboolean(L, false);
            return 1;
        }

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

    // ═══════════════════════════════════════════════════════════
    //  run_on_actor
    // ═══════════════════════════════════════════════════════════
    // POD-only guarded write (C2712 forbids __try in functions owning
    // RAII objects, so this lives outside run_on_actor).
    static bool write_parent_slot(std::uintptr_t parentSlot, std::uintptr_t actor)
    {
        __try {
            *reinterpret_cast<std::uintptr_t*>(parentSlot) = actor;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    int run_on_actor(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TSTRING);

        // ClassName may be absent or non-string (lua_tolstring -> NULL).
        // Never build a string_view over possibly-NULL: validate, then copy
        // to an owned string. Unreadable/empty fails closed as "not an Actor".
        lua_getfield(L, 1, "ClassName");
        size_t cnLen = 0;
        const char* cnRaw = lua_tolstring(L, -1, &cnLen);
        std::string class_name;
        if (cnRaw && cnLen > 0) {
            class_name.resize(cnLen);
            if (!signal_safe_read(cnRaw, class_name.data(), cnLen))
                class_name.clear();
        }
        lua_pop(L, 1);

        if (class_name != "Actor") {
            luaL_argerror(L, 1, "Actor expected");
            return 0;
        }

        // Actor userdata payload holds the game Instance pointer by value.
        // The userdata may be stale (actor destroyed/freed): prove the
        // payload read and the value before touching game memory.
        void* actorUd = lua_touserdata(L, 1);
        if (!actorUd) {
            luaL_argerror(L, 1, "stale Actor");
            return 0;
        }
        __int64 actor = 0;
        if (!signal_safe_read(actorUd, &actor, sizeof(actor)) || !actor) {
            luaL_argerror(L, 1, "stale Actor");
            return 0;
        }

        // Copy source to an owned string BEFORE lua_remove drops the stack
        // refs (the raw pointer would dangle on GC); checktype above makes
        // NULL impossible, this is fail-closed hardening.
        size_t codeLen = 0;
        const char* codeRaw = lua_tolstring(L, 2, &codeLen);
        if (!codeRaw) {
            luaL_argerror(L, 2, "string expected");
            return 0;
        }
        std::string Code(codeRaw, codeLen);

        lua_remove(L, 2);
        lua_remove(L, 1);
        int Args = lua_gettop(L);

        lua_getglobal(L, "Instance");
        lua_getfield(L, -1, "new");
        lua_pushstring(L, "LocalScript");
        lua_call(L, 1, 1);
        // Instance.new result must be userdata; the payload pointer it holds
        // must prove readable and non-zero (stale/freed script object).
        if (!lua_isuserdata(L, -1)) {
            luaL_error(L, "run_on_actor: Instance.new failed");
            return 0;
        }
        void* lsUd = lua_touserdata(L, -1);
        std::uintptr_t LocalScript = 0;
        if (!lsUd || !signal_safe_read(lsUd, &LocalScript, sizeof(LocalScript)) || !LocalScript) {
            luaL_error(L, "run_on_actor: bad LocalScript");
            return 0;
        }
        // Parent slot write: prove the slot readable first, then guarded
        // write — a freed/read-only page fails as a Lua error, never an AV.
        const std::uintptr_t parentSlot = LocalScript + Offsets::Instance::Parent;
        std::uintptr_t parentProbe = 0;
        if (!signal_safe_read(reinterpret_cast<const void*>(parentSlot), &parentProbe, sizeof(parentProbe))) {
            luaL_error(L, "run_on_actor: bad LocalScript");
            return 0;
        }
        if (!write_parent_slot(parentSlot, static_cast<std::uintptr_t>(actor))) {
            luaL_error(L, "run_on_actor: bad LocalScript");
            return 0;
        }

        // L->userdata->shared->scriptContext: NULL-check every link. The
        // shared_ptr read assumes the MSVC layout (stored pointer first);
        // each step is proven readable so a dead session errors, not faults.
        if (!L) return 0;
        RobloxExtraSpace* extra = nullptr;
        if (!signal_safe_read(&L->userdata, &extra, sizeof(extra)) || !extra) {
            luaL_error(L, "run_on_actor: no execution context");
            return 0;
        }
        SharedExtraSpace* sharedRaw = nullptr;
        if (!signal_safe_read(&extra->shared, &sharedRaw, sizeof(sharedRaw)) || !sharedRaw) {
            luaL_error(L, "run_on_actor: no execution context");
            return 0;
        }
        void* scRaw = nullptr;
        if (!signal_safe_read(&sharedRaw->scriptContext, &scRaw, sizeof(scRaw)) || !scRaw) {
            luaL_error(L, "run_on_actor: no execution context");
            return 0;
        }
        const std::uintptr_t ScriptContext = reinterpret_cast<std::uintptr_t>(scRaw);
        uintptr_t unused = 0;
        lua_State* ActorState = reinterpret_cast<lua_State*>(
            task_scheduler::GetLuaState(ScriptContext, unused, LocalScript));
        // Never lua_newthread(NULL): GetLuaState fails on teleport/dead
        // ScriptContext — surface a catchable Lua error instead.
        if (!ActorState) {
            luaL_error(L, "run_on_actor: no actor thread (GetLuaState failed)");
            return 0;
        }

        lua_State* NL = lua_newthread(ActorState);
        if (!NL || !NL->userdata) {
            luaL_error(L, "run_on_actor: no actor thread (newthread failed)");
            return 0;
        }
        // Anchor NL against GC: lua_newthread pushes the thread; a bare pop
        // leaves it unrooted so the next allocation (register_lib tables,
        // CompileScript, luau_load) can collect it from under us — a UAF that
        // surfaces later as a get_comm_channel-time crash. Hold a registry
        // ref until after TaskDefer (mirrors Execution::ExecuteScript).
        int nlRef = lua_ref(ActorState, -1);
        lua_pop(ActorState, 1);
        if (nlRef == LUA_NOREF || nlRef == LUA_REFNIL) {
            luaL_error(L, "run_on_actor: no actor thread (ref failed)");
            return 0;
        }
        // Actor slot is weak_ptr: a make_shared temporary would expire at the
        // end of the full-expression, leaving isparallel's raw read pointed at
        // freed storage (UAF/garbage). Keep the shared owner alive.
        auto actorHold = std::make_shared<uintptr_t>(static_cast<uintptr_t>(actor));
        NL->userdata->actor = actorHold;
        {
            static std::mutex s_holdMutex;
            static std::vector<std::shared_ptr<uintptr_t>> s_holds;
            std::lock_guard<std::mutex> _g(s_holdMutex);
            s_holds.push_back(std::move(actorHold));
        }
        task_scheduler::SetThreadCapabilities(NL, 8, MaxCapabilities);
        luaL_sandboxthread(NL);
        lua_settop(NL, 0);

        lua_newtable(NL); lua_setglobal(NL, "_G");
        lua_newtable(NL); lua_setglobal(NL, "shared");

        Http::register_lib(NL);
        Crypt::register_lib(NL);
        Input::register_lib(NL);
        Debug::register_lib(NL);
        Cache::register_lib(NL);
        Signals::register_lib(NL);
        Script::register_lib(NL);
        Closures::register_lib(NL);
        Instance::register_lib(NL);
        WebSocket::register_lib(NL);
        Metatable::register_lib(NL);
        Filesystem::register_lib(NL);
        Reflection::register_lib(NL);
        Miscellaneous::register_lib(NL);
        Regex::register_lib(NL);
        FileDialog::register_lib(NL);

        // Register comm channel functions on actor thread
        Environment->make_global(NL, "get_comm_channel", Actor::get_comm_channel, 0);
        Environment->make_global(NL, "create_comm_channel", Actor::create_comm_channel, 0);

        tmp_actor_stage("libs registered");

        // Inject isparallel Lua wrapper (overrides C-level with coroutine-aware version)
        inject_isparallel_wrapper(NL, L);

        // Deep copy helper — cross-state safe.
        // L (exploit thread) and NL (actor thread) are DIFFERENT global
        // states. lua_xmove across globals moves a GC pointer into a foreign
        // GC (heap corruption surfacing as a later get_comm_channel crash),
        // and INTEGER args fell into the old default: branch (only NUMBER was
        // handled) so every integer channel id corrupted the actor stack.
        // This version copies by value, pushes nil for non-transferable types
        // (function/thread/etc, mirroring signals.cpp), caps depth at 64 and
        // tracks cycles via registry refs — all without luaL_error (longjmp
        // through this std::function's frames is UB and kills the client).
        std::unordered_map<const void*, int> copySeen;
        std::vector<int> copyRefs;
        std::function<void(lua_State*, lua_State*, int, int)> deepCopy =
            [&](lua_State* src, lua_State* dst, int idx, int depth)
            {
                if (!src || !dst) return;
                if (depth > 64) { lua_pushnil(dst); return; }
                int absIdx = lua_absindex(src, idx);
                int t = lua_type(src, absIdx);
                switch (t)
                {
                case LUA_TTABLE: {
                    const void* ident = lua_topointer(src, absIdx);
                    auto it = copySeen.find(ident);
                    if (it != copySeen.end()) {
                        lua_getref(dst, it->second);
                        return;
                    }
                    lua_newtable(dst);
                    int destAbs = lua_gettop(dst);
                    int ref = lua_ref(dst, destAbs);
                    if (ref == LUA_NOREF || ref == LUA_REFNIL) return; // table already pushed
                    copySeen.emplace(ident, ref);
                    copyRefs.push_back(ref);
                    lua_pushnil(src);
                    while (lua_next(src, absIdx)) {
                        deepCopy(src, dst, -2, depth + 1);
                        deepCopy(src, dst, -1, depth + 1);
                        lua_settable(dst, destAbs);
                        lua_pop(src, 1);
                    }
                    break;
                }
                case LUA_TUSERDATA: {
                    const char* tn = luaL_typename(src, absIdx);
                    if (tn && strcmp(tn, "Instance") == 0) {
                        void* ud = lua_touserdata(src, absIdx);
                        void* payload = nullptr;
                        if (ud && signal_safe_read(ud, &payload, sizeof(payload)) && payload) {
                            int topBefore = lua_gettop(dst);
                            // Guarded bridge: stale Instance AVs inside game
                            // code instead of returning; fail closed to nil.
                            // Pass a tick-owned slot holding the native
                            // (same *slot == native contract as the comm
                            // path) so the bridge never touches the foreign
                            // heap inside game code.
                            void* slotCopy = payload;
                            push_actor_instance_guarded(dst, &slotCopy, topBefore);
                        }
                        else {
                            lua_pushnil(dst);
                        }
                    }
                    else {
                        // Non-Instance userdata cannot cross states (xmove
                        // would corrupt the foreign GC): nil, like signals.
                        lua_pushnil(dst);
                    }
                    break;
                }
                case LUA_TSTRING: {
                    size_t len = 0;
                    const char* str = lua_tolstring(src, absIdx, &len);
                    if (!str) { lua_pushnil(dst); break; }
                    lua_pushlstring(dst, str, len);
                    break;
                }
                case LUA_TNUMBER: lua_pushnumber(dst, lua_tonumber(src, absIdx)); break;
                case LUA_TINTEGER: lua_pushinteger(dst, lua_tointeger(src, absIdx)); break;
                case LUA_TBOOLEAN: lua_pushboolean(dst, lua_toboolean(src, absIdx)); break;
                case LUA_TNIL: lua_pushnil(dst); break;
                case LUA_TLIGHTUSERDATA: lua_pushlightuserdata(dst, lua_touserdata(src, absIdx)); break;
                case LUA_TVECTOR: {
                    const float* v = lua_tovector(src, absIdx);
                    if (!v) { lua_pushnil(dst); break; }
#if LUA_VECTOR_SIZE == 4
                    lua_pushvector(dst, v[0], v[1], v[2], v[3]);
#else
                    lua_pushvector(dst, v[0], v[1], v[2]);
#endif
                    break;
                }
                case LUA_TBUFFER: {
                    size_t blen = 0;
                    const void* bv = lua_tobuffer(src, absIdx, &blen);
                    if (!bv && blen) { lua_pushnil(dst); break; }
                    void* nb = lua_newbuffer(dst, blen);
                    if (blen && nb && bv) memcpy(nb, bv, blen);
                    break;
                }
                default:
                    // FUNCTION/THREAD/CLASS/etc cannot cross global states:
                    // nil instead of xmove (which would corrupt both GCs).
                    lua_pushnil(dst);
                    break;
                }
            };

        // Compile on the calling thread (pure computation). Never let a C++
        // compiler exception cross the Lua C boundary (terminate) and never
        // feed empty bytecode to luau_load.
        std::string Bytecode;
        try { Bytecode = Execution::CompileScript(Code); }
        catch (...) { Bytecode.clear(); }
        if (Bytecode.empty()) {
            for (int r : copyRefs) lua_unref(NL, r);
            lua_unref(ActorState, nlRef);
            luaL_error(L, "run_on_actor: compile failed");
            return 0;
        }
        tmp_actor_stage("compiled");
        int LoadResult = luau_load(NL, "Xploits", Bytecode.c_str(), Bytecode.size(), 0);
        if (LoadResult == LUA_OK) {
            // Prove Lclosure (execution.cpp discipline): clvalue on a
            // non-closure misreads the TValue and AVs on ->l.p.
            bool okClosure = false;
            if (lua_isfunction(NL, -1)) {
                const TValue* co = luaA_toobject(NL, -1);
                Closure* Cl = co ? clvalue(co) : nullptr;
                if (Cl && !Cl->isC && Cl->l.p) {
                    task_scheduler::SetProtoCapabilities(Cl->l.p, &MaxCapabilities);
                    okClosure = true;
                }
            }
            if (!okClosure) {
                lua_pop(NL, 1);
                for (int r : copyRefs) lua_unref(NL, r);
                lua_unref(ActorState, nlRef);
                luaL_error(L, "run_on_actor: load failed (not a Lua closure)");
                return 0;
            }
            // Stack discipline: [func] then Args copies -> [func, a1..aN],
            // the exact layout TaskDefer expects (signals.cpp pattern).
            // Args were counted before Instance.new pushed LocalScript, so
            // indices 1..Args still address the caller's args.
            for (int i{ 1 }; i <= Args; i++)
                deepCopy(L, NL, i, 0);
            for (int r : copyRefs) lua_unref(NL, r);
            // NL is anchored (nlRef), capabilities set, closure proven.
            Roblox::TaskDefer(NL);
            tmp_actor_stage("deferred");
            lua_unref(ActorState, nlRef);
        }
        else {
            // Own the error string before popping: errRaw points into NL's
            // GC and would dangle after pop on a foreign state.
            std::string loadErr;
            const char* errRaw = lua_tostring(NL, -1);
            if (errRaw) loadErr.assign(errRaw);
            lua_pop(NL, 1);
            for (int r : copyRefs) lua_unref(NL, r);
            lua_unref(ActorState, nlRef);
            luaL_error(L, "%s", loadErr.empty() ? "run_on_actor: load failed" : loadErr.c_str());
            return 0;
        }
        return 0;
    }

    int runonactor(lua_State* L) { return run_on_actor(L); }

    // ═══════════════════════════════════════════════════════════
    //  Comm channels — cross-thread safe via YieldQueue
    // ═══════════════════════════════════════════════════════════
    struct CommArg {
        enum Type { NIL, BOOLEAN, NUMBER, INTEGER, STRING, USERDATA };
        Type type = NIL;
        bool boolVal = false;
        double numVal = 0;
        lua_Integer intVal = 0;
        std::string strVal;
        // Copied game-instance pointer (never a foreign-heap userdata
        // address): read synchronously on the firing thread while the source
        // userdata is provably alive; the tick only touches this value.
        void* instancePtr = nullptr;
    };

    struct CommChannelData {
        int channelRef = LUA_NOREF; // ref to channel table on creator's global
        int signalRef = LUA_NOREF;  // ref to .Event Signal on creator's global
        lua_State* mainL = nullptr; // creating thread, anchored by threadRef
        // Registry anchor keeping mainL alive for deferred Fire ticks.
        // Channels are process-lifetime so this is intentionally never unref'd.
        int threadRef = LUA_NOREF;
        // Creator's global_State* (registry owner). Refs are per-global-state:
        // a ref taken on state A is meaningless on state B.
        void* ownerGlobal = nullptr;
    };

    static std::mutex commMutex;
    static std::unordered_map<int, CommChannelData> commIdToData;
    static int nextCommId = 1;

    // ── comm_fire dispatch: collects args, fires via YieldQueue ──
    static void DispatchFire(int commId, lua_State* callingL, int argStart, int nargs)
    {
        CommChannelData channelData;
        {
            std::lock_guard<std::mutex> lock(commMutex);
            auto it = commIdToData.find(commId);
            if (it == commIdToData.end()) return;
            channelData = it->second;
        }

        // Collect args from calling thread's stack (outside commMutex; all
        // reads are on the calling thread itself). Non-transferable types map
        // to NIL, mirroring deepCopy/signals discipline. INTEGER is explicit:
        // it is a distinct Luau type and must not fall into default (NIL).
        auto args = std::make_shared<std::vector<CommArg>>();
        for (int i = 0; i < nargs; i++) {
            int idx = argStart + i;
            CommArg arg;
            switch (lua_type(callingL, idx)) {
            case LUA_TNIL:     arg.type = CommArg::NIL; break;
            case LUA_TBOOLEAN: arg.type = CommArg::BOOLEAN; arg.boolVal = lua_toboolean(callingL, idx) != 0; break;
            case LUA_TNUMBER:  arg.type = CommArg::NUMBER; arg.numVal = lua_tonumber(callingL, idx); break;
            case LUA_TINTEGER: arg.type = CommArg::INTEGER; arg.intVal = lua_tointeger(callingL, idx); break;
            case LUA_TSTRING: {
                size_t len = 0;
                const char* s = lua_tolstring(callingL, idx, &len);
                if (!s) { arg.type = CommArg::NIL; break; }
                arg.type = CommArg::STRING;
                arg.strVal.assign(s, len);
                break;
            }
            case LUA_TUSERDATA: {
                // Cross-global Fire (actor NL -> exploit tick) must not defer
                // a foreign-heap userdata address: the source thread can be
                // collected before the tick, leaving a dangling pointer that
                // PushInstance would dereference. Copy the 8-byte game
                // instance pointer NOW while the source is alive; the tick
                // pushes from this value and never touches actor memory.
                // Non-Instance userdata cannot cross states: nil, like deepCopy.
                const char* tn = luaL_typename(callingL, idx);
                if (!tn || strcmp(tn, "Instance") != 0) { arg.type = CommArg::NIL; break; }
                void* ud = lua_touserdata(callingL, idx);
                void* inst = nullptr;
                if (!ud || !signal_safe_read(ud, &inst, sizeof(inst)) || !inst) {
                    arg.type = CommArg::NIL;
                    break;
                }
                arg.type = CommArg::USERDATA;
                arg.instancePtr = inst;
                break;
            }
            default: arg.type = CommArg::NIL; break;
            }
            args->push_back(arg);
        }

        lua_State* mainL = channelData.mainL;
        void* ownerGlobal = channelData.ownerGlobal;
        int signalRef = channelData.signalRef;
        const uint64_t gen = Shared::StateGeneration.load(std::memory_order_acquire);

        {
            std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
            Shared::YieldQueue.emplace([mainL, ownerGlobal, signalRef, args, gen]() {
            if (gen != Shared::StateGeneration.load(std::memory_order_acquire))
                return;
            // Execution thread: prefer the idle exploit thread on the SAME
            // global (refs are per-global, so any thread on ownerGlobal
            // resolves them identically). The creator coroutine M is usually
            // suspended in task.wait here; pcall'ing Signal:Fire on a yielded
            // thread is illegal re-entry and races with the wait resumption
            // on the scheduler (stack/CallInfo corruption -> silent death).
            // The websocket transport uses Shared::ExploitThread for exactly
            // this reason. Fallback to the anchored creator only for
            // actor-owned channels (different global) or between reset/re-init.
            lua_State* execL = nullptr;
            lua_State* exploit = Shared::ExploitThread;
            if (exploit && (void*)exploit->global == ownerGlobal)
                execL = exploit;
            else
                execL = mainL; // anchored via threadRef; same registry as signalRef
            if (!execL)
                return;
            // Pin the stack: the exploit thread may hold unrelated values
            // above us (suspended myriad coroutine, UI leftovers). Every exit
            // restores `base`, so we never disturb them and always balance.
            const int base = lua_gettop(execL);
            lua_getref(execL, signalRef);
            if (!lua_istable(execL, -1)) { lua_settop(execL, base); return; }

            lua_getfield(execL, -1, "Fire");
            if (!lua_isfunction(execL, -1)) { lua_settop(execL, base); return; }

            // Stack: [.., Signal, Fire]. Push Signal as self so the call is
            // Fire(Signal, a1..aN) — no absolute-index insert (lua_insert
            // buried Fire under unrelated stack slots whenever the thread
            // wasn't empty, corrupting the stack and detonating later).
            // Without this self push, pcall would attempt to call the Signal
            // table itself (func resolves to Signal, not Fire), error
            // silently inside pcall, and no :Once/:Connect continuation
            // would ever be task.spawn'ed.
            lua_pushvalue(execL, -2); // self (Signal) for Signal:Fire
            for (auto& arg : *args) {
                switch (arg.type) {
                case CommArg::NIL:     lua_pushnil(execL); break;
                case CommArg::BOOLEAN: lua_pushboolean(execL, arg.boolVal); break;
                case CommArg::NUMBER:  lua_pushnumber(execL, arg.numVal); break;
                case CommArg::INTEGER: lua_pushinteger(execL, arg.intVal); break;
                case CommArg::STRING:  lua_pushlstring(execL, arg.strVal.data(), arg.strVal.size()); break;
                case CommArg::USERDATA: {
                    // Guarded bridge (stale/destroyed instance fails closed to
                    // nil, never AVs inside game PushInstance). The native was
                    // copied at Fire time; no actor-heap access happens here.
                    // Push takes a SLOT (void** whose *slot == native, i.e. a
                    // userdata address) and derefs it — see deepCopy which
                    // passes `ud`, and cloneref/cache which pass the userdata
                    // slot. Passing the native VALUE directly would make Push
                    // deref game memory (*native == vtable/descriptor) and
                    // yield nil/wrong, which Myriad observes as
                    // `not onceReceivedArgs[2]` ("unexpected values").
                    // `&arg.instancePtr` is a tick-owned slot holding the
                    // native, so *slot == native without touching actor mem.
                    int topBefore = lua_gettop(execL);
                    if (arg.instancePtr)
                        push_actor_instance_guarded(execL, &arg.instancePtr, topBefore);
                    else if (lua_gettop(execL) == topBefore)
                        lua_pushnil(execL);
                    break;
                }
                }
            }
            // Stack: [.., Signal, Fire, Signal, a1, ..., aN].
            // pcall, not call: a Fire error must surface as a contained Lua
            // error, never as a longjmp through C++ frames (silent death).
            lua_pcall(execL, (int)args->size() + 1, 0, 0);
            lua_settop(execL, base);
            });
        }
    }

    // ── comm_fire: called when channel:Fire(...) is invoked ────
    static int comm_fire(lua_State* L)
    {
        int commId = (int)lua_tointeger(L, lua_upvalueindex(1));
        int nargs = lua_gettop(L) - 1; // exclude self
        if (nargs < 0) nargs = 0;

        DispatchFire(commId, L, 2, nargs);
        return 0;
    }

    // ── create_comm_channel() ──────────────────────────────────
    int create_comm_channel(lua_State* L)
    {
        int commId;
        {
            std::lock_guard<std::mutex> lock(commMutex);
            commId = nextCommId++;
        }

        lua_newtable(L);
        int objIdx = lua_gettop(L);

        // Create Signal for .Event
        lua_getglobal(L, "Signal");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "new");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                    lua_setfield(L, objIdx, "Event");
                }
                else {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    lua_setfield(L, objIdx, "Event");
                }
            }
            else {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_setfield(L, objIdx, "Event");
            }
        }
        else {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setfield(L, objIdx, "Event");
        }

        // Ref the .Event Signal for cross-thread fire
        lua_getfield(L, objIdx, "Event");
        int signalRef = lua_ref(L, -1);
        lua_pop(L, 1);

        // Ref the channel object itself
        lua_pushvalue(L, objIdx);
        int channelRef = lua_ref(L, -1);
        lua_pop(L, 1);

        // Anchor the creating thread: the deferred Fire lambda touches this
        // thread's registry later, and a bare lua_newthread-style thread is
        // GC-eligible once unref'd (run_on_actor's NL pattern). A registry ref
        // roots it for the channel's lifetime. Channels are process-lifetime
        // so this anchor is intentionally never released.
        lua_pushthread(L);
        int threadRef = lua_ref(L, -1);
        lua_pop(L, 1);
        void* ownerGlobal = (void*)L->global;

        {
            std::lock_guard<std::mutex> lock(commMutex);
            commIdToData[commId] = { channelRef, signalRef, L, threadRef, ownerGlobal };
        }

        // Fire function (works on main thread directly)
        lua_pushinteger(L, commId);
        lua_pushcclosure(L, comm_fire, "CommFire", 1);
        lua_setfield(L, objIdx, "Fire");

        lua_pushinteger(L, commId);
        lua_pushvalue(L, objIdx);
        return 2;
    }

    // ── get_comm_channel() ─────────────────────────────────────
    int get_comm_channel(lua_State* L)
    {
        if (lua_gettop(L) < 1 || lua_isnil(L, 1)) {
            lua_pushnil(L);
            return 1;
        }

        int commId = (int)lua_tointeger(L, 1);

        // Snapshot under lock; all Lua API below runs unlocked (Lua ops
        // must never execute under commMutex).
        CommChannelData channelData{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(commMutex);
            auto it = commIdToData.find(commId);
            if (it != commIdToData.end()) {
                channelData = it->second;
                found = true;
            }
        }
        if (!found) {
            lua_pushnil(L);
            return 1;
        }

        // Same thread as creator — return ref directly. The global check guards
        // pointer-reuse after teleport: a fresh thread allocated at the same
        // address with a fresh registry must not resolve stale refs.
        if (L == channelData.mainL && (void*)L->global == channelData.ownerGlobal) {
            lua_getref(L, channelData.channelRef);
            return 1;
        }

        // Different thread (actor thread) — return proxy table with Fire
        lua_newtable(L);

        // Fire closure that dispatches via YieldQueue
        lua_pushinteger(L, commId);
        lua_pushcclosure(L, comm_fire, "CommFire", 1);
        lua_setfield(L, -2, "Fire");

        return 1;
    }

    // ═══════════════════════════════════════════════════════════
    //  register_lib
    // ═══════════════════════════════════════════════════════════
    void register_lib(lua_State* L)
    {
        Environment->make_global(L, "getactors", Actor::getactors, funcnameinc++);
        Environment->make_global(L, "isparallel", Actor::isparallel, funcnameinc++);
        Environment->make_global(L, "checkparallel", Actor::isparallel, funcnameinc++);
        Environment->make_global(L, "run_on_actor", Actor::run_on_actor, funcnameinc++);
        Environment->make_global(L, "runonactor", Actor::runonactor, funcnameinc++);
        Environment->make_global(L, "create_comm_channel", Actor::create_comm_channel, funcnameinc++);
        Environment->make_global(L, "get_comm_channel", Actor::get_comm_channel, funcnameinc++);
    }
}

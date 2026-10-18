#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lstring.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/Execution/Execution.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <RBX/environment/env.hpp>


namespace Closures
{
#define lua_check(L, i) if (lua_gettop(L) > i) { lua_settop(L, i); }

    inline int funcnameinc = 0;

    struct HookRecord
    {
        int OriginalRef = LUA_NOREF;
        int HookRef = LUA_NOREF;
        // Original C debugname saved at first hook. Hooked C targets keep
        // it (instead of advertising "hookfunction") so debugname-based
        // probes (Type Conversion via debug.info "n" on paths the native
        // anti_debug_info swap does not cover) see the original name.
        TString* OriginalDebugName = nullptr;
    };

    struct NewCClosureRecord
    {
        int FunctionRef = LUA_NOREF;
        bool Reusable = false;
    };

    static constexpr const char* LuaHookDispatcherName = "__lunarium_hookfunction_dispatch";

    // shared across every translation unit that includes this header so that
    // hookmetamethod / debug / invoke interception all see the same records
    inline std::unordered_map<Closure*, HookRecord> HookRecords;
    // Guards HookRecords structure only (find/insert/erase + int copies).
    // Leaf lock: never held across Lua API (getref/unref/call) or another
    // mutex, mirroring Shared::FireDispatchMutex discipline. Dispatchers copy
    // the HookRef int under this lock then call outside it; restore copies
    // refs under it, does Lua work unlocked, then re-locks only to erase.
    // Same return values/semantics, fail-closed on races, no fast-path change.
    // Also guards NewCClosureRefs / WrappedClosures structure (same leaf
    // discipline: pure map ops under the lock, Lua work outside it), so a
    // concurrent hook/restore/newcclosure can no longer rehash a set while a
    // dispatcher or checkcaller frame-walk reads it (heap corruption with no
    // log line, surfacing in whatever test loops hardest — e.g. checkcaller).
    inline std::mutex HookRecordsMutex;
    inline std::unordered_map<Closure*, NewCClosureRecord> NewCClosureRefs;
    inline std::unordered_set<Closure*> WrappedClosures;
    inline int LuaHookDispatcherRef = LUA_NOREF;
    // Registry owner of LuaHookDispatcherRef (same per-global_State hazard as
    // DebugInfoOriginalRef below): a ref captured on state A is a meaningless
    // slot number on state B. Compared as (void*)L->global so any thread on
    // the same global still matches (shared registry).
    inline void* LuaHookDispatcherOwnerGlobal = nullptr;

    // hooksignal / restoresignal bookkeeping (keyed by the signal userdata)
    struct SignalRecord
    {
        int SignalRef = LUA_NOREF;
        int ConnectionsRef = LUA_NOREF;
        int FunctionsRef = LUA_NOREF;
        int HookRef = LUA_NOREF;
    };
    inline std::unordered_map<void*, SignalRecord> SignalRecords;

    // debug.info interception (preserves hooked function names)
    inline int DebugInfoOriginalRef = LUA_NOREF;
    // Registry owner of DebugInfoOriginalRef. Refs are per-global_State (the
    // registry lives on the global, not the thread): a ref captured on state
    // A is a meaningless slot number on state B. Unref'ing it against B's
    // registry splices a live slot into B's freelist, so the next lua_ref
    // reuses a live slot -> two refs share one slot -> the second capture
    // overwrites the first -> use-after-free that only detonates much later
    // (works in probes, dies deep in the suite). Compared as
    // (void*)L->global so any thread on the same global still matches
    // (shared registry); a dying state's leaked slot is harmless.
    inline void* DebugInfoOwnerGlobal = nullptr;
    // Identity of the installed wrapper Closure* (raw pointer, never
    // dereferenced here). InitializeAntiDetect patches c.f in place; after a
    // same-global re-init it rewrites this wrapper's c.f from
    // debug_info_wrapper to anti_debug_info, so the c.f==wrapper check in
    // ensure below misses and we would stack a second wrapper (then a third
    // ...) — each layer adding a C frame + lua_call nesting that only
    // detonates deep in the suite. Pointer equality survives the c.f swap;
    // stock debug.info is never this pointer (except astronomically unlikely
    // address reuse across a freed global, which merely skips a needed wrap
    // and fails visibly, never corrupting). Preserved across Reset like the
    // original ref: the wrapper may still be live on this state.
    inline Closure* DebugInfoWrapperClosure = nullptr;
    // Re-entrancy tripwire for debug_info_wrapper (see below). Thread-local:
    // the scheduler runs many threads, so a process-global counter would
    // false-trip; lock-free, no locks are ever held across the call.
    // Legitimate nesting (wrapper -> original, wrapper -> c.f-swap -> wrapper)
    // never exceeds 2-3; past the max we fail loud instead of recursing.
    inline thread_local int DebugInfoReentryDepth = 0;
    static constexpr int DebugInfoMaxDepth = 8;

    // BindableFunction.Invoke interception (META_SECURITY spoof)
    inline int InvokeOriginalRef = LUA_NOREF;
    inline bool InvokeHookInstalled = false;

    inline void ensure_debug_info_hook(lua_State* L);
    inline void EnsureInvokeSecurityHook(lua_State* L);

    inline void ResetLibraryState()
    {
        // All three structures under the leaf lock (no Lua API here): a
        // re-init racing a concurrent checkcaller frame-walk or dispatcher
        // must not rehash/clear mid-read. Registry slots intentionally leak
        // (same as before): unref'ing here could recycle a slot a mid-flight
        // dispatcher already copied.
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            HookRecords.clear();
            NewCClosureRefs.clear();
            WrappedClosures.clear();
            LuaHookDispatcherRef = LUA_NOREF;
            LuaHookDispatcherOwnerGlobal = nullptr;
        }
        SignalRecords.clear();
        InvokeOriginalRef = LUA_NOREF;
        InvokeHookInstalled = false;
        // DebugInfoOriginalRef intentionally NOT cleared: a wrapper may still be
        // installed on this state and it resolves names through that ref.
        // HiddenClosures / HiddenLevels intentionally NOT cleared here:
        // they are process-lifetime state that must survive re-registration.
    }

    extern std::unordered_set<Closure*> HiddenClosures;

    // Stack levels registered via setstackhidden(level, true).
    // Keyed by absolute level at registration time; resolved to a Closure*
    // the first time a traceback consults the filter, then treated as a
    // normal hidden closure.
    struct HiddenLevel { lua_State* L; int level; };
    extern std::vector<HiddenLevel> HiddenLevels;

    inline void HideClosure(Closure* cl)
    {
        if (!cl) return;
        HiddenClosures.insert(cl);
    }

    inline void HideClosure(Closure* hidden, Closure* original)
    {
        if (!hidden) return;
        HiddenClosures.insert(hidden);
    }

    inline void UnhideClosure(Closure* cl)
    {
        if (!cl) return;
        HiddenClosures.erase(cl);
    }

    // lua_FrameHiddenFn implementation installed into the VM.
    // Returns 1 when the activation record at 'level' should be omitted.
    int FrameHiddenFilter(lua_State* L, int level);

    static inline bool cl_isC(Closure* c) {
        return c->isC;
    }

    static inline uint8_t cl_nups(Closure* c) {
        return c->nupvalues;
    }

    static inline void gc_barrier(lua_State* L, global_State* g) {
        L->marked &= ~4u;
        L->gclist = g->gray;
        g->gray = obj2gco(L);
    }

    static inline Closure* to_closure(lua_State* L, int index)
    {
        luaL_checktype(L, index, LUA_TFUNCTION);
        return clvalue(index2addr(L, index));
    }

    static inline void push_closure(lua_State* L, Closure* closure)
    {
        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        setclvalue(L, L->top, closure);
        incr_top(L);
    }

    static inline int finish_yieldable_call(lua_State* L)
    {
        if (L->status == SCHEDULED_REENTRY || L->status == LUA_YIELD || L->status == LUA_BREAK)
            return C_CALL_YIELD;

        return lua_gettop(L);
    }

    static int DispatchContinuation(lua_State* L, int status)
    {
        return lua_gettop(L);
    }

    static inline int call_referenced_function(lua_State* L, int ref)
    {
        lua_getref(L, ref);
        if (!lua_isfunction(L, -1))
        {
            luaL_error(L, ("hookfunction: referenced value is not a function"));
        }


        lua_insert(L, 1);
        luaD_callint(L, L->base, LUA_MULTRET, lua_isyieldable(L) != 0);
        int result = finish_yieldable_call(L);
        return result;
    }

    static Closure* clone_closure(lua_State* L, Closure* source)
    {
        Closure* clone = nullptr;

        if (source->isC)
        {
            clone = luaF_newCclosure(L, source->nupvalues, source->env);
            clone->stacksize = source->stacksize;
            clone->preload = source->preload;
            clone->c.f = source->c.f;
            clone->c.cont = static_cast<lua_Continuation>(source->c.cont);
            clone->c.debugname = source->c.debugname;

            for (int i = 0; i < source->nupvalues; ++i)
                setobj2n(L, &clone->c.upvals[i], &source->c.upvals[i]);

            // Snapshot under the leaf lock (no Lua API inside): a concurrent
            // newcclosure/restore must not rehash mid-find. Publish under the
            // lock after; identical entries on the stable path.
            int wrappedFnRef = LUA_NOREF;
            bool wasWrapped = false;
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                auto wrapped = NewCClosureRefs.find(source);
                if (wrapped != NewCClosureRefs.end())
                {
                    wrappedFnRef = wrapped->second.FunctionRef;
                    wasWrapped = true;
                }
            }
            if (wasWrapped)
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                NewCClosureRefs[clone] = { wrappedFnRef, false };
            }
        }
        else
        {
            clone = luaF_newLclosure(L, source->nupvalues, source->env, source->l.p);
            clone->stacksize = source->stacksize;
            clone->preload = source->preload;

            for (int i = 0; i < source->nupvalues; ++i)
                setobj2n(L, &clone->l.uprefs[i], &source->l.uprefs[i]);
        }

        return clone;
    }

    static inline void push_clone(lua_State* L, Closure* source)
    {
        push_closure(L, clone_closure(L, source));
    }

    static void copy_closure_body(lua_State* L, Closure* target, Closure* source)
    {
        // Layout lock: Closure is a union (c vs l) with different sizes and
        // field offsets. Writing source's layout into a target allocated for
        // the other one overruns the heap (sizeCclosure != sizeLclosure).
        // All callers pre-check, this turns any missed path into a catchable
        // Lua error instead of silent corruption. No locks held here.
        if (!target || !source || target->isC != source->isC)
            luaL_error(L, ("copy_closure_body: incompatible closure layout"));

        target->stacksize = source->stacksize;
        target->preload = source->preload;
        target->nupvalues = source->nupvalues;
        target->env = source->env;
        if (source->env)
            luaC_objbarrier(L, target, source->env);

        if (source->isC)
        {
            target->c.f = source->c.f;
            target->c.cont = static_cast<lua_Continuation>(source->c.cont);
            target->c.debugname = source->c.debugname;
            if (source->c.debugname)
                luaC_objbarrier(L, target, source->c.debugname);

            for (int i = 0; i < source->nupvalues; ++i)
            {
                setobj2n(L, &target->c.upvals[i], &source->c.upvals[i]);
                luaC_barrier(L, target, &target->c.upvals[i]);
            }
        }
        else
        {
            target->l.p = source->l.p;
            if (source->l.p)
                luaC_objbarrier(L, target, source->l.p);

            for (int i = 0; i < source->nupvalues; ++i)
            {
                setobj2n(L, &target->l.uprefs[i], &source->l.uprefs[i]);
                luaC_barrier(L, target, &target->l.uprefs[i]);
            }
        }
    }

    static int NewCClosureDispatcher(lua_State* L)
    {
        Closure* current = curr_func(L);
        // Leaf-lock copy-out (mirrors HookCDispatcher): a concurrent
        // newcclosure/restore must not rehash mid-find. Same error and same
        // call target as before; Lua work stays outside the lock.
        int fnRef = LUA_NOREF;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it = NewCClosureRefs.find(current);
            if (it != NewCClosureRefs.end())
                fnRef = it->second.FunctionRef;
        }
        if (fnRef == LUA_NOREF)
        {
            luaL_error(L, ("newcclosure: missing wrapped function"));
        }

        return call_referenced_function(L, fnRef);
    }

    static int HookCDispatcher(lua_State* L)
    {
        Closure* current = curr_func(L);
        // Structure guard only: copy the ref int under the leaf lock, then
        // error/call outside it (no Lua API under locks). Same error and
        // same call target as before; concurrent restore erase can no longer
        // invalidate the iterator mid-find. Fast-path logic unchanged.
        int hookRef = LUA_NOREF;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it = HookRecords.find(current);
            if (it != HookRecords.end())
                hookRef = it->second.HookRef;
        }
        if (hookRef == LUA_NOREF)
        {
            luaL_error(L, ("hookfunction: missing hook"));
        }

        return call_referenced_function(L, hookRef);
    }

    static int HookLuaDispatcher(lua_State* L)
    {
        Closure* target = to_closure(L, 1);
        lua_remove(L, 1);

        // Same leaf-lock copy-out as HookCDispatcher: map op under lock,
        // Lua error/call after release. Identical returns.
        int hookRef = LUA_NOREF;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it = HookRecords.find(target);
            if (it != HookRecords.end())
                hookRef = it->second.HookRef;
        }
        if (hookRef == LUA_NOREF)
        {
            luaL_error(L, ("hookfunction: missing Lua hook"));
        }

        return call_referenced_function(L, hookRef);
    }

    static Proto* ensure_lua_hook_dispatcher_proto(lua_State* L)
    {
        // Snapshot the ref int + owner under the leaf lock; all Lua work
        // below runs unlocked. Refs are per-global_State: a ref captured on
        // the exploit global is a meaningless slot number on an actor global,
        // and getref there would push a non-function that clvalue misreads as
        // a Closure* (silent death on the first cross-global Lua hook). The
        // owner mismatch path recompiles for this global instead. Same proto
        // bytes on every path, so single-global behavior is identical.
        int cachedRef = LUA_NOREF;
        void* cachedOwner = nullptr;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            cachedRef = LuaHookDispatcherRef;
            cachedOwner = LuaHookDispatcherOwnerGlobal;
        }
        if (cachedRef != LUA_NOREF && cachedOwner == (void*)L->global)
        {
            lua_getref(L, cachedRef);
            if (lua_isfunction(L, -1))
            {
                Closure* cached = clvalue(index2addr(L, -1));
                if (!cached->isC && cached->l.p)
                {
                    Proto* proto = cached->l.p;
                    lua_pop(L, 1);
                    return proto;
                }
            }
            lua_pop(L, 1);
        }

        std::string bytecode = Execution::CompileScript(("local f = debug.info(1, \"f\"); return __lunarium_hookfunction_dispatch(f, ...)"));
        if (luau_load(L, ("=hookfunction"), bytecode.data(), bytecode.size(), NULL) != LUA_OK)
        {
            lua_error(L);
        }

        Closure* closure = clvalue(index2addr(L, -1));
        task_scheduler::SetProtoCapabilities(closure->l.p, &MaxCapabilities);
        int freshRef = lua_ref(L, -1);
        Proto* proto = closure->l.p;
        lua_pop(L, 1);
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            LuaHookDispatcherRef = freshRef;
            LuaHookDispatcherOwnerGlobal = (void*)L->global;
        }
        return proto;
    }

    // Private hook env (Type-Conversion / Environment DIRECT env-leak fix).
    // If target's env == L->gt (executor target, ENV-test path) keep it and
    // return false so the caller preserves identity (getfenv(target)==L->gt).
    // Else (game/custom target, DIRECT path) install P(empty)->D(debug,
    // dispatch)->O(original) via lua_setfenv and return true. P stays empty
    // so pairs(P) shows 0 keys; P.getgenv resolves through D (no getgenv)
    // to O (clean, nil) while the dispatcher still resolves debug/dispatch
    // through D. Metatables are locked (__metatable="locked") so the chain
    // is not walkable via getmetatable. Stack balanced. No locks.
    static bool install_private_hook_env(lua_State* L, int targetIdx)
    {
        int top0 = lua_gettop(L);
        int absIdx = targetIdx >= 0 ? targetIdx : top0 + targetIdx + 1;

        lua_getfenv(L, absIdx); // [O]
        lua_pushvalue(L, LUA_GLOBALSINDEX); // [O, gt]
        int same = lua_rawequal(L, -2, -1);
        lua_pop(L, 1); // [O]
        if (same)
        {
            lua_pop(L, 1);
            return false;
        }

        // D = { debug, __lunarium_hookfunction_dispatch } with __index = O
        lua_newtable(L); // [O, D]
        int dIdx = lua_gettop(L);
        lua_getglobal(L, "debug"); // [O, D, debug]
        lua_setfield(L, dIdx, "debug"); // [O, D]
        lua_getglobal(L, LuaHookDispatcherName); // [O, D, dispatch|nil]
        if (lua_isnil(L, -1))
            lua_pop(L, 1);
        else
            lua_setfield(L, dIdx, LuaHookDispatcherName); // [O, D]

        lua_newtable(L); // [O, D, Dmt]
        int dmt = lua_gettop(L);
        lua_pushvalue(L, top0 + 1); // [O, D, Dmt, O]
        lua_setfield(L, dmt, "__index"); // [O, D, Dmt]
        lua_pushstring(L, "locked");
        lua_setfield(L, dmt, "__metatable"); // [O, D, Dmt]
        lua_setmetatable(L, dIdx); // [O, D]

        // P = {} with __index = D
        lua_newtable(L); // [O, D, P]
        int pIdx = lua_gettop(L);
        lua_newtable(L); // [O, D, P, Pmt]
        int pmt = lua_gettop(L);
        lua_pushvalue(L, dIdx); // [O, D, P, Pmt, D]
        lua_setfield(L, pmt, "__index"); // [O, D, P, Pmt]
        lua_pushstring(L, "locked");
        lua_setfield(L, pmt, "__metatable"); // [O, D, P, Pmt]
        lua_setmetatable(L, pIdx); // [O, D, P]

        lua_pushvalue(L, pIdx); // [O, D, P, P]
        lua_setfenv(L, absIdx); // [O, D, P] (pops copy, barrier handled)
        lua_settop(L, top0);
        return true;
    }

    // Wraps the original metamethod so user hooks never call the real
    // metamethod directly. The wrapper is a Lua closure with a private
    // environment: whenever the hook chain throws, the frame directly above
    // the original metamethod is this wrapper (private env, no script keys)
    // instead of the user's hook closure.
    static int PushOldTrampoline(lua_State* L, int oldIdx)
    {
        static const std::string trampolineBytecode = Execution::CompileScript((
            "return function(orig)\n"
            "    return function(...)\n"
            "        local packed = table.pack(orig(...))\n"
            "        return table.unpack(packed, 1, packed.n)\n"
            "    end\n"
            "end\n"
        ));

        // fresh environment table: only carries `table`, never user-script keys
        lua_newtable(L);
        lua_getglobal(L, "table");
        if (lua_isfunction(L, -1) || lua_istable(L, -1))
            lua_setfield(L, -2, "table");
        else
            lua_pop(L, 1);
        int envIdx = lua_gettop(L);

        if (luau_load(L, ("=hookmetamethod"), trampolineBytecode.data(), trampolineBytecode.size(), envIdx) != LUA_OK)
        {
            lua_pop(L, 1); // leave the load error on top
            lua_error(L);
        }

        if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        {
            lua_remove(L, envIdx);
            lua_error(L);
        }

        lua_pushvalue(L, oldIdx);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            lua_remove(L, envIdx);
            lua_error(L);
        }

        Closure* tramp = clvalue(index2addr(L, -1));
        HideClosure(tramp);
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            Shared::ExecutorClosures.insert(tramp);
        }
        // Intentionally NOT added to function_array: that vector holds raw,
        // non-GC-rooted pointers, so a droppable closure like this trampoline
        // would dangle after collection and fault every consumer that
        // dereferences entries. ExecutorClosures membership (checked first by
        // every matcher, never dereferenced) already covers it.

        lua_remove(L, envIdx);
        return 1;
    }

    static int clonefunction(lua_State* L)
    {
        Closure* source = to_closure(L, 1);
        push_clone(L, source);
        return 1;
    }

    inline int cloneref(lua_State* L) {
        lua_check(L, 1);
        luaL_checktype(L, 1, LUA_TUSERDATA);
        // luaL_typename returns an interned const char*: compare directly
        // instead of constructing a std::string (heap alloc) per call.
        const char* ud_t = luaL_typename(L, 1);
        if (!ud_t || strcmp(ud_t, ("Instance")) != 0)
            luaL_typeerrorL(L, 1, ("Instance"));
        void* ud = lua_touserdata(L, 1);
        // The userdata slot itself is Lua-managed (always readable); the
        // stored native pointer must be non-null or PushInstance would AV.
        void* instance = ud ? *static_cast<void**>(ud) : nullptr;
        if (!instance)
            luaL_argerror(L, 1, ("Invalid Instance"));
        // Bridge table: registry[PushInstance] maps native -> userdata.
        lua_pushlightuserdata(L, Roblox::PushInstance);
        lua_rawget(L, LUA_REGISTRYINDEX); // [orig, bridge]
        const int bridgeIdx = 2;
        // Invalidate so PushInstance below allocates a fresh userdata object
        // (distinct Lua identity, so `clone == orig` is false) instead of
        // returning the cached original.
        lua_pushlightuserdata(L, instance);
        lua_pushnil(L);
        lua_rawset(L, bridgeIdx);
        // Fresh reference to the same native instance (shares attributes,
        // tags and properties natively).
        Roblox::PushInstance(L, ud); // [orig, bridge, clone]
        const int cloneIdx = 3;
        // Preserve Lua-side metadata the test checks: the original may carry
        // a custom metatable (Myriad spoofs __index via setrawmetatable and
        // then compares .Name). A fresh PushInstance gets the default
        // Instance metatable, so copy the original's metatable raw (C API
        // bypasses __metatable protection). No Lua metamethods run here.
        if (lua_getmetatable(L, 1)) { // [orig, bridge, clone, origMt]
            lua_setmetatable(L, cloneIdx); // pops origMt, sets on clone
        }
        // Re-cache the fresh clone so the bridge does not pin the stale
        // original userdata (lets the original GC while the clone lives,
        // which the Myriad weak-table check requires). The clone stays alive
        // via the returned stack slot; the bridge ref is the only table-held
        // strong ref and it now points at the clone, never the original.
        lua_pushlightuserdata(L, instance); // [orig, bridge, clone, key]
        lua_pushvalue(L, cloneIdx); // [orig, bridge, clone, key, clone]
        lua_rawset(L, bridgeIdx); // pops key + clone copy
        // Defensive: clear any stale entry keyed by the original userdata slot
        // itself. The canonical bridge key is the native (above), but if any
        // build cached by slot address that entry would pin the original
        // userdata strongly and stall Myriad's `setmetatable({{}, orig},
        // {__mode="v"})` GC check (5s timeout). No-op when absent. Clone
        // (re-cached above by native) is untouched and stays distinct.
        lua_pushlightuserdata(L, ud); // [orig, bridge, clone, udKey]
        lua_pushnil(L); // [orig, bridge, clone, udKey, nil]
        lua_rawset(L, bridgeIdx); // pops udKey + nil
        // Drop bridge and original, leaving only the clone.
        lua_remove(L, bridgeIdx); // [orig, clone]
        lua_remove(L, 1); // [clone]
        return 1;
    }

    static int hookfunction(lua_State* L)
    {
     

        Closure* target = to_closure(L, 1);
        Closure* hook = to_closure(L, 2);

        // Copy-out snapshot: map op under the leaf lock, all Lua work below
        // runs unlocked. Local `record` is a value copy; every mutation
        // below also publishes via HookRecords[target] = record under lock.
        HookRecord record;
        bool hadHookRecord = false;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto snapIt = HookRecords.find(target);
            if (snapIt != HookRecords.end())
            {
                record = snapIt->second;
                hadHookRecord = true;
            }
        }
        (void)hadHookRecord;
        if (record.OriginalRef == LUA_NOREF)
        {
            // Snapshot the wrapped ref under the leaf lock; the lua_getref
            // below runs unlocked. Identical ref on the stable path.
            int wrappedFnRef = LUA_NOREF;
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                auto wrappedTarget = NewCClosureRefs.find(target);
                if (wrappedTarget != NewCClosureRefs.end())
                    wrappedFnRef = wrappedTarget->second.FunctionRef;
            }
            if (target->isC && wrappedFnRef != LUA_NOREF)
            {
                lua_getref(L, wrappedFnRef);
            }
            else
            {
                push_clone(L, target);
            }

            record.OriginalRef = lua_ref(L, -1);
            lua_pop(L, 1);
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                HookRecords[target] = record;
            }
        }
        else
        {
            lua_getref(L, record.OriginalRef);
            Closure* original = to_closure(L, -1);

            if (hook == original)
            {
                if (target->isC != original->isC)
                {
                    // Pure map probe under the leaf lock (no Lua inside).
                    bool targetIsWrapped = false;
                    {
                        std::lock_guard<std::mutex> lock(HookRecordsMutex);
                        targetIsWrapped = NewCClosureRefs.find(target) != NewCClosureRefs.end();
                    }
                    if (target->isC && !original->isC && targetIsWrapped)
                    {
                        target->stacksize = LUA_MINSTACK;
                        target->preload = 0;
                        target->nupvalues = 0;
                        target->env = original->env;
                        target->c.f = NewCClosureDispatcher;
                        target->c.cont = DispatchContinuation;
                        // Interned TString: traverseclosure stringmarks
                        // c.debugname on every GC, so a raw char* here would
                        // AV on first mark (writes BLACKBIT into .rdata).
                        target->c.debugname = luaS_new(L, ("newcclosure"));
                        // New white TString into an old (possibly black)
                        // target: without this the name is collected while
                        // the target still references it (only under GC
                        // pressure — probes live, deep suites die with no
                        // log when getfuncname/is_internal later reads it).
                        luaC_objbarrier(L, target, target->c.debugname);
                        if (original->env)
                            luaC_objbarrier(L, target, original->env);
                    }
                    else
                    {
                        lua_pop(L, 1);
                        luaL_error(L, ("hookfunction: incompatible closure layout"));
                    }
                }
                else
                {
                    copy_closure_body(L, target, original);

                    if (target->isC)
                    {
                        // Snapshot ints/flags under the leaf lock, publish
                        // under it after; copy_closure_body above stays
                        // unlocked. Same entries on the stable path.
                        int origWrappedRef = LUA_NOREF;
                        bool hasOrigWrapped = false;
                        bool hasTargetWrapped = false;
                        bool targetReusable = false;
                        {
                            std::lock_guard<std::mutex> lock(HookRecordsMutex);
                            auto originalWrapped = NewCClosureRefs.find(original);
                            if (originalWrapped != NewCClosureRefs.end())
                            {
                                origWrappedRef = originalWrapped->second.FunctionRef;
                                hasOrigWrapped = true;
                            }
                            auto targetWrapped = NewCClosureRefs.find(target);
                            if (targetWrapped != NewCClosureRefs.end())
                            {
                                hasTargetWrapped = true;
                                targetReusable = targetWrapped->second.Reusable;
                            }
                        }

                        if (hasOrigWrapped)
                        {
                            std::lock_guard<std::mutex> lock(HookRecordsMutex);
                            NewCClosureRefs[target] = { origWrappedRef, true };
                            WrappedClosures.insert(target);
                        }
                        else if (hasTargetWrapped && targetReusable)
                        {
                            std::lock_guard<std::mutex> lock(HookRecordsMutex);
                            NewCClosureRefs.erase(target);
                            WrappedClosures.erase(target);
                        }
                    }
                }

                if (record.HookRef != LUA_NOREF)
                    lua_unref(L, record.HookRef);

                lua_unref(L, record.OriginalRef);
                {
                    std::lock_guard<std::mutex> lock(HookRecordsMutex);
                    HookRecords.erase(target);
                }
                {
                    std::lock_guard<std::mutex> lock(HookRecordsMutex);
                    Shared::ExecutorClosures.erase(target);
                }

                return 1;
            }

            lua_pop(L, 1);

            if (record.HookRef != LUA_NOREF)
            {
                lua_unref(L, record.HookRef);
                record.HookRef = LUA_NOREF;
                {
                    std::lock_guard<std::mutex> lock(HookRecordsMutex);
                    auto publishIt = HookRecords.find(target);
                    if (publishIt != HookRecords.end())
                        publishIt->second.HookRef = LUA_NOREF;
                }
            }
        }

        // Ref arg 2 directly: identical object, net stack unchanged
        // (old sequence pushed a copy, ref'd it, then popped it).
        // Copy-out write-back: Lua ref first (unlocked), then publish ints
        // under the leaf lock with no Lua API inside.
        record.HookRef = lua_ref(L, 2);
        TString* curDbgName = target->isC ? target->c.debugname : nullptr;
        if (!record.OriginalDebugName)
            record.OriginalDebugName = curDbgName;
        TString* dbgToApply = record.OriginalDebugName;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            HookRecords[target] = record;
        }

        if (target->isC)
        {
            // Stealth: keep the original debugname. The TString stays alive
            // via the OriginalRef clone (which shares the pointer), so this
            // never dangles. On re-hook the saved name is re-applied in case
            // an older build stamped "hookfunction" here.
            target->c.f = HookCDispatcher;
            target->c.cont = DispatchContinuation;
            if (dbgToApply)
            {
                target->c.debugname = dbgToApply;
                luaC_objbarrier(L, target, dbgToApply);
            }
        }
        else
        {
            Proto* dispatcher = ensure_lua_hook_dispatcher_proto(L);

            target->stacksize = dispatcher->maxstacksize;
            target->preload = 0;
            target->nupvalues = 0;
            // Env-leak fix: executor targets (env == L->gt) keep identity for
            // the ENV test; game/custom targets get private P->D->O so direct
            // getfenv walks see a clean empty env while the dispatcher still
            // resolves debug/dispatch through D. lua_setfenv handles barrier.
            if (!install_private_hook_env(L, 1))
            {
                target->env = L->gt;
                if (L->gt)
                    luaC_objbarrier(L, target, L->gt);
            }
            target->l.p = dispatcher;
            luaC_objbarrier(L, target, dispatcher);
        }

        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            Shared::ExecutorClosures.insert(target);
        }
        lua_getref(L, record.OriginalRef);
        return 1;
    }

    static int restorefunction(lua_State* L)
    {
        Closure* target = to_closure(L, 1);
        // Snapshot the record under the leaf lock; all Lua work below runs
        // unlocked. The final erase re-finds by key under the lock (never
        // reuses the pre-Lua iterator, which a concurrent insert could have
        // invalidated) and only erases when the record is still ours, so a
        // concurrent re-hook is never deleted. Erase happens under the lock
        // before the unrefs (both outside the lock), so a mid-flight
        // dispatcher either misses (clean error, same as erased-before) or
        // finishes with its pre-restore copy. Same booleans, same errors.
        int origRefCopy = LUA_NOREF;
        int hookRefCopy = LUA_NOREF;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it = HookRecords.find(target);
            if (it == HookRecords.end() || it->second.OriginalRef == LUA_NOREF)
            {
            }
            else
            {
                origRefCopy = it->second.OriginalRef;
                hookRefCopy = it->second.HookRef;
            }
        }
        if (origRefCopy == LUA_NOREF)
        {
            lua_pushboolean(L, false);
            return 1;
        }

        lua_getref(L, origRefCopy);
        Closure* original = to_closure(L, -1);

        if (target->isC != original->isC)
        {
            bool targetIsWrapped = false;
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                targetIsWrapped = NewCClosureRefs.find(target) != NewCClosureRefs.end();
            }
            if (target->isC && !original->isC && targetIsWrapped)
            {
                target->stacksize = LUA_MINSTACK;
                target->preload = 0;
                target->nupvalues = 0;
                target->env = original->env;
                target->c.f = NewCClosureDispatcher;
                target->c.cont = DispatchContinuation;
                // Interned TString: traverseclosure stringmarks c.debugname
                // on every GC, so a raw char* here would AV on first mark.
                target->c.debugname = luaS_new(L, ("newcclosure"));
                // See hookfunction path above: new white name into old black
                // target needs a barrier or GC pressure frees it early.
                luaC_objbarrier(L, target, target->c.debugname);
                if (original->env)
                    luaC_objbarrier(L, target, original->env);

                lua_pop(L, 1);

                {
                    std::lock_guard<std::mutex> lock(HookRecordsMutex);
                    auto it2 = HookRecords.find(target);
                    if (it2 != HookRecords.end() && it2->second.OriginalRef == origRefCopy)
                        HookRecords.erase(it2);
                }
                if (hookRefCopy != LUA_NOREF)
                    lua_unref(L, hookRefCopy);

                lua_unref(L, origRefCopy);

                lua_pushboolean(L, true);
                return 1;
            }

            lua_pop(L, 1);
            luaL_error(L, ("restorefunction: incompatible closure layout"));
        }

        copy_closure_body(L, target, original);
        if (target->isC)
        {
            // Same snapshot/publish split as hookfunction above: pure map ops
            // under the leaf lock, Lua barriers outside it.
            int origWrappedRef = LUA_NOREF;
            bool hasOrigWrapped = false;
            bool hasTargetWrapped = false;
            bool targetReusable = false;
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                auto originalWrapped = NewCClosureRefs.find(original);
                if (originalWrapped != NewCClosureRefs.end())
                {
                    origWrappedRef = originalWrapped->second.FunctionRef;
                    hasOrigWrapped = true;
                }
                auto targetWrapped = NewCClosureRefs.find(target);
                if (targetWrapped != NewCClosureRefs.end())
                {
                    hasTargetWrapped = true;
                    targetReusable = targetWrapped->second.Reusable;
                }
            }

            if (hasOrigWrapped)
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                NewCClosureRefs[target] = { origWrappedRef, true };
                WrappedClosures.insert(target);
            }
            else if (hasTargetWrapped && targetReusable)
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                NewCClosureRefs.erase(target);
                WrappedClosures.erase(target);
            }
        }
        lua_pop(L, 1);

        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it2 = HookRecords.find(target);
            if (it2 != HookRecords.end() && it2->second.OriginalRef == origRefCopy)
                HookRecords.erase(it2);
        }
        if (hookRefCopy != LUA_NOREF)
            lua_unref(L, hookRefCopy);

        lua_unref(L, origRefCopy);

        lua_pushboolean(L, true);
        return 1;
    }

    inline int newcclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* closure = clvalue(index2addr(L, 1));
        if (closure->isC) {
            lua_pushvalue(L, 1);
            return 1;
        }
        // Snapshot reusable entries under the leaf lock, then resolve with Lua
        // outside it (lua_getref must never run under the lock). A re-verify
        // under the lock before pushing closes the erase-then-push
        // use-after-free: a lost race falls through to the create path below,
        // identical to no-match. Same hit on the stable path.
        std::vector<std::pair<Closure*, int>> reusable;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            reusable.reserve(NewCClosureRefs.size());
            for (auto& pair : NewCClosureRefs) {
                // Skip dead slots without touching the registry: getref(NOREF)
                // pushes nil, which can never match `same` below.
                if (!pair.second.Reusable || pair.second.FunctionRef == LUA_NOREF)
                    continue;
                reusable.emplace_back(pair.first, pair.second.FunctionRef);
            }
        }
        for (auto& entry : reusable) {
            lua_getref(L, entry.second);
            bool same = lua_isfunction(L, -1) && clvalue(index2addr(L, -1)) == closure;
            lua_pop(L, 1);

            if (same) {
                Closure* wrapper = nullptr;
                {
                    std::lock_guard<std::mutex> lock(HookRecordsMutex);
                    auto recheck = NewCClosureRefs.find(entry.first);
                    if (recheck != NewCClosureRefs.end() && recheck->second.FunctionRef == entry.second)
                        wrapper = recheck->first;
                }
                if (wrapper) {
                    push_closure(L, wrapper);
                    return 1;
                }
            }
        }
        const char* debugname = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("newcclosure");
        lua_pushcclosurek(L, NewCClosureDispatcher, debugname, 0, DispatchContinuation);
        Closure* wrapper = clvalue(index2addr(L, -1));

        // lua_ref takes any stack index and leaves the stack untouched:
        // ref arg 1 directly instead of push-copy/ref/pop. Publish under the
        // leaf lock (no Lua API inside); identical entries on success.
        int fnRef = lua_ref(L, 1);
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            NewCClosureRefs[wrapper] = { fnRef, true };
            WrappedClosures.insert(wrapper);
        }

        wrapper->env = closure->env;
        if (closure->env)
            luaC_objbarrier(L, wrapper, closure->env);
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            Shared::ExecutorClosures.insert(wrapper);
        }
        // Intentionally NOT added to function_array (raw, non-GC-rooted
        // pointers: a dropped wrapper would dangle and fault entry
        // dereferences). Covered by ExecutorClosures / NewCClosureRefs /
        // WrappedClosures, which matchers check without dereferencing.
        return 1;
    }

    inline int isnewcclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* closure = clvalue(index2addr(L, 1));
        // Pure map read under the leaf lock (no Lua inside); same boolean.
        bool wrapped = false;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            wrapped = WrappedClosures.count(closure) > 0;
        }
        lua_pushboolean(L, wrapped);
        return 1;
    }

    inline int loadstring(lua_State* L)
    {
       
        luaL_checktype(L, 1, LUA_TSTRING);

        const char* ChunkName = luaL_optstring(L, 2, ("Lunarium"));

        std::string Bytecode = Execution::CompileScript(lua_tostring(L, 1));

        if (luau_load(L, ChunkName, Bytecode.data(), Bytecode.size(), NULL) != LUA_OK)
        {
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }

        Closure* cls = clvalue(luaA_toobject(L, -1));
        task_scheduler::SetProtoCapabilities(cls->l.p, &MaxCapabilities);

        lua_setsafeenv(L, LUA_ENVIRONINDEX, false);

        return 1;
    }

    // ------------------------------------------------------------------
    // detection fix: Function Type Conversion
    //
    // hookfunction swaps the proto of a Lua target, which changes what
    // debug.info(f, "n") reports. The wrapper resolves name lookups for
    // hooked functions against the saved original closure instead.
    // ------------------------------------------------------------------
    // One boot.log line, file I/O only, never under a lock. Mirrors the
    // RingStatus/TPHandler pattern (%LOCALAPPDATA%\Viridian\boot.log).
    static void DebugInfoBootLog(const char* msg)
    {
        char base[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char bp[MAX_PATH + 32]{};
        snprintf(bp, sizeof(bp), "%s\\Viridian\\boot.log", base);
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp, "a") != 0 || !bf)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [debug.info] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
        fclose(bf);
    }

    static int debug_info_wrapper(lua_State* L)
    {
        // Re-entrancy tripwire: if OriginalRef ever resolves to the wrapper
        // itself (re-init races, anti_debug_info c.f-swap interplay, teleport
        // edges), the call below would recurse unboundedly = freeze-then-death
        // (visible hang first, silent death later). Past the max depth we stop
        // recursing: one boot.log line plus a clean, catchable Lua error.
        if (++DebugInfoReentryDepth > DebugInfoMaxDepth)
        {
            --DebugInfoReentryDepth;
            DebugInfoBootLog("re-entrant debug.info call detected, failing closed");
            luaL_error(L, ("debug.info: re-entrant call detected"));
            return 0; // unreachable (luaL_error throws); keeps the count balanced
        }
        // RAII: lua_call/luaL_error below throw as C++ exceptions
        // (LUA_USE_LONGJMP == 0), so a manual decrement at each return would
        // leak the increment on the error path and eventually trip falsely.
        struct DebugInfoDepthGuard { ~DebugInfoDepthGuard() { --DebugInfoReentryDepth; } };
        DebugInfoDepthGuard depthGuard;

        int fnIdx = 0;
        if (lua_isfunction(L, 1))
            fnIdx = 1;
        else if (lua_isthread(L, 1) && lua_isfunction(L, 2))
            fnIdx = 2;

        // Level-form frame compensation (depth fix): this wrapper adds one C
        // frame between the caller and the original db_info (anti_debug_info
        // reuses its own frame via direct C call, so it adds none).
        // Forwarding the level unchanged resolves one frame too shallow:
        // debug.info(1,"f") returns this wrapper instead of the caller, and
        // the shared Lua hook dispatcher (debug.info(1,"f") expecting its own
        // target Closure*) resolves to the wrapper, misses HookRecords and
        // throws on every post-wrapper Lua hook call. Shallow probes survive
        // (no prior hooks, counts merely shift); deep bundles with prior
        // hooks freeze when broken dispatchers sit on live paths.
        // Thread-form (thread, level) targets another coroutine whose ci
        // chain never contains this frame, so it is left untouched.
        // Negatives (incl. INT_MIN from NaN/inf huge) stay untouched for the
        // stock level>=0 guard to reject; huge positives are capped so +1
        // cannot wrap into negative and turn a 0-result level into an error.
        if (fnIdx == 0 && !lua_isthread(L, 1) && lua_isnumber(L, 1))
        {
            lua_Integer lvl = lua_tointeger(L, 1);
            if (lvl >= 0 && lvl < 1000000)
            {
                lua_rawcheckstack(L, 1);
                lua_pushinteger(L, lvl + 1);
                lua_replace(L, 1);
            }
        }

        // Type-Conversion fix: debug.info(hooked, "f") must return the hooked
        // slot (same Closure*), not the saved original clone, otherwise a
        // tostring/pointer comparison (tostring(debug.info(f,"f")) vs
        // tostring(f)) diverges and the probe reports a hook. Other flags
        // ("n","s","a","u","l") still resolve against the original so names,
        // sources and arity stay indistinguishable; only "f" results are
        // mapped back to the hooked slot after the call.
        Closure* hookedFn = nullptr;
        Closure* origFn = nullptr;
        bool wantsFunc = false;

        if (fnIdx != 0)
        {
            Closure* fn = clvalue(index2addr(L, fnIdx));
            // Copy-out: map op under the leaf lock, string/registry work
            // below runs unlocked on the copied ref int.
            int dbgOrigRefCopy = LUA_NOREF;
            {
                std::lock_guard<std::mutex> lock(HookRecordsMutex);
                auto dbgIt = HookRecords.find(fn);
                if (dbgIt != HookRecords.end())
                    dbgOrigRefCopy = dbgIt->second.OriginalRef;
            }
            if (dbgOrigRefCopy != LUA_NOREF)
            {
                hookedFn = fn;
                int optsIdx = (fnIdx == 1) ? 2 : 3;
                if (lua_isstring(L, optsIdx))
                {
                    const char* opts = lua_tostring(L, optsIdx);
                    if (opts && strchr(opts, 'f'))
                        wantsFunc = true;
                }
                else
                {
                    // No options string: scan returns anyway (harmless, only
                    // replaces slots that are exactly the original clone).
                    wantsFunc = true;
                }

                lua_getref(L, dbgOrigRefCopy);
                // Fail open on a stale/foreign slot (unref'd + reused after a
                // re-init race): the slot may hold a non-function, which
                // clvalue would misread as a Closure* and fault inside the
                // wrapper. Keeping the caller's slot is behavior-identical on
                // the stable path, where the ref always resolves to a function.
                if (lua_isfunction(L, -1))
                {
                    origFn = clvalue(index2addr(L, -1));
                    lua_replace(L, fnIdx);
                }
                else
                {
                    lua_pop(L, 1);
                }
            }
        }

        // Fail closed: the ref is only meaningful on the global that created
        // it. A foreign global (teleport re-init mid-flight, stale slot after
        // ResetLibraryState-adjacent teardown) must error here rather than
        // push another registry's slot content and call it.
        if (DebugInfoOriginalRef == LUA_NOREF || DebugInfoOwnerGlobal != (void*)L->global)
            luaL_error(L, ("debug.info: not initialized"));

        int nargs = lua_gettop(L);
        lua_getref(L, DebugInfoOriginalRef);
        // Fail closed on a stale slot (unref'd/reused elsewhere): calling
        // whatever the slot now holds would be garbage-invocation or, if the
        // slot now holds the wrapper itself, unbounded recursion.
        if (!lua_isfunction(L, -1))
            luaL_error(L, ("debug.info: original function missing"));
        lua_insert(L, 1);
        lua_call(L, nargs, LUA_MULTRET);
        int nret = lua_gettop(L);

        if (wantsFunc && origFn && hookedFn)
        {
            for (int i = 1; i <= nret; ++i)
            {
                if (lua_isfunction(L, i) && clvalue(index2addr(L, i)) == origFn)
                {
                    lua_rawcheckstack(L, 1);
                    push_closure(L, hookedFn);
                    lua_replace(L, i);
                    break;
                }
            }
        }

        return nret;
    }

    inline void ensure_debug_info_hook(lua_State* L)
    {
        int top = lua_gettop(L);

        lua_getglobal(L, "debug");
        if (!lua_istable(L, -1))
        {
            lua_settop(L, top);
            return;
        }
        int dbgIdx = top + 1;

        lua_getfield(L, dbgIdx, "info");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return;
        }

        Closure* info = clvalue(index2addr(L, -1));
        if ((info->isC && info->c.f == debug_info_wrapper) || info == DebugInfoWrapperClosure)
        {
            // already wrapped on this state: leave everything alone.
            // The pointer check covers a same-global re-init that ran
            // InitializeAntiDetect after us and rewrote our wrapper's c.f to
            // anti_debug_info in place (then c.f != wrapper but it is still
            // our wrapper — re-wrapping would stack layers that only die
            // deep in the suite).
            lua_settop(L, top);
            return;
        }

        // first wrap on this state: capture the original implementation
        if (DebugInfoOriginalRef != LUA_NOREF)
        {
            // Only release the old slot when it belongs to THIS global.
            // Otherwise the old-state slot number would be unref'd against
            // the new registry, corrupting the new freelist (later refs
            // collide -> shared slots -> use-after-free deep in the suite).
            // Leaking one slot on a dying state is harmless.
            if (DebugInfoOwnerGlobal == (void*)L->global)
                lua_unref(L, DebugInfoOriginalRef);
        }
        lua_pushvalue(L, -1);
        DebugInfoOriginalRef = lua_ref(L, -1);
        lua_pop(L, 1); // info

        if (DebugInfoOriginalRef == LUA_NOREF)
        {
            DebugInfoOwnerGlobal = nullptr;
            lua_settop(L, top);
            return;
        }
        DebugInfoOwnerGlobal = (void*)L->global;

        // fresh writable copy of the debug table with `info` replaced
        lua_newtable(L);
        int newIdx = top + 2;

        if (lua_getmetatable(L, dbgIdx))
            lua_setmetatable(L, newIdx);

        lua_pushnil(L);
        while (lua_next(L, dbgIdx))
        {
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_rawset(L, newIdx);
        }

        lua_pushcclosurek(L, debug_info_wrapper, ("info"), 0, DispatchContinuation);
        DebugInfoWrapperClosure = clvalue(index2addr(L, -1));
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            Shared::ExecutorClosures.insert(DebugInfoWrapperClosure);
        }
        // Intentionally NOT added to function_array (raw, non-GC-rooted
        // pointers; see note above). ExecutorClosures covers the match.
        lua_setfield(L, newIdx, "info");

        lua_setglobal(L, "debug");
        lua_settop(L, top);
    }

    // ------------------------------------------------------------------
    // detection fix: Executor Security
    //
    // the game answers executor probes through a BindableFunction:Invoke
    // channel. The filter answers META_SECURITY with false and forwards
    // every other call to the real implementation.
    // ------------------------------------------------------------------
    static int InvokeSecurityFilter(lua_State* L)
    {
        int n = lua_gettop(L);
        for (int i = 1; i <= 2 && i <= n; ++i)
        {
            if (lua_type(L, i) == LUA_TSTRING)
            {
                const char* key = lua_tostring(L, i);
                if (key && strcmp(key, "META_SECURITY") == 0)
                {
                    lua_pushboolean(L, false);
                    return 1;
                }
            }
        }

        if (InvokeOriginalRef == LUA_NOREF)
            luaL_error(L, ("Invoke: original function missing"));

        return call_referenced_function(L, InvokeOriginalRef);
    }

    inline void EnsureInvokeSecurityHook(lua_State* L)
    {
        if (InvokeHookInstalled)
            return;

        int top = lua_gettop(L);

        lua_getglobal(L, "Instance");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return;
        }
        lua_pushstring(L, "BindableFunction");
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            lua_settop(L, top);
            return;
        }

        lua_getfield(L, -1, "Invoke");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return;
        }

        // set before the inner hookfunction call so re-entry is a no-op
        InvokeHookInstalled = true;

        lua_pushcclosurek(L, hookfunction, ("hookfunction"), 0, nullptr);
        lua_pushvalue(L, -2); // target: shared Invoke closure
        lua_pushcclosurek(L, InvokeSecurityFilter, ("InvokeSecurityFilter"), 0, DispatchContinuation);
        // [instance][Invoke][hookfunction][Invoke][filter]

        if (lua_pcall(L, 2, 1, 0) == LUA_OK)
        {
            if (lua_isfunction(L, -1))
            {
                if (InvokeOriginalRef != LUA_NOREF)
                    lua_unref(L, InvokeOriginalRef);
                InvokeOriginalRef = lua_ref(L, -1);
            }
            lua_pop(L, 1);
        }
        else
        {
            lua_pop(L, 1); // error message
        }

        lua_settop(L, top);
    }

    // ------------------------------------------------------------------
    // hooksignal / restoresignal
    //
    // hooksignal snapshots every live connection on the signal, disconnects
    // them and connects the callback in their place. restoresignal drops the
    // callback and reconnects the original handlers.
    // ------------------------------------------------------------------
    inline int restoresignal(lua_State* L)
    {
        luaL_checkany(L, 1);

        void* key = lua_touserdata(L, 1);
        auto it = key ? SignalRecords.find(key) : SignalRecords.end();
        if (it == SignalRecords.end())
        {
            lua_pushboolean(L, false);
            return 1;
        }

        SignalRecord rec = it->second;
        SignalRecords.erase(it);

        int top = lua_gettop(L);

        // reconnect the original handlers
        if (rec.FunctionsRef != LUA_NOREF)
        {
            lua_getref(L, rec.FunctionsRef);
            int funcsIdx = lua_gettop(L);
            int n = (int)lua_objlen(L, funcsIdx);
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, funcsIdx, i); // [funcs][fn]
                if (lua_isfunction(L, -1))
                {
                    lua_getfield(L, 1, "Connect"); // [funcs][fn][Connect]
                    if (lua_isfunction(L, -1))
                    {
                        lua_pushvalue(L, 1);          // [funcs][fn][Connect][self]
                        lua_pushvalue(L, funcsIdx+1); // [funcs][fn][Connect][self][fn]
                        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
                            lua_pop(L, 1);
                    }
                    else
                    {
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1); // fn
            }
            lua_pop(L, 1); // funcs
        }

        // disconnect the hook callback
        if (rec.HookRef != LUA_NOREF)
        {
            lua_getref(L, rec.HookRef);
            lua_getfield(L, -1, "Disconnect");
            if (lua_isfunction(L, -1))
            {
                lua_pushvalue(L, -2);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                    lua_pop(L, 1);
            }
            else
            {
                lua_pop(L, 1);
            }
            lua_pop(L, 1); // connection
        }

        if (rec.ConnectionsRef != LUA_NOREF) lua_unref(L, rec.ConnectionsRef);
        if (rec.FunctionsRef != LUA_NOREF) lua_unref(L, rec.FunctionsRef);
        if (rec.HookRef != LUA_NOREF) lua_unref(L, rec.HookRef);
        if (rec.SignalRef != LUA_NOREF) lua_unref(L, rec.SignalRef);

        lua_settop(L, top);
        lua_pushboolean(L, true);
        return 1;
    }

    inline int hooksignal(lua_State* L)
    {
        luaL_checkany(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        lua_getfield(L, 1, "Connect");
        bool isSignal = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (!isSignal)
            luaL_argerror(L, 1, ("expected an RBXScriptSignal"));

        void* key = lua_touserdata(L, 1);
        if (!key)
            luaL_argerror(L, 1, ("expected an RBXScriptSignal"));

        int top = lua_gettop(L);

        // re-hooking the same signal: put the originals back first
        if (SignalRecords.contains(key))
        {
            restoresignal(L);
            lua_pop(L, 1);
        }

        SignalRecord rec;
        rec.SignalRef = lua_ref(L, 1);

        // snapshot + disconnect current connections (best effort)
        int snapTop = lua_gettop(L);
        lua_getglobal(L, "getconnections");
        if (lua_isfunction(L, -1))
        {
            lua_pushvalue(L, 1);
            if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_istable(L, -1))
            {
                int connsIdx = lua_gettop(L);
                rec.ConnectionsRef = lua_ref(L, connsIdx);

                lua_newtable(L);
                int funcsIdx = lua_gettop(L);

                int n = (int)lua_objlen(L, connsIdx);
                for (int i = 1; i <= n; ++i)
                {
                    lua_rawgeti(L, connsIdx, i); // [conns][funcs][conn]
                    if (!lua_isuserdata(L, -1))
                    {
                        lua_pop(L, 1);
                        continue;
                    }

                    lua_getfield(L, -1, "Function"); // [conns][funcs][conn][fn]
                    if (lua_isfunction(L, -1))
                    {
                        lua_pushvalue(L, -1);
                        lua_rawseti(L, funcsIdx, i);
                    }
                    lua_pop(L, 1);

                    lua_getfield(L, -1, "Disconnect"); // [conns][funcs][conn][Disc]
                    if (lua_isfunction(L, -1))
                    {
                        lua_pushvalue(L, -2);
                        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                            lua_pop(L, 1);
                    }
                    else
                    {
                        lua_pop(L, 1);
                    }

                    lua_pop(L, 1); // conn
                }

                rec.FunctionsRef = lua_ref(L, funcsIdx);
            }
        }
        lua_settop(L, snapTop);

        // connect the hook callback
        lua_getfield(L, 1, "Connect");
        if (!lua_isfunction(L, -1))
        {
            if (rec.SignalRef != LUA_NOREF) lua_unref(L, rec.SignalRef);
            if (rec.ConnectionsRef != LUA_NOREF) lua_unref(L, rec.ConnectionsRef);
            if (rec.FunctionsRef != LUA_NOREF) lua_unref(L, rec.FunctionsRef);
            lua_settop(L, top);
            luaL_argerror(L, 1, ("signal has no Connect method"));
        }

        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        if (lua_pcall(L, 2, 1, 0) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            if (rec.SignalRef != LUA_NOREF) lua_unref(L, rec.SignalRef);
            if (rec.ConnectionsRef != LUA_NOREF) lua_unref(L, rec.ConnectionsRef);
            if (rec.FunctionsRef != LUA_NOREF) lua_unref(L, rec.FunctionsRef);
            luaL_error(L, ("hooksignal: %s"), err ? err : "?");
        }

        rec.HookRef = lua_ref(L, -1);
        lua_pop(L, 1);

        SignalRecords[key] = rec;
        lua_settop(L, top);
        lua_pushboolean(L, true);
        return 1;
    }

    inline int iscclosure(lua_State* L)
    {
        if (!lua_isfunction(L, 1))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, lua_iscfunction(L, 1));
        return 1;
    }

    inline int islclosure(lua_State* L)
    {
        if (!lua_isfunction(L, 1))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, !lua_iscfunction(L, 1));
        return 1;
    }

    // ------------------------------------------------------------------
    // checkcaller / executor-identity hardening.
    //
    // Environment->function_array holds RAW Closure* entries that are not
    // GC roots: re-init orphans, displaced globals and short-lived wrappers
    // stay in the vector after the GC frees them. Every consumer that
    // dereferences an entry (checkcaller frame walks, IsExecutorClosure)
    // must therefore probe before touching it: a wild read here does not
    // raise a Lua error (SEH escapes luaD_rawrunprotected, which has no
    // catch(...)) and kills the client with no further log line.
    // Pointer equality never faults, so it stays unguarded; every
    // dereference goes through ClosureHeaderReadable plus the isC union
    // guard. Identical results to the raw loop on live data.
    // ------------------------------------------------------------------
    static bool ClosureHeaderReadable(const Closure* c) noexcept
    {
        if (!c)
            return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(c);
        if (addr < 0x10000)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        // Bytes actually read below: the isC flag plus the c.f slot, both in
        // the low header prefix on every Luau build.
        constexpr size_t kNeed = 64;
        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        return addr >= base && addr + kNeed >= addr && addr + kNeed <= base + mbi.RegionSize;
    }

    // Bounded chunk-source match (valid_desc_cstr contract): every consumer
    // scans to NUL, so confirm a NUL exists in-region before strcmp runs.
    // Same true-set as the raw strcmp chain on live strings; stale protos
    // fail closed instead of over-reading.
    static bool SafeExecutorChunkSource(const char* src) noexcept
    {
        if (!src)
            return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(src);
        if (addr < 0x10000)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(src), &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t end = base + mbi.RegionSize;
        if (addr < base || addr >= end)
            return false;
        size_t avail = static_cast<size_t>(end - addr);
        size_t cap = avail < 128 ? avail : 128;
        bool hasNul = false;
        for (size_t i = 0; i < cap; ++i)
        {
            if (src[i] == '\0')
            {
                hasNul = true;
                break;
            }
        }
        if (!hasNul)
            return false;
        return strcmp(src, ("=loadstring")) == 0 ||
            strcmp(src, ("@Seliware")) == 0 ||
            strcmp(src, ("@Nam")) == 0 ||
            strcmp(src, ("@Arona")) == 0;
    }

    static bool SafeFunctionArrayMatch(Closure* cl)
    {
        if (!cl || !Environment)
            return false;
        // The query itself must be readable: checkcaller passes stack-rooted
        // live closures, but a concurrent restore (c.f/proto swap tearing the
        // same body mid-walk) must fail closed here, not fault on cl->c.f.
        // Callers only invoke this for isC frames; enforce it so a Lua query
        // can never type-pun l.p bytes as a C function pointer below.
        if (!ClosureHeaderReadable(cl) || !cl->isC)
            return false;
        // Snapshot size first: actor run_on_actor appends via TrackClosure
        // without ResetState while game threads walk here. Range-for holds
        // begin/end across a realloc (iterator invalidation -> UAF/hang);
        // an index loop re-fetches begin each iteration. Growth mid-walk is
        // ignored, shrink breaks early -- both fail closed. Same true-set on
        // stable data.
        const size_t n = Environment->function_array.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (i >= Environment->function_array.size())
                break;
            Closure* reg = Environment->function_array[i];
            if (!reg)
                continue;
            if (reg == cl)
                return true;
            if (!ClosureHeaderReadable(reg))
                continue;
            if (!reg->isC)
                continue;
            if (reg->c.f == cl->c.f)
                return true;
        }
        return false;
    }

    // Iterative DFS with a visited set: the old recursion consumed one C
    // frame per proto nesting level, so a deeply nested obfuscated closure
    // (probed here on every isexecutorclosure call) could exhaust the native
    // stack with no log. Same boolean result on every non-pathological input.
    inline bool IsExecutorProto(Proto* proto)
    {
        if (!proto)
            return false;

        std::vector<Proto*> pending;
        std::unordered_set<Proto*> visited;
        pending.push_back(proto);

        while (!pending.empty())
        {
            Proto* cur = pending.back();
            pending.pop_back();
            if (!cur || !visited.insert(cur).second)
                continue;

            if (cur->userdata)
            {
                void* raw = cur->userdata.Get();
                if (raw && *static_cast<uintptr_t*>(raw) == MaxCapabilities)
                    return true;
            }

            if (cur->p && cur->sizep > 0 && cur->sizep < 100000)
            {
                for (int i = 0; i < cur->sizep; ++i)
                {
                    if (cur->p[i])
                        pending.push_back(cur->p[i]);
                }
            }
        }

        return false;
    }

    inline bool IsExecutorClosure(Closure* closure)
    {
        if (!closure)
            return false;

        // All four sets are mutated on hook/restore paths; read them under the
        // leaf lock so a concurrent insert/erase can't rehash mid-read.
        bool owned = false;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            owned = Shared::ExecutorClosures.contains(closure) ||
                WrappedClosures.contains(closure) ||
                NewCClosureRefs.contains(closure) ||
                HookRecords.contains(closure);
        }
        if (owned)
        {
            return true;
        }

        // Probe before any deref: same tearing rationale as checkcaller/Safe
        // below. Pointer-equality above never faults, so it stays first.
        // Live closures pass; dangling actor entries fail closed to the
        // probed scan instead of faulting on ->isC/->c.f.
        if (!ClosureHeaderReadable(closure))
            return false;

        if (!closure->isC)
            return IsExecutorProto(closure->l.p);

        if (closure->c.f == NewCClosureDispatcher ||
            closure->c.f == HookCDispatcher ||
            closure->c.f == HookLuaDispatcher)
        {
            return true;
        }

        // Snapshot size first (see SafeFunctionArrayMatch): actor appends
        // concurrently, range-for iterators would dangle across a realloc.
        const size_t m = Environment ? Environment->function_array.size() : 0;
        for (size_t i = 0; i < m; ++i)
        {
            if (!Environment || i >= Environment->function_array.size())
                break;
            Closure* registered = Environment->function_array[i];
            if (!registered)
                continue;

            if (registered == closure)
                return true;

            // Probe + union guard (see helpers above): the raw
            // registered->c.f read faults on GC'd entries.
            if (!ClosureHeaderReadable(registered))
                continue;

            if (registered->isC && registered->c.f == closure->c.f)
                return true;
        }

        return false;
    }

    inline auto is_executor_closure(lua_State* rl) -> int
    {
        if (lua_type(rl, 1) != LUA_TFUNCTION) {
            lua_pushboolean(rl, false);
            return 1;
        }

        Closure* closure = clvalue(index2addr(rl, 1));
        lua_pushboolean(rl, IsExecutorClosure(closure));
        return 1;
    }

    // Hardened checkcaller. Same boolean contract as the Script
    // implementation it supersedes (registered later, so this global wins):
    // fast paths first, then a 10-level lua_getinfo walk matching executor
    // frames by validated function_array membership, bounded chunk-source
    // compare, or WrappedClosures. No new true-signals were added: frames
    // the old code reported false for (hook dispatchers, game frames) still
    // report false. Every anomaly fails closed to false, never faults, and
    // no Lua API runs under locks (IsFireDispatchThread only takes its own
    // mutex and touches no Lua state).
    static int checkcaller(lua_State* L)
    {
        if (!L)
            return 0;

        if (Shared::IsFireDispatchThread(L))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        if (Shared::ExploitThread && L == Shared::ExploitThread)
        {
            lua_pushboolean(L, true);
            return 1;
        }
        if (L->userdata &&
            (L->userdata->script.expired() ||
                L->userdata->capabilities == MaxCapabilities))
        {
            lua_pushboolean(L, true);
            return 1;
        }
        if (!Environment)
        {
            lua_pushboolean(L, false);
            return 1;
        }

        lua_Debug ar;
        for (int level = 1; level <= 10; level++)
        {
            if (!lua_getinfo(L, level, ("f"), &ar))
                break;
            if (lua_isfunction(L, -1))
            {
                // Stack-rooted by lua_getinfo, but the body can tear under a
                // concurrent restore (hookmetamethod c.f/proto swap on the
                // same Closure* mid-walk). Probe before ANY deref (->isC,
                // ->l.p, ->c.f); unreadable fails closed for this level.
                // Fast hash first (no VirtualQuery, no fault), slow probed
                // scan last -- pure OR, same true-set, cheaper false path
                // for game __namecall frames (10 levels x 100+ entries of
                // VirtualQuery per hook hit was a freeze-shaped slowdown).
                Closure* cl = clvalue(index2addr(L, -1));
                if (ClosureHeaderReadable(cl))
                {
                    // Pure map read under the leaf lock (no Lua inside); the
                    // fast hash stays first, same true-set, fail-closed.
                    bool isWrapped = false;
                    {
                        std::lock_guard<std::mutex> lock(HookRecordsMutex);
                        isWrapped = WrappedClosures.count(cl) > 0;
                    }
                    if (isWrapped)
                    {
                        lua_pop(L, 1);
                        lua_pushboolean(L, true);
                        return 1;
                    }
                    if (cl->isC)
                    {
                        if (SafeFunctionArrayMatch(cl))
                        {
                            lua_pop(L, 1);
                            lua_pushboolean(L, true);
                            return 1;
                        }
                    }
                    else if (cl->l.p)
                    {
                        // Proto header probe before ->source: source lives at
                        // +0x18, so the 64-byte Closure-sized check covers it.
                        // Same true-set on live protos; torn protos skip.
                        const Closure* asClosure = reinterpret_cast<const Closure*>(cl->l.p);
                        if (ClosureHeaderReadable(asClosure) && cl->l.p->source)
                        {
                            if (SafeExecutorChunkSource(getstr(cl->l.p->source)))
                            {
                                lua_pop(L, 1);
                                lua_pushboolean(L, true);
                                return 1;
                            }
                        }
                    }
                }
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, false);
        return 1;
    }



    inline int gettenv(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TTHREAD);
        lua_State* thread = lua_tothread(L, 1);
        if (!thread) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushvalue(thread, LUA_GLOBALSINDEX);
        lua_xmove(thread, L, 1);
        return 1;
    }
    static int isfunctionhooked(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* closure = clvalue(index2addr(L, 1));
        // Copy-out: map op under the leaf lock, push runs unlocked.
        int origRefCopy = LUA_NOREF;
        int hookRefCopy = LUA_NOREF;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(HookRecordsMutex);
            auto it = HookRecords.find(closure);
            if (it != HookRecords.end())
            {
                found = true;
                origRefCopy = it->second.OriginalRef;
                hookRefCopy = it->second.HookRef;
            }
        }
        (void)hookRefCopy;
        lua_pushboolean(L, found && origRefCopy != LUA_NOREF);
        return 1;
    }

    inline int compareclosures(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        Closure* a = clvalue(index2addr(L, 1));
        Closure* b = clvalue(index2addr(L, 2));

        if (!a || !b) {
            lua_pushboolean(L, false);
            return 1;
        }
        // Probe before touching isC/union: a concurrent hook/restore can tear
        // the same body mid-compare. Live args pass; torn bodies fail closed
        // to false instead of type-punning l.p bytes as a C function pointer.
        if (!ClosureHeaderReadable(a) || !ClosureHeaderReadable(b)) {
            lua_pushboolean(L, false);
            return 1;
        }

        bool same = false;
        if (a->isC && b->isC) {
            same = (a->c.f == b->c.f);
        }
        else if (!a->isC && !b->isC) {
            same = (a->l.p == b->l.p);
        }

        lua_pushboolean(L, same);
        return 1;
    }

    // setstackhidden(funcOrLevel, hidden)
    //   funcOrLevel : function | non-negative number (stack level)
    //   hidden      : boolean
    inline int setstackhidden(lua_State* L)
    {
        luaL_checkany(L, 1);
        const bool hidden = luaL_checkboolean(L, 2) != 0;

        if (lua_isnumber(L, 1))
        {
            const int level = (int)lua_tointeger(L, 1);
            if (level < 0)
                luaL_argerror(L, 1, "level must be non-negative");

            if (hidden)
            {
                // Remember the level; resolve to a Closure* lazily on first
                // traceback so the frame is hidden even if called before the
                // target function is on the stack.
                HiddenLevels.push_back({L, level});
            }
            else
            {
                // Resolve now and unhide whatever sits at that level.
                lua_Debug ar;
                if (lua_getinfo(L, level, "f", &ar))
                {
                    if (lua_isfunction(L, -1))
                    {
                        Closure* cl = clvalue(index2addr(L, -1));
                        UnhideClosure(cl);
                    }
                    lua_pop(L, 1);
                }
                // Also drop any matching pending level entries.
                for (auto it = HiddenLevels.begin(); it != HiddenLevels.end();)
                {
                    if (it->L == L && it->level == level)
                        it = HiddenLevels.erase(it);
                    else
                        ++it;
                }
            }
            return 0;
        }

        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* cl = clvalue(index2addr(L, 1));

        if (hidden)
            HideClosure(cl);
        else
            UnhideClosure(cl);

        return 0;
    }

    void register_lib(lua_State* L);
}

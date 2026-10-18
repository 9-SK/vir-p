#pragma once
#include <string>
#include <cstdint>
#include <cstring>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
#include <lstate.h>
#include <lualib.h>
#include <string>
#include <cstdint>
#include <cstring>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <lstate.h>
#include <lualib.h>
#include <zstd.h>
#include <xxhash.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>

namespace Closures
{
#define lua_check(L, i) if (lua_gettop(L) > i) { lua_settop(L, i); }

    struct HookRecord
    {
        int OriginalRef = LUA_NOREF;
        int HookRef = LUA_NOREF;
    };

    struct NewCClosureRecord
    {
        int FunctionRef = LUA_NOREF;
        bool Reusable = false;
    };

    static constexpr const char* LuaHookDispatcherName = "__hookfunction_dispatch";
    static std::unordered_map<Closure*, HookRecord> HookRecords;
    static std::unordered_map<Closure*, NewCClosureRecord> NewCClosureRefs;
    static std::unordered_set<Closure*> WrappedClosures;
    static int LuaHookDispatcherRef = LUA_NOREF;
    inline void ResetLibraryState()
    {
        HookRecords.clear();
        NewCClosureRefs.clear();
        WrappedClosures.clear();
        LuaHookDispatcherRef = LUA_NOREF;
    }

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

            auto wrapped = NewCClosureRefs.find(source);
            if (wrapped != NewCClosureRefs.end())
            {
                NewCClosureRefs[clone] = { wrapped->second.FunctionRef, false };
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
        auto it = NewCClosureRefs.find(current);
        if (it == NewCClosureRefs.end() || it->second.FunctionRef == LUA_NOREF)
        {
            luaL_error(L, ("newcclosure: missing wrapped function"));
        }

        return call_referenced_function(L, it->second.FunctionRef);
    }

    static int HookCDispatcher(lua_State* L)
    {
        Closure* current = curr_func(L);
        auto it = HookRecords.find(current);
        if (it == HookRecords.end() || it->second.HookRef == LUA_NOREF)
        {
            luaL_error(L, ("hookfunction: missing hook"));
        }

        return call_referenced_function(L, it->second.HookRef);
    }

    static int HookLuaDispatcher(lua_State* L)
    {
        Closure* target = to_closure(L, 1);
        lua_remove(L, 1);

        auto it = HookRecords.find(target);
        if (it == HookRecords.end() || it->second.HookRef == LUA_NOREF)
        {
            luaL_error(L, ("hookfunction: missing Lua hook"));
        }

        return call_referenced_function(L, it->second.HookRef);
    }

    static Proto* ensure_lua_hook_dispatcher_proto(lua_State* L)
    {
        if (LuaHookDispatcherRef != LUA_NOREF)
        {
            lua_getref(L, LuaHookDispatcherRef);
            Closure* closure = clvalue(index2addr(L, -1));
            Proto* proto = closure->l.p;
            lua_pop(L, 1);
            return proto;
        }

        std::string bytecode = Execution::CompileScript(("local f = debug.info(1, \"f\"); return __hookfunction_dispatch(f, ...)"));
        if (luau_load(L, ("=hookfunction"), bytecode.data(), bytecode.size(), NULL) != LUA_OK)
        {
            lua_error(L);
        }

        Closure* closure = clvalue(index2addr(L, -1));
        TaskScheduler::SetProtoCapabilities(closure->l.p, &MaxCapabilities);
        LuaHookDispatcherRef = lua_ref(L, -1);
        Proto* proto = closure->l.p;
        lua_pop(L, 1);
        return proto;
    }

    static int clonefunction(lua_State* L)
    {
        Closure* source = to_closure(L, 1);
        push_clone(L, source);
        return 1;
    }

    static int hookfunction(lua_State* L)
    {
        Closure* target = to_closure(L, 1);
        Closure* hook = to_closure(L, 2);

        HookRecord& record = HookRecords[target];
        if (record.OriginalRef == LUA_NOREF)
        {
            auto wrappedTarget = NewCClosureRefs.find(target);
            if (target->isC && wrappedTarget != NewCClosureRefs.end() && wrappedTarget->second.FunctionRef != LUA_NOREF)
            {
                lua_getref(L, wrappedTarget->second.FunctionRef);
            }
            else
            {
                push_clone(L, target);
            }

            record.OriginalRef = lua_ref(L, -1);
            lua_pop(L, 1);
        }
        else
        {
            lua_getref(L, record.OriginalRef);
            Closure* original = to_closure(L, -1);

            if (hook == original)
            {
                if (target->isC != original->isC)
                {
                    auto targetWrapped = NewCClosureRefs.find(target);
                    if (target->isC && !original->isC && targetWrapped != NewCClosureRefs.end())
                    {
                        target->stacksize = LUA_MINSTACK;
                        target->preload = 0;
                        target->nupvalues = 0;
                        target->env = original->env;
                        target->c.f = NewCClosureDispatcher;
                        target->c.cont = DispatchContinuation;
                        target->c.debugname = reinterpret_cast<TString*>(const_cast<char*>(("newcclosure")));
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
                        auto originalWrapped = NewCClosureRefs.find(original);
                        auto targetWrapped = NewCClosureRefs.find(target);

                        if (originalWrapped != NewCClosureRefs.end())
                        {
                            NewCClosureRefs[target] = { originalWrapped->second.FunctionRef, true };
                            WrappedClosures.insert(target);
                        }
                        else if (targetWrapped != NewCClosureRefs.end() && targetWrapped->second.Reusable)
                        {
                            NewCClosureRefs.erase(targetWrapped);
                            WrappedClosures.erase(target);
                        }
                    }
                }

                if (record.HookRef != LUA_NOREF)
                    lua_unref(L, record.HookRef);

                lua_unref(L, record.OriginalRef);
                HookRecords.erase(target);
                Shared::ExecutorClosures.erase(target);

                return 1;
            }

            lua_pop(L, 1);

            if (record.HookRef != LUA_NOREF)
            {
                lua_unref(L, record.HookRef);
                record.HookRef = LUA_NOREF;
            }
        }

        lua_pushvalue(L, 2);
        record.HookRef = lua_ref(L, -1);
        lua_pop(L, 1);

        if (target->isC)
        {
            target->c.f = HookCDispatcher;
            target->c.cont = DispatchContinuation;
            target->c.debugname = reinterpret_cast<TString*>(const_cast<char*>(("hookfunction")));
        }
        else
        {
            Proto* dispatcher = ensure_lua_hook_dispatcher_proto(L);

            target->stacksize = dispatcher->maxstacksize;
            target->preload = 0;
            target->nupvalues = 0;
            target->env = L->gt;
            target->l.p = dispatcher;
            luaC_objbarrier(L, target, L->gt);
            luaC_objbarrier(L, target, dispatcher);
        }

        Shared::ExecutorClosures.insert(target);
        lua_getref(L, record.OriginalRef);
        return 1;
    }

    static int restorefunction(lua_State* L)
    {
        Closure* target = to_closure(L, 1);
        auto it = HookRecords.find(target);
        if (it == HookRecords.end() || it->second.OriginalRef == LUA_NOREF)
        {
            lua_pushboolean(L, false);
            return 1;
        }

        lua_getref(L, it->second.OriginalRef);
        Closure* original = to_closure(L, -1);

        if (target->isC != original->isC)
        {
            auto targetWrapped = NewCClosureRefs.find(target);
            if (target->isC && !original->isC && targetWrapped != NewCClosureRefs.end())
            {
                target->stacksize = LUA_MINSTACK;
                target->preload = 0;
                target->nupvalues = 0;
                target->env = original->env;
                target->c.f = NewCClosureDispatcher;
                target->c.cont = DispatchContinuation;
                target->c.debugname = reinterpret_cast<TString*>(const_cast<char*>(("newcclosure")));
                if (original->env)
                    luaC_objbarrier(L, target, original->env);

                lua_pop(L, 1);

                if (it->second.HookRef != LUA_NOREF)
                    lua_unref(L, it->second.HookRef);

                lua_unref(L, it->second.OriginalRef);
                HookRecords.erase(it);

                lua_pushboolean(L, true);
                return 1;
            }

            lua_pop(L, 1);
            luaL_error(L, ("restorefunction: incompatible closure layout"));
        }

        copy_closure_body(L, target, original);
        if (target->isC)
        {
            auto originalWrapped = NewCClosureRefs.find(original);
            auto targetWrapped = NewCClosureRefs.find(target);

            if (originalWrapped != NewCClosureRefs.end())
            {
                NewCClosureRefs[target] = { originalWrapped->second.FunctionRef, true };
                WrappedClosures.insert(target);
            }
            else if (targetWrapped != NewCClosureRefs.end() && targetWrapped->second.Reusable)
            {
                NewCClosureRefs.erase(targetWrapped);
                WrappedClosures.erase(target);
            }
        }
        lua_pop(L, 1);

        if (it->second.HookRef != LUA_NOREF)
            lua_unref(L, it->second.HookRef);

        lua_unref(L, it->second.OriginalRef);
        HookRecords.erase(it);

        lua_pushboolean(L, true);
        return 1;
    }

    int newcclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* closure = clvalue(index2addr(L, 1));
        if (closure->isC) {
            lua_pushvalue(L, 1);
            return 1;
        }
        for (auto& pair : NewCClosureRefs) {
            if (!pair.second.Reusable)
                continue;

            lua_getref(L, pair.second.FunctionRef);
            bool same = lua_isfunction(L, -1) && clvalue(index2addr(L, -1)) == closure;
            lua_pop(L, 1);

            if (same) {
                push_closure(L, pair.first);
                return 1;
            }
        }
        const char* debugname = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("newcclosure");
        lua_pushcclosurek(L, NewCClosureDispatcher, debugname, 0, DispatchContinuation);
        Closure* wrapper = clvalue(index2addr(L, -1));

        lua_pushvalue(L, 1);
        NewCClosureRefs[wrapper] = { lua_ref(L, -1), true };
        lua_pop(L, 1);

        WrappedClosures.insert(wrapper);
        wrapper->env = closure->env;
        Shared::ExecutorClosures.insert(wrapper);
        Environment::function_array.push_back(wrapper);
        return 1;
    }

    int isnewcclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* closure = clvalue(index2addr(L, 1));
        lua_pushboolean(L, WrappedClosures.count(closure) > 0);
        return 1;
    }

    int loadstring(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TSTRING);

        const char* ChunkName = luaL_optstring(L, 2, ("viridian"));

        std::string Bytecode = Execution::CompileScript(lua_tostring(L, 1));

        if (luau_load(L, ChunkName, Bytecode.data(), Bytecode.size(), NULL) != LUA_OK)
        {
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }

        Closure* cls = clvalue(luaA_toobject(L, -1));
        TaskScheduler::SetProtoCapabilities(cls->l.p, &MaxCapabilities);

        lua_setsafeenv(L, LUA_ENVIRONINDEX, false);

        return 1;
    }

    int iscclosure(lua_State* L)
    {
        if (!lua_isfunction(L, 1))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, lua_iscfunction(L, 1));
        return 1;
    }

    int islclosure(lua_State* L)
    {
        if (!lua_isfunction(L, 1))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        lua_pushboolean(L, !lua_iscfunction(L, 1));
        return 1;
    }

    inline bool IsExecutorProto(Proto* proto)
    {
        if (!proto)
            return false;

        if (proto->userdata)
        {
            void* raw = proto->userdata.Get();
            if (raw && *static_cast<uintptr_t*>(raw) == MaxCapabilities)
                return true;
        }

        for (int i = 0; proto->p && i < proto->sizep; ++i)
        {
            if (IsExecutorProto(proto->p[i]))
                return true;
        }

        return false;
    }

    inline bool IsExecutorClosure(Closure* closure)
    {
        if (!closure)
            return false;

        if (Shared::ExecutorClosures.contains(closure) ||
            WrappedClosures.contains(closure) ||
            NewCClosureRefs.contains(closure) ||
            HookRecords.contains(closure))
        {
            return true;
        }

        if (!closure->isC)
            return IsExecutorProto(closure->l.p);

        if (closure->c.f == NewCClosureDispatcher ||
            closure->c.f == HookCDispatcher ||
            closure->c.f == HookLuaDispatcher)
        {
            return true;
        }

        for (Closure* registered : Environment::function_array)
        {
            if (!registered)
                continue;

            if (registered == closure)
                return true;

            if (registered->isC && registered->c.f == closure->c.f)
                return true;
        }

        return false;
    }

    auto is_executor_closure(lua_State* rl) -> int
    {
        if (lua_type(rl, 1) != LUA_TFUNCTION) {
            lua_pushboolean(rl, false);
            return 1;
        }

        Closure* closure = clvalue(index2addr(rl, 1));
        lua_pushboolean(rl, IsExecutorClosure(closure));
        return 1;
    }

    inline int cloneref(lua_State* L) {
        lua_check(L, 1);
        luaL_checktype(L, 1, LUA_TUSERDATA);
        std::string ud_t = luaL_typename(L, 1);
        if (ud_t != ("Instance"))
            luaL_typeerrorL(L, 1, ("Instance"));
        const auto instance = *static_cast<void**>(lua_touserdata(L, 1));
        lua_pushlightuserdata(L, Hyperion::PushInstance);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_pushlightuserdata(L, instance);
        lua_rawget(L, -2);
        lua_pushlightuserdata(L, instance);
        lua_pushnil(L);
        lua_rawset(L, -4);
        Hyperion::PushInstance(L, lua_touserdata(L, 1));
        lua_pushlightuserdata(L, instance);
        lua_pushvalue(L, -3);
        lua_rawset(L, -5);
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
        lua_pushboolean(L, HookRecords.count(closure) > 0 && HookRecords[closure].OriginalRef != LUA_NOREF);
        return 1;
    }

    int compareclosures(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        luaL_checktype(L, 2, LUA_TFUNCTION);

        Closure* a = clvalue(index2addr(L, 1));
        Closure* b = clvalue(index2addr(L, 2));

        if (!a || !b) {
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
    int checkcaller(lua_State* L) {
        if (Shared::VoltState && L == Shared::VoltState) {
            lua_pushboolean(L, true);
            return 1;
        }
        if (L && L->userdata &&
            (L->userdata->script.expired() ||
                L->userdata->capabilities == MaxCapabilities)) {
            lua_pushboolean(L, true);
            return 1;
        }
        lua_Debug ar;
        for (int level = 1; level <= 10; level++) {
            if (!lua_getinfo(L, level, ("f"), &ar))
                break;
            if (lua_isfunction(L, -1)) {
                Closure* cl = clvalue(index2addr(L, -1));
                if (cl->isC) {
                    for (auto func : Environment::function_array)
                        if (func == cl || func->c.f == cl->c.f) {
                            lua_pop(L, 1);
                            lua_pushboolean(L, true);
                            return 1;
                        }
                }
                else if (cl->l.p && cl->l.p->source) {
                    const char* src = getstr(cl->l.p->source);
                    if (src && strcmp(src, ("=loadstring")) == 0) {
                        lua_pop(L, 1);
                        lua_pushboolean(L, true);
                        return 1;
                    }
                }
                if (Closures::WrappedClosures.count(cl)) {
                    lua_pop(L, 1);
                    lua_pushboolean(L, true);
                    return 1;
                }
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, false);
        return 1;
    }
    void RegisterLibrary(lua_State* L)
    {
        lua_pushcclosurek(L, Closures::HookLuaDispatcher, LuaHookDispatcherName, 0, DispatchContinuation);
        lua_setglobal(L, LuaHookDispatcherName);

        Utils::AddFunction(L, ("loadstring"), Closures::loadstring);
        Utils::AddFunction(L, ("hookfunction"), Closures::hookfunction);
        Utils::AddFunction(L, ("hookfunc"), Closures::hookfunction);
        Utils::AddFunction(L, ("replaceclosure"), Closures::hookfunction);
        Utils::AddFunction(L, ("newcclosure"), Closures::newcclosure);
        Utils::AddFunction(L, ("isnewcclosure"), Closures::isnewcclosure);
        Utils::AddFunction(L, ("restorefunction"), Closures::restorefunction);
        Utils::AddFunction(L, ("restorefunc"), Closures::restorefunction);
        Utils::AddFunction(L, ("clonefunction"), Closures::clonefunction);
        Utils::AddFunction(L, ("iscclosure"), Closures::iscclosure);
        Utils::AddFunction(L, ("islclosure"), Closures::islclosure);
        Utils::AddFunction(L, ("checkcaller"), Closures::checkcaller);

        Utils::AddFunction(L, ("isourclosure"), Closures::is_executor_closure);
        Utils::AddFunction(L, ("checkclosure"), Closures::is_executor_closure);
        Utils::AddFunction(L, ("isexecutorclosure"), Closures::is_executor_closure);
        Utils::AddFunction(L, ("isexecutorfunction"), Closures::is_executor_closure);
        Utils::AddFunction(L, ("cloneref"), Closures::cloneref);
        Utils::AddFunction(L, ("gettenv"), gettenv);
        Utils::AddFunction(L, ("isfunctionhooked"), isfunctionhooked);
        Utils::AddFunction(L, ("compareclosures"), compareclosures);
    }
}

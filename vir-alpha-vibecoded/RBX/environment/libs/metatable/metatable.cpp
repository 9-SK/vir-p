#include "Metatable.hpp"

#include <Windows.h>
#include <lstate.h>
#include <lobject.h>
#include <lfunc.h>
#include <lstring.h>
#include <ltable.h>
#include <lapi.h>
#include <ldo.h>
#include <lgc.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <lualib.h>

#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>

#include <RBX/environment/libs/closures/closures.hpp>
#include <RBX/environment/env.hpp>


namespace Metatable {

    int getrawmetatable(lua_State* LS) {
        if (LS->userdata == nullptr)
            return 0;

        luaL_checkany(LS, 1);

        if (!lua_getmetatable(LS, 1))
            lua_pushnil(LS);

        return 1;
    }

    int setreadonly(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TBOOLEAN);
        hvalue(luaA_toobject(L, 1))->readonly = lua_toboolean(L, 2);
        return 0;
    }

    int isreadonly(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_pushboolean(L, hvalue(luaA_toobject(L, 1))->readonly);
        return 1;
    }

    int setrawmetatable(lua_State* L)
    {
        luaL_checkany(L, 1);

        const int metatableType = lua_type(L, 2);

        if (metatableType != LUA_TTABLE && metatableType != LUA_TNIL)
            luaL_typeerror(L, 2, ("table or nil"));

        lua_settop(L, 2);
        lua_setmetatable(L, 1);

        lua_pushvalue(L, 1);
        return 1;
    }

    int makereadonly(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);

        LuaTable* table = hvalue(luaA_toobject(L, 1));

        table->readonly = true;

        return 0;
    }

    int makewriteable(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);

        LuaTable* table = hvalue(luaA_toobject(L, 1));

        table->readonly = false;

        return 0;
    }

    int getnamecallmethod(lua_State* L)
    {
        const char* namecall = lua_namecallatom(L, nullptr);

        if (!namecall)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_pushstring(L, namecall);
        return 1;
    }
    inline int hookmetamethod(lua_State* L) {
        luaL_checkany(L, 1);
        luaL_checktype(L, 2, LUA_TSTRING);
        luaL_checktype(L, 3, LUA_TFUNCTION);

        if (!lua_getmetatable(L, 1))
            luaL_argerror(L, 1, "object has no metatable");

        const int mta = lua_gettop(L);
        lua_pushvalue(L, 2); // reuse the already-interned method string as key
        lua_rawget(L, mta);

        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            luaL_argerror(L, 2, "metamethod is not a function");
        }

        // In-place hook (slot identity preserved): mutate the SAME Closure*
        // that lives in the metatable slot, mirroring Closures::hookfunction.
        // Never lua_rawset/replace the slot, so identity probes
        // (Closure* equality before/after) see the same pointer with
        // swapped c.f (C, e.g. __namecall) or swapped proto (Lua, e.g. __index).
        Closure* target = clvalue(index2addr(L, -1));
        Closure* hook = clvalue(index2addr(L, 3));

        // Resolve the Lua dispatcher BEFORE touching HookRecords so a
        // compile/load failure can't leave a half-created record behind.
        // Balanced (pushes/pops internally): stack is still
        // [obj, method, hook, mt, old] on return.
        Proto* luaDispatcher = nullptr;
        if (!target->isC)
            luaDispatcher = Closures::ensure_lua_hook_dispatcher_proto(L);

        // Copy-out snapshot: map op under the leaf lock, all Lua work below
        // runs unlocked. Local `record` is a value copy; mutations publish
        // via map assign under lock with no Lua API inside.
        Closures::HookRecord record;
        bool hadHookRecord = false;
        {
            std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
            auto snapIt = Closures::HookRecords.find(target);
            if (snapIt != Closures::HookRecords.end())
            {
                record = snapIt->second;
                hadHookRecord = true;
            }
        }
        (void)hadHookRecord;
        if (record.OriginalRef == LUA_NOREF) {
            // First hook: clone the pristine body for the return value
            // and for later restore. Metatable slots are never newcclosures,
            // so plain clone (not FunctionRef unwrap) keeps restore symmetric.
            Closures::push_clone(L, target);
            record.OriginalRef = lua_ref(L, -1);
            lua_pop(L, 1);
            {
                std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                Closures::HookRecords[target] = record;
            }
        }
        else {
            // Already hooked: check restore-via-rehook (hook == original).
            lua_getref(L, record.OriginalRef);
            Closure* original = clvalue(index2addr(L, -1));

            if (hook == original) {
                // Restore: copy the saved body back into the SAME slot object.
                // isC never changes for metamethod targets (C stays C via
                // c.f swap, Lua stays Lua via proto swap), so a mismatch
                // means corruption — error before mutating anything.
                if (target->isC != original->isC) {
                    lua_pop(L, 1);
                    luaL_error(L, ("hookmetamethod: incompatible closure layout"));
                }

                Closures::copy_closure_body(L, target, original);

                if (record.HookRef != LUA_NOREF)
                    lua_unref(L, record.HookRef);
                lua_unref(L, record.OriginalRef);
                {
                    std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                    Closures::HookRecords.erase(target);
                }
                Shared::ExecutorClosures.erase(target);

                // Stack is [obj, method, hook, mt, old(target), original];
                // return top (the original clone). Registry refs are gone
                // but the object stays alive via the stack return slot.
                return 1;
            }

            lua_pop(L, 1); // drop original; re-hook below with a new HookRef

            if (record.HookRef != LUA_NOREF) {
                lua_unref(L, record.HookRef);
                record.HookRef = LUA_NOREF;
                {
                    std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                    auto publishIt = Closures::HookRecords.find(target);
                    if (publishIt != Closures::HookRecords.end())
                        publishIt->second.HookRef = LUA_NOREF;
                }
            }
        }

        // Point the live slot at the new hook. Ref leaves the stack untouched.
        // Copy-out write-back: Lua ref first (unlocked), then publish ints
        // under the leaf lock with no Lua API inside.
        record.HookRef = lua_ref(L, 3);
        TString* curDbgName = target->isC ? target->c.debugname : nullptr;
        if (!record.OriginalDebugName)
            record.OriginalDebugName = curDbgName;
        TString* dbgToApply = record.OriginalDebugName;
        {
            std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
            Closures::HookRecords[target] = record;
        }

        if (target->isC) {
            // C path (__namecall and friends): swap c.f to the shared
            // dispatcher, which resolves HookRecords[curr_func] -> HookRef.
            // Keep the original debugname so debug.info probes see it.
            target->c.f = Closures::HookCDispatcher;
            target->c.cont = Closures::DispatchContinuation;
            if (dbgToApply)
            {
                target->c.debugname = dbgToApply;
                luaC_objbarrier(L, target, dbgToApply);
            }
        }
        else {
            // Lua path (__index and friends): swap proto to the shared
            // dispatcher proto (debug.info(1,"f") -> HookLuaDispatcher).
            // Env-leak fix (mirrors Closures::hookfunction): executor targets
            // keep L->gt for ENV-test identity; game/custom targets get a
            // private P(empty)->D(debug,dispatch)->O(original) so direct
            // getfenv walks see a clean empty env while the dispatcher still
            // resolves its two globals through D.
            if (!luaDispatcher)
                luaDispatcher = Closures::ensure_lua_hook_dispatcher_proto(L);
            target->stacksize = luaDispatcher->maxstacksize;
            target->preload = 0;
            target->nupvalues = 0;
            int targetIdx = lua_gettop(L); // old slot (absolute)
            if (!Closures::install_private_hook_env(L, targetIdx))
            {
                target->env = L->gt;
                if (L->gt)
                    luaC_objbarrier(L, target, L->gt);
            }
            target->l.p = luaDispatcher;
            luaC_objbarrier(L, target, luaDispatcher);
        }

        Shared::ExecutorClosures.insert(target);

        // Transparency for source/name probes (Myriad hookmetamethod __index):
        // hooked Lua slots swap proto to the shared dispatcher (source
        // "=hookfunction"), which reads as "replaced" via debug.info(fn,"s").
        // Closures::debug_info_wrapper resolves "s"/"n"/etc. against the saved
        // original while keeping "f" on the live slot. It exists but is never
        // installed — install it here. Stack-balanced, no locks, call path /
        // restore-via-rehook / old-still-callable unchanged.
        Closures::ensure_debug_info_hook(L);

        // No lua_rawset and no readonly toggle: the table itself is never
        // written (only the Closure body is mutated), so sibling entries in
        // the shared game metatable and its readonly flag are untouched.
        lua_getref(L, record.OriginalRef);
        return 1;
    }

    void register_lib(lua_State* L)
    {
        Environment->make_global(L, ("hookmetamethod"), hookmetamethod, Metatable::funcnameinc++);
        Environment->make_global(L, ("getnamecallmethod"), getnamecallmethod, Metatable::funcnameinc++);
        Environment->make_global(L, ("setreadonly"), setreadonly, Metatable::funcnameinc++);
        Environment->make_global(L, ("isreadonly"), isreadonly, Metatable::funcnameinc++);
        Environment->make_global(L, ("getrawmetatable"), getrawmetatable, Metatable::funcnameinc++);
        Environment->make_global(L, ("setrawmetatable"), setrawmetatable, Metatable::funcnameinc++);
        Environment->make_global(L, ("makereadonly"), makereadonly, Metatable::funcnameinc++);
        Environment->make_global(L, ("makewriteable"), makewriteable, Metatable::funcnameinc++);
    }
}

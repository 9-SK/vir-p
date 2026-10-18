#include "Closures.hpp"

#include <RBX/environment/env.hpp>

namespace Closures
{
    std::unordered_set<Closure*> HiddenClosures;
    std::vector<HiddenLevel> HiddenLevels;



    void register_lib(lua_State* L)
    {
        // Idempotent re-init (stale-reference fix): if this state's
        // `checkcaller` global is already our hardened closure, every
        // global below is already installed for this state. Re-creating
        // them would displace the live Closure* objects — any Lua-held
        // reference captured beforehand (local, upvalue, hook record)
        // goes stale and use-after-frees once the GC collects the
        // displaced generation — and ResetLibraryState would wipe live
        // HookRecords while hooked dispatchers still resolve through
        // them. Fresh states (nil or a foreign global) fall through to
        // full registration. Stack-balanced, no locks, no Lua API locks.
        lua_getglobal(L, "checkcaller");
        bool alreadyOurs = false;
        if (lua_isfunction(L, -1))
        {
            Closure* current = clvalue(index2addr(L, -1));
            alreadyOurs = (current && current->isC && current->c.f == Closures::checkcaller);
        }
        lua_pop(L, 1);
        if (alreadyOurs)
            return;

        funcnameinc = 0;
        ResetLibraryState();


        lua_pushcclosurek(L, Closures::HookLuaDispatcher, LuaHookDispatcherName, 0, DispatchContinuation);
        lua_setglobal(L, LuaHookDispatcherName);

        Environment->make_global(L, "loadstring", Closures::loadstring, funcnameinc++);
       Environment->make_global(L, "hookfunction", Closures::hookfunction, funcnameinc++);
       Environment->make_global(L, "hookfunc", Closures::hookfunction, funcnameinc++);
       Environment->make_global(L, "replaceclosure", Closures::hookfunction, funcnameinc++);
      Environment->make_global(L, "newcclosure", Closures::newcclosure, funcnameinc++);
        Environment->make_global(L, "isnewcclosure", Closures::isnewcclosure, funcnameinc++);
      Environment->make_global(L, "restorefunction", Closures::restorefunction, funcnameinc++);
        Environment->make_global(L, "restorefunc", Closures::restorefunction, funcnameinc++);
        Environment->make_global(L, "clonefunction", Closures::clonefunction, funcnameinc++);
        Environment->make_global(L, "cloneref", Closures::cloneref, funcnameinc++);
       Environment->make_global(L, "iscclosure", Closures::iscclosure, funcnameinc++);
       Environment->make_global(L, "islclosure", Closures::islclosure, funcnameinc++);
       Environment->make_global(L, "isourclosure", Closures::is_executor_closure, funcnameinc++);
       Environment->make_global(L, "checkclosure", Closures::is_executor_closure, funcnameinc++);
       Environment->make_global(L, "isexecutorclosure", Closures::is_executor_closure, funcnameinc++);
        Environment->make_global(L, "isexecutorfunction", Closures::is_executor_closure, funcnameinc++);
        // checkcaller supersedes Script::checkcaller: Closures::register_lib
        // runs after Script::register_lib in both init paths
        // (initalizeEnvironment, actor init), so this global wins. The
        // hardened version (see Closures::checkcaller) keeps the exact
        // boolean contract but never dereferences a stale function_array
        // entry, which is what killed the client inside Myriad's checkcaller
        // test. Both pins below are intentional one-slot registry leaks:
        // function_array/ExecutorClosures keep raw pointers to these
        // closures, so each displaced generation stays alive instead of
        // dangling once the GC runs.
        lua_getglobal(L, "checkcaller");
        if (lua_isfunction(L, -1))
            (void)lua_ref(L, -1); // pin Script's displaced closure
        lua_pop(L, 1);
        Environment->make_global(L, "checkcaller", Closures::checkcaller, funcnameinc++);
        lua_getglobal(L, "checkcaller");
        (void)lua_ref(L, -1); // pin ours: Script's next re-init displaces it
        lua_pop(L, 1);
     Environment->make_global(L, "gettenv", Closures::gettenv, funcnameinc++);
        Environment->make_global(L, "isfunctionhooked", Closures::isfunctionhooked, funcnameinc++);
        Environment->make_global(L, "compareclosures", Closures::compareclosures, funcnameinc++);
      Environment->make_global(L, "hooksignal", Closures::hooksignal, funcnameinc++);
        Environment->make_global(L, "restoresignal", Closures::restoresignal, funcnameinc++);
    //    Environment->make_global(L, "setstackhidden", Closures::setstackhidden, funcnameinc++);

        // detection fixes: keep debug.info names stable for hooked closures
        // and intercept the META_SECURITY probe on BindableFunction:Invoke
   
    }
}

#include "Debug.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Utils.hpp>
#include <RBX/environment/libs/script/script.hpp>
#include <RBX/execution/execution.hpp>
#include <RBX/environment/libs/cache/cache.hpp>
#include <RBX/environment/libs/closures/closures.hpp>


namespace Debug
{
    // â”€â”€â”€ Internal Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    static Closure* header_get_function(lua_State* L, bool allowCclosure = false, bool popcl = true)
    {
        luaL_checkany(L, 1);

        if (!(lua_isfunction(L, 1) || lua_isnumber(L, 1)))
        {
            luaL_argerror(L, 1, ("function or number"));
        }

        int level = 0;
        if (lua_isnumber(L, 1))
        {
            level = lua_tointeger(L, 1);

            if (level <= 0)
            {
                luaL_argerror(L, 1, ("level out of range"));
            }
        }
        else if (lua_isfunction(L, 1))
        {
            level = -lua_gettop(L);
        }

        lua_Debug ar;
        if (!lua_getinfo(L, level, ("f"), &ar))
        {
            luaL_argerror(L, 1, ("invalid level"));
        }

        if (!lua_isfunction(L, -1))
            luaL_argerror(L, 1, ("level does not point to a function"));
        // (A second identical iscfunction check lived here; it could never
        // fire — same slot, no stack ops between the two tests.)
        if (lua_iscfunction(L, -1) && !allowCclosure)
            luaL_argerror(L, 1, ("level points to c function"));

        auto function = clvalue(luaA_toobject(L, -1));
        if (popcl)
            lua_pop(L, 1);

        return function;
    }

    static const char* aux_upvalue_2(Closure* f, int n, TValue** val)
    {
        if (f->isC)
        {
            if (!(1 <= n && n <= f->nupvalues))
                return NULL;
            *val = &f->c.upvals[n - 1];
            return ("");
        }
        else
        {
            Proto* p = f->l.p;
            if (!(1 <= n && n <= p->nups))
                return NULL;
            TValue* r = &f->l.uprefs[n - 1];
            *val = ttisupval(r) ? upvalue(r)->v : r;
            if (!(1 <= n && n <= p->sizeupvalues))
                return ("");
            return getstr(p->upvalues[n - 1]);
        }
    }

    // depth caps C-stack recursion at 64 nested protos: legit closures nest
    // far shallower, while an obfuscated bundle's pathological nesting (or a
    // corrupt sizep chain) would otherwise exhaust the native stack with no
    // log and no dump. Failing closed as a Lua error keeps it pcall-able.
    static Proto* clone_proto(lua_State* L, Proto* proto, int depth = 0)
    {
        if (depth > 64)
            luaL_error(L, ("debug.getproto: proto nesting too deep"));
        if (!proto)
            luaL_error(L, ("debug.getproto: null proto"));
        if (proto->sizep < 0 || proto->sizep > 100000)
            luaL_error(L, ("debug.getproto: corrupt proto"));

        Proto* clone = luaF_newproto(L);

        clone->sizek = proto->sizek;
        clone->k = luaM_newarray(L, proto->sizek, TValue, proto->memcat);
        for (int i = 0; i < proto->sizek; ++i)
            setobj2n(L, &clone->k[i], &proto->k[i]);

        clone->sizelineinfo = proto->sizelineinfo;
        if (proto->sizelineinfo > 0)
        {
            clone->lineinfo = luaM_newarray(L, proto->sizelineinfo, uint8_t, proto->memcat);
            for (int i = 0; i < proto->sizelineinfo; ++i)
                clone->lineinfo[i] = proto->lineinfo[i];
        }
        else
        {
            clone->lineinfo = NULL;
        }

        clone->locvars = luaM_newarray(L, proto->sizelocvars, LocVar, proto->memcat);
        for (int i = 0; i < proto->sizelocvars; ++i)
        {
            const auto varname = getstr(proto->locvars[i].varname);
            const auto varname_size = strlen(varname);
            clone->locvars[i].varname = luaS_newlstr(L, varname, varname_size);
            clone->locvars[i].endpc = proto->locvars[i].endpc;
            clone->locvars[i].reg = proto->locvars[i].reg;
            clone->locvars[i].startpc = proto->locvars[i].startpc;
        }

        clone->nups = proto->nups;
        clone->sizeupvalues = proto->sizeupvalues;
        clone->sizelineinfo = proto->sizelineinfo;
        clone->linegaplog2 = proto->linegaplog2;
        clone->sizelocvars = proto->sizelocvars;
        clone->linedefined = proto->linedefined;

        if (proto->debugname)
        {
            const auto debugname = getstr(proto->debugname);
            const auto debugname_size = strlen(debugname);
            clone->debugname = luaS_newlstr(L, debugname, debugname_size);
        }

        if (proto->source)
        {
            const auto source = getstr(proto->source);
            const auto source_size = strlen(source);
            clone->source = luaS_newlstr(L, source, source_size);
        }

        clone->numparams = proto->numparams;
        clone->is_vararg = proto->is_vararg;
        clone->maxstacksize = proto->maxstacksize;
        clone->bytecodeid = proto->bytecodeid;

        std::string bytecode = Execution::CompileScript(std::string(("return")));
        luau_load(L, ("@cloneproto"), bytecode.c_str(), bytecode.size(), 0);

        Closure* cl = clvalue(luaA_toobject(L, -1));

        clone->sizecode = cl->l.p->sizecode;
        clone->code = luaM_newarray(L, clone->sizecode, Instruction, proto->memcat);
        for (int i = 0; i < cl->l.p->sizecode; i++)
        {
            clone->code[i] = cl->l.p->code[i];
        }
        lua_pop(L, 1);

        clone->codeentry = clone->code;
        clone->debuginsn = 0;

        clone->sizep = proto->sizep;
        clone->p = luaM_newarray(L, proto->sizep, Proto*, proto->memcat);
        for (int i = 0; i < proto->sizep; ++i)
        {
            clone->p[i] = clone_proto(L, proto->p[i], depth + 1);
        }

        return clone;
    }

    // â”€â”€â”€ Lua Function Implementations â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    int debug_getupvalues(lua_State* L)
    {
        luaL_checkany(L, 1);

        header_get_function(L, false, false);
        const int functionIndex = lua_gettop(L);

        lua_newtable(L);
        const int resultTable = lua_gettop(L);

        for (int index = 1;; ++index)
        {
            const char* name = lua_getupvalue(L, functionIndex, index);

            if (!name)
                break;

            lua_rawseti(L, resultTable, index);
        }

        return 1;
    }

    int debug_getupvalue(lua_State* L)
    {
        luaL_checkany(L, 1);

        const int index = static_cast<int>(luaL_checkinteger(L, 2));

        if (index < 1)
            luaL_argerror(L, 2, ("upvalue index must be positive"));

        header_get_function(L, false, false);
        const int functionIndex = lua_gettop(L);

        const char* name = lua_getupvalue(L, functionIndex, index);

        if (!name)
            luaL_argerror(L, 2, ("upvalue index out of bounds"));

        return 1;
    }

    int debug_setupvalue(lua_State* L)
    {
        const auto Func = header_get_function(L, false, false);
        const auto idx = lua_tointeger(L, 2);

        if (Func->nupvalues <= 0)
        {
            luaL_argerror(L, 1, ("function has no upvalues"));
        }

        if (!(idx >= 1 && idx <= Func->nupvalues))
        {
            luaL_argerror(L, 2, ("index out of range"));
        }

        lua_pushvalue(L, 3);
        lua_setupvalue(L, -2, idx);
        return 1;
    }

    int debug_getconstants(lua_State* L)
    {
        const auto Func = header_get_function(L);
        const auto p = (Proto*)Func->l.p;

        lua_createtable(L, p->sizek, 0);

        for (int i = 0; i < p->sizek; i++)
        {
            TValue k = p->k[i];

            if (k.tt == LUA_TNIL || k.tt == LUA_TFUNCTION || k.tt == LUA_TTABLE)
            {
                lua_pushnil(L);
            }
            else
            {
                luaC_threadbarrier(L);
                luaA_pushvalue(L, &k);
            }
            lua_rawseti(L, -2, i + 1);
        }

        return 1;
    }

    int debug_getconstant(lua_State* L)
    {
        const auto Func = header_get_function(L);
        const auto idx = luaL_checkinteger(L, 2);
        const auto p = (Proto*)Func->l.p;

        const auto level = -lua_gettop(L);

        if (p->sizek <= 0)
        {
            luaL_argerror(L, 1, ("function has no constants"));
        }

        lua_Debug ar;
        if (!lua_getinfo(L, level, ("f"), &ar))
            luaL_error(L, ("invalid level"));

        if (!(idx >= 1 && idx <= p->sizek))
        {
            luaL_argerror(L, 2, ("index out of range"));
        }

        const auto k = &p->k[idx - 1];

        if (k->tt == LUA_TNIL || k->tt == LUA_TTABLE || k->tt == LUA_TFUNCTION)
        {
            lua_pushnil(L);
            return 1;
        }

        luaA_pushvalue(L, k);
        return 1;
    }

    int debug_setconstant(lua_State* L)
    {
        luaL_checkany(L, 3);

        const auto Func = header_get_function(L);
        const auto idx = luaL_checkinteger(L, 2);
        const auto p = (Proto*)Func->l.p;

        if (p->sizek <= 0)
        {
            luaL_argerror(L, 1, ("function has no constants"));
        }

        if (!(idx >= 1 && idx <= p->sizek))
        {
            luaL_argerror(L, 2, ("index out of range"));
        }

        TValue* k = &p->k[idx - 1];

        if (k->tt == LUA_TFUNCTION || k->tt == LUA_TTABLE)
        {
            return 0;
        }
        else
        {
            if (k->tt == luaA_toobject(L, 3)->tt)
            {
                setobj2s(L, k, luaA_toobject(L, 3));
                luaC_barrier(L, p, k);
            }
        }

        return 0;
    }

    int debug_getprotos(lua_State* L)
    {
        Closure* Func = header_get_function(L);
        bool active = !lua_isnoneornil(L, 2) ? (lua_toboolean(L, 2) != 0) : false;
        Proto* p = (Proto*)Func->l.p;
        lua_createtable(L, p->sizep, 0);
        if (!active) {
            for (int i = 0; i < p->sizep; i++) {
                lua_pushlightuserdata(L, (void*)p->p[i]);
                lua_rawseti(L, -2, i + 1);
            }
        }
        else {
            for (int i = 0; i < p->sizep; i++) {
                Proto* proto = p->p[i];
                Closure* pcl = luaF_newLclosure(L, proto->nups, Func->env, proto);
                luaC_threadbarrier(L);
                setclvalue(L, L->top, pcl); L->top++;
                lua_rawseti(L, -2, i + 1);
            }
        }
        return 1;
    }

    int debug_getproto(lua_State* L)
    {
        luaL_checkany(L, 1);

        const int index = static_cast<int>(luaL_checkinteger(L, 2));
        const bool active = luaL_optboolean(L, 3, false);

        Closure* parentClosure = nullptr;

        if (lua_isfunction(L, 1))
        {
            parentClosure = clvalue(luaA_toobject(L, 1));

            if (!parentClosure || parentClosure->isC)
                luaL_argerror(L, 1, ("Lua function expected"));
        }
        else if (lua_isnumber(L, 1))
        {
            const int level = static_cast<int>(luaL_checkinteger(L, 1));

            if (level <= 0)
                luaL_argerror(L, 1, ("stack level must be positive"));

            lua_Debug ar{};

            if (!lua_getinfo(L, level, ("f"), &ar))
                luaL_argerror(L, 1, ("stack level out of bounds"));

            if (!lua_isfunction(L, -1))
            {
                lua_pop(L, 1);
                luaL_argerror(L, 1, ("stack level does not contain a function"));
            }

            if (lua_iscfunction(L, -1))
            {
                lua_pop(L, 1);
                luaL_argerror(L, 1, ("Lua function expected"));
            }

            parentClosure = clvalue(luaA_toobject(L, -1));
            lua_pop(L, 1);
        }
        else
        {
            luaL_argerror(L, 1, ("function or stack level expected"));
        }

        if (!parentClosure || parentClosure->isC || !parentClosure->l.p)
            luaL_argerror(L, 1, ("invalid Lua function"));

        Proto* parentProto = parentClosure->l.p;

        if (index < 1 || index > parentProto->sizep || !parentProto->p)
            luaL_argerror(L, 2, ("index out of bounds"));

        Proto* wantedProto = parentProto->p[index - 1];

        if (!wantedProto)
            luaL_argerror(L, 2, ("proto is unavailable"));

        if (!active)
        {
            Proto* clonedProto = clone_proto(L, wantedProto);

            if (!clonedProto)
                luaL_error(L, ("failed to clone proto"));

            luaD_checkstack(L, 1);

            Closure* result = luaF_newLclosure(
                L,
                parentClosure->nupvalues,
                L->gt,
                clonedProto
            );

            if (!result)
                luaL_error(L, ("failed to create proto closure"));

            setclvalue(L, L->top, result);
            incr_top(L);
            luaC_threadbarrier(L);

            return 1;
        }

        lua_newtable(L);
        const int outputTable = lua_gettop(L);
        int outputCount = 0;

        lua_pushcfunction(L, Script::getgc, ("debug.getproto.internal_getgc"));
        lua_pushboolean(L, true);

        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            const char* error = lua_tostring(L, -1);
            luaL_error(L, ("internal getgc failed: %s"), error ? error : ("unknown error"));
        }

        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            luaL_error(L, ("internal getgc did not return a table"));
        }

        const int gcTable = lua_gettop(L);
        const int objectCount = static_cast<int>(lua_objlen(L, gcTable));

        for (int i = 1; i <= objectCount; ++i)
        {
            lua_rawgeti(L, gcTable, i);

            if (lua_isfunction(L, -1) && !lua_iscfunction(L, -1))
            {
                Closure* candidate = clvalue(luaA_toobject(L, -1));

                if (candidate && !candidate->isC && candidate->l.p == wantedProto)
                {
                    lua_pushvalue(L, -1);
                    lua_rawseti(L, outputTable, ++outputCount);
                }
            }

            lua_pop(L, 1);
        }

        lua_pop(L, 1);

        return 1;
    }
    bool is_internal_stack_frame(CallInfo* ci)
    {
        if (!ci || !ci->func || !ttisfunction(ci->func))
            return true;

        Closure* closure = clvalue(ci->func);
        if (!closure)
            return true;

        if (closure->isC)
        {
            const char* debugName = closure->c.debugname ? getstr(closure->c.debugname) : nullptr;
            return debugName &&
                (strcmp(debugName, ("hookfunction")) == 0 ||
                    strcmp(debugName, ("newcclosure")) == 0 ||
                    strcmp(debugName, ("__lunarium_hookfunction_dispatch")) == 0);
        }

        Proto* proto = closure->l.p;
        const char* source = proto && proto->source ? getstr(proto->source) : nullptr;
        return source && strcmp(source, ("=hookfunction")) == 0;
    }

    CallInfo* resolve_visible_stack_level(lua_State* target, int level)
    {
        if (!target || level <= 0)
            return nullptr;

        int visibleLevel = 0;
        for (CallInfo* ci = target->ci; ci > target->base_ci;)
        {
            --ci;

            if (is_internal_stack_frame(ci))
                continue;

            if (++visibleLevel == level)
                return ci;
        }

        return nullptr;
    }

    int stack_frame_size(lua_State* target, CallInfo* ci, Closure* closure)
    {
        if (!target || !ci || !ci->base)
            return 0;

        int size = 0;
        if (closure && !closure->isC && closure->l.p)
            size = closure->l.p->maxstacksize;

        if (size <= 0 && ci->top && ci->top > ci->base)
            size = static_cast<int>(ci->top - ci->base);

        if (size <= 0 && target->top > ci->base)
            size = static_cast<int>(target->top - ci->base);

        const int stackAvailable = static_cast<int>((target->stack + target->stacksize) - ci->base);
        if (stackAvailable < size)
            size = stackAvailable;

        return size > 0 ? size : 0;
    }

    bool resolve_stack_target(lua_State* L, int argPos, int& out_level, CallInfo*& out_ci, Closure*& out_cl)
    {
        out_level = 0;
        out_ci = nullptr;
        out_cl = nullptr;

        if (lua_isnumber(L, argPos))
        {
            out_level = lua_tointeger(L, argPos);
            out_ci = resolve_visible_stack_level(L, out_level);
            if (!out_ci)
            {
                luaL_argerror(L, argPos, ("level out of range"));
                return false;
            }
        }
        else if (lua_isfunction(L, argPos))
        {
            // Loop invariants: the arg slot is untouched by the pointer walk
            // below (no Lua calls), so resolve once, not per frame.
            Closure* const wanted = clvalue(luaA_toobject(L, argPos));
            const int stack_size = static_cast<int>(L->ci - L->base_ci);
            for (int i = 1; i < stack_size; ++i)
            {
                CallInfo* ci = L->ci - i;
                if (is_internal_stack_frame(ci))
                    continue;

                if (!ci->func || !ttisfunction(ci->func))
                    continue;

                Closure* active = clvalue(ci->func);
                if (active == wanted)
                {
                    out_level = i;
                    out_ci = ci;
                    break;
                }
            }

            if (out_level == 0)
            {
                luaL_argerror(L, argPos, ("function not found in call stack"));
                return false;
            }
        }
        else
        {
            luaL_argerror(L, argPos, ("function or number expected"));
            return false;
        }

        if (!out_ci)
        {
            out_ci = resolve_visible_stack_level(L, out_level);
            if (!out_ci)
            {
                luaL_argerror(L, argPos, ("invalid level"));
                return false;
            }
        }

        if (!out_ci->func || !ttisfunction(out_ci->func))
        {
            luaL_argerror(L, argPos, ("not a function"));
            return false;
        }

        out_cl = clvalue(out_ci->func);
        if (!out_cl)
        {
            luaL_argerror(L, argPos, ("not a function"));
            return false;
        }

        if (out_cl->isC)
        {
            luaL_argerror(L, argPos, ("luau function expected"));
            return false;
        }

        if (out_ci < L->base_ci || out_ci > L->ci)
        {
            luaL_error(L, ("invalid callinfo"));
            return false;
        }

        if (!out_ci->base || !out_ci->top)
        {
            luaL_error(L, ("callinfo base/top is null"));
            return false;
        }

        return true;
    }

    int debug_getstack(lua_State* L)
    {
        int arg = 0;
        lua_State* target = L;

        if (lua_isthread(L, 1))
        {
            target = lua_tothread(L, 1);
            if (!target)
                luaL_argerror(L, 1, ("invalid thread"));

            arg = 1;
        }

        const int level = luaL_checkinteger(L, arg + 1);
        CallInfo* ci = resolve_visible_stack_level(target, level);
        if (!ci)
            luaL_argerror(L, arg + 1, ("level out of range"));

        if (ci < target->base_ci || ci > target->ci)
            luaL_error(L, ("invalid callinfo"));

        if (!ci->func || ci->func->tt != LUA_TFUNCTION)
            luaL_error(L, ("invalid stack frame"));

        Closure* closure = clvalue(ci->func);
        if (closure && closure->isC)
            luaL_error(L, ("cannot get stack of a C function"));

        if (!ci->base)
            luaL_error(L, ("callinfo base is null"));

        const int frame_size = stack_frame_size(target, ci, closure);

        if (lua_isnumber(L, arg + 2))
        {
            const int idx = lua_tointeger(L, arg + 2) - 1;

            if (idx < 0 || idx >= frame_size)
            {
                lua_pushnil(L);
                return 1;
            }

            StkId slot = ci->base + idx;
            if (slot < target->stack || slot >= target->stack + target->stacksize)
            {
                lua_pushnil(L);
                return 1;
            }

            luaC_threadbarrier(L);
            luaA_pushvalue(L, slot);
        }
        else
        {
            lua_newtable(L);

            for (int i = 0; i < frame_size; ++i)
            {
                StkId slot = ci->base + i;
                if (slot < target->stack || slot >= target->stack + target->stacksize)
                    break;

                luaC_threadbarrier(L);
                luaA_pushvalue(L, slot);
                lua_rawseti(L, -2, i + 1);
            }
        }

        return 1;
    }

    int debug_setstack(lua_State* L)
    {
        int level = 0;
        CallInfo* ci = nullptr;
        Closure* closure = nullptr;
        if (!resolve_stack_target(L, 1, level, ci, closure))
            return 0;

        const auto idx = luaL_checkinteger(L, 2) - 1;
        const int frame_size = stack_frame_size(L, ci, closure);
        if (idx >= frame_size || idx < 0)
        {
            luaL_argerror(L, 2, ("index out of range"));
        }

        const TValue* new_value = luaA_toobject(L, 3);
        if (!new_value)
        {
            luaL_argerror(L, 3, ("invalid value"));
        }

        setobj2s(L, ci->base + idx, new_value);
        return 0;
    }

    int debug_getinfo(lua_State* L)
    {
        luaL_checkany(L, 1);

        if (!(lua_isfunction(L, 1) || lua_isnumber(L, 1)))
        {
            luaL_argerror(L, 1, ("function or number"));
        }

        int level;
        if (lua_isnumber(L, 1))
        {
            level = lua_tointeger(L, 1);
            // Negative levels address raw stack slots (lua_getinfo level<0
            // path). Out-of-range doubles (NaN/inf/huge) convert to INT_MIN
            // via luai_num2int, whose negation wraps so the VM's own
            // `-level > top-base` guard is skipped and it reads a wild
            // pointer (~2GB off-stack) -> instant silent AV. Stock db_info
            // rejects negatives; do the same. Level 0 (self frame) stays.
            if (level < 0)
                luaL_argerror(L, 1, ("level can't be negative"));
        }
        else
        {
            level = -lua_gettop(L);
        }

        const char* desc = ("sluanf");

        lua_Debug ar;
        if (!lua_getinfo(L, level, desc, &ar))
        {
            luaL_argerror(L, 1, ("invalid level"));
        }

        // Hoist the per-field option scans: desc never changes in this call.
        const bool want_s = std::strchr(desc, 's') != nullptr;
        const bool want_l = std::strchr(desc, 'l') != nullptr;
        const bool want_u = std::strchr(desc, 'u') != nullptr;
        const bool want_a = std::strchr(desc, 'a') != nullptr;
        const bool want_n = std::strchr(desc, 'n') != nullptr;
        const bool want_f = std::strchr(desc, 'f') != nullptr;

        if (!lua_isfunction(L, -1))
        {
            luaL_argerror(L, 1, ("level does not point to a function."));
        }

        lua_newtable(L);
        {
            if (want_s)
            {
                lua_pushstring(L, ar.source);
                lua_setfield(L, -2, ("source"));

                lua_pushstring(L, ar.short_src);
                lua_setfield(L, -2, ("short_src"));

                lua_pushstring(L, ar.what);
                lua_setfield(L, -2, ("what"));

                lua_pushinteger(L, ar.linedefined);
                lua_setfield(L, -2, ("linedefined"));
            }

            if (want_l)
            {
                lua_pushinteger(L, ar.currentline);
                lua_setfield(L, -2, ("currentline"));
            }

            if (want_u)
            {
                lua_pushinteger(L, ar.nupvals);
                lua_setfield(L, -2, ("nups"));
            }

            if (want_a)
            {
                lua_pushinteger(L, ar.isvararg);
                lua_setfield(L, -2, ("is_vararg"));

                lua_pushinteger(L, ar.nparams);
                lua_setfield(L, -2, ("numparams"));
            }

            if (want_n)
            {
                lua_pushstring(L, ar.name);
                lua_setfield(L, -2, ("name"));
            }

            if (want_f)
            {
                lua_pushvalue(L, -2);
                lua_remove(L, -3);
                lua_setfield(L, -2, ("func"));
            }
        }

        return 1;
    }

    int debug_getregistry(lua_State* L)
    {
        lua_pushvalue(L, LUA_REGISTRYINDEX);
        return 1;
    }

    // debug.setinfo(level_or_func, what_table)
    // Modifies function properties. Supported keys in what_table:
    //   source  - changes the source string (proto->source)
    //   linedef - changes linedefined
    //   name    - changes debugname (proto->debugname)
    int debug_setinfo(lua_State* L)
    {
        luaL_checkany(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);

        // Resolve the function
        int level;
        if (lua_isnumber(L, 1))
        {
            level = lua_tointeger(L, 1);
        }
        else if (lua_isfunction(L, 1))
        {
            level = -lua_gettop(L);
        }
        else
        {
            luaL_argerror(L, 1, ("function or number"));
            return 0;
        }

        lua_Debug ar;
        if (!lua_getinfo(L, level, ("f"), &ar))
        {
            luaL_argerror(L, 1, ("invalid level"));
            return 0;
        }

        if (!lua_isfunction(L, -1))
        {
            luaL_argerror(L, 1, ("level does not point to a function"));
            return 0;
        }

        if (lua_iscfunction(L, -1))
        {
            luaL_argerror(L, 1, ("cannot modify C functions"));
            return 0;
        }

        Closure* cl = clvalue(luaA_toobject(L, -1));
        if (!cl || !cl->l.p)
        {
            luaL_argerror(L, 1, ("invalid closure"));
            return 0;
        }

        Proto* p = cl->l.p;
        int modified = 0;

        // source
        lua_getfield(L, 2, ("source"));
        if (!lua_isnil(L, -1))
        {
            size_t len = 0;
            const char* src = luaL_checklstring(L, -1, &len);
            if (src && len > 0)
            {
                p->source = luaS_newlstr(L, src, len);
                if (p->source)
                    luaC_objbarrier(L, p, p->source);
            }
            modified = 1;
        }
        lua_pop(L, 1);

        // linedefined
        lua_getfield(L, 2, ("linedefined"));
        if (!lua_isnil(L, -1))
        {
            p->linedefined = (int)luaL_checkinteger(L, -1);
            modified = 1;
        }
        lua_pop(L, 1);

        // name -> debugname
        lua_getfield(L, 2, ("name"));
        if (!lua_isnil(L, -1))
        {
            size_t len = 0;
            const char* name = luaL_checklstring(L, -1, &len);
            if (name && len > 0)
            {
                p->debugname = luaS_newlstr(L, name, len);
                if (p->debugname)
                    luaC_objbarrier(L, p, p->debugname);
            }
            modified = 1;
        }
        lua_pop(L, 1);

        lua_pushboolean(L, modified);
        return 1;
    }

    // debug.validlevel(level) -> boolean
    int debug_validlevel(lua_State* L)
    {
        int level = (int)luaL_checkinteger(L, 1);

        // SehGuard wrapper adds 1 stack frame â€” adjust
        level += 1;

        lua_Debug ar;
        bool valid = lua_getinfo(L, level, ("f"), &ar) != 0;
        if (valid && lua_isfunction(L, -1))
        {
            lua_pushboolean(L, 1);
        }
        else
        {
            lua_pushboolean(L, 0);
        }
        return 1;
    }

    // â”€â”€â”€ Registration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void ResetLibraryState()
    {
        // Debug has no mutable global state to reset
    }

    void register_lib(lua_State* L)
    {
        funcnameinc = 0;

        debug_library::initialize(L);
    }
}

void debug_library::initialize(lua_State* L)
{
    lua_getglobal(L, ("debug"));
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_setreadonly(L, -1, false);

   Environment->make_table_function(L, "getproto", Debug::debug_getproto, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getprotos"), Debug::debug_getprotos, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getstack"), Debug::debug_getstack, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getinfo"), Debug::debug_getinfo, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getupvalue"), Debug::debug_getupvalue, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getupvalues"), Debug::debug_getupvalues, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getconstant"), Debug::debug_getconstant, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getconstants"), Debug::debug_getconstants, Debug::funcnameinc++);
    Environment->make_table_function(L, ("setconstant"), Debug::debug_setconstant, Debug::funcnameinc++);
    Environment->make_table_function(L, ("setstack"), Debug::debug_setstack, Debug::funcnameinc++);
    Environment->make_table_function(L, ("setupvalue"), Debug::debug_setupvalue, Debug::funcnameinc++);
    Environment->make_table_function(L, ("getregistry"), Debug::debug_getregistry, Debug::funcnameinc++);
   // Environment->make_table_function(L, ("setinfo"), Debug::debug_setinfo, Debug::funcnameinc++);
  //  Environment->make_table_function(L, ("validlevel"), Debug::debug_validlevel, Debug::funcnameinc++);
    Environment->make_global(L, ("setstack"), Debug::debug_setstack, Debug::funcnameinc++);
    Environment->make_global(L, ("getstack"), Debug::debug_getstack, Debug::funcnameinc++);
    Environment->make_global(L, ("getconstant"), Debug::debug_getconstant, Debug::funcnameinc++);
    Environment->make_global(L, ("setconstant"), Debug::debug_setconstant, Debug::funcnameinc++);
    Environment->make_global(L, ("getconstants"), Debug::debug_getconstants, Debug::funcnameinc++);
    Environment->make_global(L, ("getproto"), Debug::debug_getproto, Debug::funcnameinc++);
    Environment->make_global(L, ("getprotos"), Debug::debug_getprotos, Debug::funcnameinc++);
    Environment->make_global(L, ("getupvalue"), Debug::debug_getupvalue, Debug::funcnameinc++);
    Environment->make_global(L, ("setupvalue"), Debug::debug_setupvalue, Debug::funcnameinc++);
    Environment->make_global(L, ("getupvalues"), Debug::debug_getupvalues, Debug::funcnameinc++);

    lua_setreadonly(L, -1, true);
    lua_setglobal(L, ("debug"));
}

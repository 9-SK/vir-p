#include "../environment.hpp"

#include "lstate.h"
#include "lualib.h"
#include "lapi.h"
#include "ldo.h"
#include "lgc.h"
#include "lfunc.h"
#include "lmem.h"
#include "lstring.h"
#include "ldebug.h"
#include <luacode.h>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Shared/Shared.hpp>
#include <string>
#include <cstring>
#include <unordered_set>
#include <unordered_map>

#ifndef xorstr_
#define xorstr_(s) s
#endif

inline const char* aux_upvalue_2(Closure* f, int n, TValue** val)
{
    if (f->isC)
    {
        if (!(1 <= n && n <= f->nupvalues))
            return NULL;
        *val = &f->c.upvals[n - 1];
        return "";
    }
    else
    {
        Proto* p = f->l.p;
        if (!(1 <= n && n <= p->nups))
            return NULL;
        TValue* r = &f->l.uprefs[n - 1];
        *val = ttisupval(r) ? upvalue(r)->v : r;
        if (!(1 <= n && n <= p->sizeupvalues))
            return "";
        return getstr(p->upvalues[n - 1]);
    }
}

struct VMDebugState {
    std::unordered_map<void*, int>         hidden_closure_refs;
    std::unordered_map<void*, int>         fake_currentlines;
    std::unordered_map<void*, std::string> fake_names;
    std::unordered_map<void*, std::string> fake_sources;
    int roblox_debug_info_ref = LUA_NOREF;

    void clear(lua_State* L) {
        for (auto& [k, ref] : hidden_closure_refs)
            lua_unref(L, ref);
        hidden_closure_refs.clear();
        fake_currentlines.clear();
        fake_names.clear();
        fake_sources.clear();
        if (roblox_debug_info_ref != LUA_NOREF) {
            lua_unref(L, roblox_debug_info_ref);
            roblox_debug_info_ref = LUA_NOREF;
        }
    }
};

static std::unordered_map<global_State*, VMDebugState> g_vm_states;

static VMDebugState& get_state(lua_State* L) {
    return g_vm_states[L->global];
}

static inline void* closure_key(Closure* cl) {
    return (!cl->isC && cl->l.p) ? static_cast<void*>(cl->l.p)
        : static_cast<void*>(cl);
}

static const char* debugname_from_closure(Closure* cl) {
    if (!cl || cl->isC || !cl->l.p) return nullptr;
    TString* dn = cl->l.p->debugname.Get();
    if (!dn) return nullptr;
    const char* s = getstr(dn);
    return (s && s[0] != '@' && s[0] != '=' && s[0] != '\0') ? s : nullptr;
}

static const char* best_function_name(const lua_Debug& ar, Closure* cl) {
    if (ar.name && ar.name[0]) return ar.name;
    return debugname_from_closure(cl);
}

struct lua_Page
{
    lua_Page* listprev;
    lua_Page* listnext;
    lua_Page* prev;
    lua_Page* next;
    int pageSize;
    int blockSize;
    void* freeList;
    int freeNext;
    int busyBlocks;
    char padding[sizeof(void*) == 8 ? 8 : 12];
    char data[1];
};

Closure* header_get_function(lua_State* L, bool allowCclosure = false, bool popcl = true)
{
    luaL_checkany(L, 1);
    int level = 0;
    if (lua_isnumber(L, 1))
    {
        level = luaL_checkinteger(L, 1);
        int stacksize = (int)(L->ci - L->base_ci);
        if (level <= 0 || level >= stacksize)
            luaL_argerror(L, 1, xorstr_("level out of range"));
    }
    else if (lua_isfunction(L, 1))
    {
        lua_pushvalue(L, 1);
        goto gotfunc;
    }
    else
        luaL_argerror(L, 1, xorstr_("function or number expected"));
    {
        lua_Debug ar;
        if (!lua_getinfo(L, level, xorstr_("f"), &ar))
            luaL_error(L, xorstr_("invalid level"));
    }
gotfunc:
    if (!lua_isfunction(L, -1))
        luaL_error(L, xorstr_("stack corruption (not function)"));
    const TValue* o = luaA_toobject(L, -1);
    if (!o || o->tt != LUA_TFUNCTION)
        luaL_error(L, xorstr_("invalid function object"));
    Closure* cl = clvalue(o);
    if (!allowCclosure && cl->isC)
        luaL_error(L, xorstr_("luau function expected"));
    if (popcl)
        lua_pop(L, 1);
    return cl;
}

int setstackhidden(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const bool hide = lua_toboolean(L, 2) != 0;
    Closure* cl = clvalue(luaA_toobject(L, 1));
    void* key = closure_key(cl);
    auto& state = get_state(L);

    if (hide) {
        if (!state.hidden_closure_refs.count(key)) {
            lua_pushvalue(L, 1);
            int ref = lua_ref(L, -1);
            lua_pop(L, 1);
            state.hidden_closure_refs[key] = ref;
        }
    }
    else {
        auto it = state.hidden_closure_refs.find(key);
        if (it != state.hidden_closure_refs.end()) {
            lua_unref(L, it->second);
            state.hidden_closure_refs.erase(it);
        }
    }
    return 0;
}

int debug_traceback(lua_State* L)
{
    lua_State* thread = L;
    const char* prefix = nullptr;

    if (lua_isthread(L, 1)) {
        thread = lua_tothread(L, 1);
        if (lua_isstring(L, 2)) prefix = lua_tostring(L, 2);
    }
    else if (lua_isstring(L, 1)) {
        prefix = lua_tostring(L, 1);
    }

    auto& state = get_state(L);
    std::string result;
    if (prefix) { result = prefix; result += "\n"; }
    result += xorstr_("Stack Begin");

    const int stacksize = (int)(thread->ci - thread->base_ci);
    for (int level = 1; level < stacksize; level++)
    {
        lua_Debug ar;
        if (!lua_getinfo(thread, level, xorstr_("sluanf"), &ar))
            break;

        Closure* cl = nullptr;
        if (thread == L) {
            if (lua_isfunction(L, -1)) cl = clvalue(luaA_toobject(L, -1));
            lua_pop(L, 1);
        }
        else {
            if (lua_isfunction(thread, -1)) cl = clvalue(luaA_toobject(thread, -1));
            lua_pop(thread, 1);
        }

        if (cl && state.hidden_closure_refs.count(closure_key(cl)))
            continue;

        result += "\n";
        const char* src = (ar.short_src && ar.short_src[0]) ? ar.short_src : xorstr_("?");
        const int   line = (ar.currentline > 0) ? ar.currentline : ar.linedefined;
        result += xorstr_("Script '");
        result += src;
        result += xorstr_(", Line ");
        result += std::to_string(line);

        const char* fname = best_function_name(ar, cl);
        if (fname) {
            result += xorstr_(" - function '");
            result += fname;
            result += "'";
        }
    }
    result += xorstr_("\nStack End");
    lua_pushstring(L, result.c_str());
    return 1;
}

static void set_proto_source(lua_State* L, Proto* p, const char* src, size_t len)
{
    TString* s;
    if (len > 0 && (src[0] == '@' || src[0] == '='))
        s = luaS_newlstr(L, src, len);
    else {
        std::string prefixed; prefixed.reserve(len + 1);
        prefixed.push_back('='); prefixed.append(src, len);
        s = luaS_newlstr(L, prefixed.c_str(), prefixed.size());
    }
    p->source.Set(s);
    p->debugname.Set(s);
}

int debug_setinfo(lua_State* L)
{
    Closure* cl = nullptr;
    CallInfo* ci_target = nullptr;

    if (lua_isnumber(L, 1))
    {
        const int level = lua_tointeger(L, 1);
        const int stacksz = static_cast<int>(L->ci - L->base_ci);
        if (level <= 0 || level >= stacksz)
            luaL_argerror(L, 1, xorstr_("level out of range"));
        lua_Debug ar;
        if (!lua_getinfo(L, level, xorstr_("f"), &ar))
            luaL_argerror(L, 1, xorstr_("invalid level"));
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); luaL_argerror(L, 1, xorstr_("level not a function")); }
        cl = clvalue(luaA_toobject(L, -1));
        lua_pop(L, 1);
        ci_target = L->ci - level;
    }
    else if (lua_isfunction(L, 1))
    {
        cl = clvalue(luaA_toobject(L, 1));
    }
    else
    {
        luaL_argerror(L, 1, xorstr_("function or number expected"));
    }

    if (!cl) luaL_argerror(L, 1, xorstr_("invalid function"));

    void* key = closure_key(cl);
    Proto* p = (!cl->isC && cl->l.p) ? cl->l.p : nullptr;
    auto& state = get_state(L);

    if (lua_istable(L, 2))
    {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                const char* k = lua_tostring(L, -2);

                if ((strcmp(k, xorstr_("source")) == 0 || strcmp(k, xorstr_("short_src")) == 0)
                    && lua_isstring(L, -1))
                {
                    size_t len = 0; const char* src = lua_tolstring(L, -1, &len);
                    if (p) set_proto_source(L, p, src, len);
                    if (len > 0 && (src[0] == '=' || src[0] == '@'))
                        state.fake_sources[key] = std::string(src + 1, len - 1);
                    else
                        state.fake_sources[key] = std::string(src, len);
                }
                else if (strcmp(k, xorstr_("name")) == 0 && lua_isstring(L, -1))
                {
                    size_t len = 0; const char* name = lua_tolstring(L, -1, &len);
                    if (p) p->debugname.Set(luaS_newlstr(L, name, len));
                    state.fake_names[key] = std::string(name, len);
                }
                else if (strcmp(k, xorstr_("linedefined")) == 0 && lua_isnumber(L, -1))
                {
                    if (p) p->linedefined = (int)lua_tointeger(L, -1);
                }
                else if (strcmp(k, xorstr_("currentline")) == 0 && lua_isnumber(L, -1))
                {
                    state.fake_currentlines[key] = (int)lua_tointeger(L, -1);
                }
            }
            lua_pop(L, 1);
        }
        return 0;
    }

    luaL_checktype(L, 2, LUA_TSTRING);
    luaL_checkany(L, 3);
    const char* field = lua_tostring(L, 2);

    if (strcmp(field, xorstr_("linedefined")) == 0)
    {
        if (p) p->linedefined = luaL_checkinteger(L, 3);
    }
    else if (strcmp(field, xorstr_("source")) == 0 || strcmp(field, xorstr_("short_src")) == 0)
    {
        size_t len = 0; const char* src = luaL_checklstring(L, 3, &len);
        if (p) set_proto_source(L, p, src, len);
        if (len > 0 && (src[0] == '=' || src[0] == '@'))
            state.fake_sources[key] = std::string(src + 1, len - 1);
        else
            state.fake_sources[key] = std::string(src, len);
    }
    else if (strcmp(field, xorstr_("name")) == 0)
    {
        size_t len = 0; const char* name = luaL_checklstring(L, 3, &len);
        if (p) p->debugname.Set(luaS_newlstr(L, name, len));
        state.fake_names[key] = std::string(name, len);
    }
    else if (strcmp(field, xorstr_("currentline")) == 0)
    {
        const int target_line = luaL_checkinteger(L, 3);
        state.fake_currentlines[key] = target_line;
        if (ci_target && p && p->lineinfo.Get() && p->sizecode > 0) {
            for (int i = 0; i < p->sizecode; i++) {
                if (luaG_getline(p, i) == target_line) {
                    ci_target->savedpc = p->code + i + 1; break;
                }
            }
        }
    }
    else
    {
        luaL_argerrorL(L, 2, xorstr_("unknown field (source/short_src/name/linedefined/currentline)"));
    }

    return 0;
}

int debug_validlevel(lua_State* L)
{
    const int level = luaL_checkinteger(L, 1);
    lua_State* thread = L;
    if (lua_isthread(L, 2)) {
        thread = lua_tothread(L, 2);
        if (!thread) luaL_argerror(L, 2, xorstr_("invalid thread"));
    }
    const int stacksize = static_cast<int>(thread->ci - thread->base_ci);
    lua_pushboolean(L, level >= 0 && level < stacksize);
    return 1;
}

int debug_getcallstack(lua_State* L)
{
    lua_State* thread = L;
    if (lua_isthread(L, 1)) {
        thread = lua_tothread(L, 1);
        if (!thread) luaL_argerror(L, 1, xorstr_("invalid thread"));
    }

    lua_newtable(L);
    int count = 0;
    const int stacksize = (int)(thread->ci - thread->base_ci);

    for (int level = 1; level < stacksize; level++)
    {
        lua_Debug ar;
        if (!lua_getinfo(thread, level, xorstr_("sluanf"), &ar))
            break;

        lua_newtable(L);
        if (thread == L) lua_insert(L, -2);
        else             lua_xmove(thread, L, 1);
        lua_setfield(L, -2, xorstr_("func"));

        lua_pushstring(L, ar.source ? ar.source : xorstr_("=[C]"));
        lua_setfield(L, -2, xorstr_("source"));
        lua_pushstring(L, (ar.short_src && ar.short_src[0]) ? ar.short_src : xorstr_("=[C]"));
        lua_setfield(L, -2, xorstr_("short_src"));
        lua_pushstring(L, ar.what ? ar.what : xorstr_("C"));
        lua_setfield(L, -2, xorstr_("what"));
        lua_pushinteger(L, ar.linedefined);
        lua_setfield(L, -2, xorstr_("linedefined"));
        lua_pushinteger(L, ar.currentline);
        lua_setfield(L, -2, xorstr_("currentline"));
        lua_pushinteger(L, ar.nupvals);
        lua_setfield(L, -2, xorstr_("nups"));
        lua_pushinteger(L, ar.nparams);
        lua_setfield(L, -2, xorstr_("numparams"));
        lua_pushinteger(L, ar.isvararg);
        lua_setfield(L, -2, xorstr_("is_vararg"));
        lua_pushstring(L, ar.name ? ar.name : xorstr_("?"));
        lua_setfield(L, -2, xorstr_("name"));

        lua_rawseti(L, -2, ++count);
    }

    return 1;
}
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
        luaL_argerror(L, 2, "upvalue index must be positive");

    header_get_function(L, false, false);
    const int functionIndex = lua_gettop(L);

    const char* name = lua_getupvalue(L, functionIndex, index);
    if (!name)
        luaL_argerror(L, 2, "upvalue index out of bounds");

    return 1;
}

int debug_setupvalue(lua_State* L)
{
    const auto Func = header_get_function(L, false, false);
    const auto idx = lua_tointeger(L, 2);
    if (Func->nupvalues <= 0)
        luaL_argerror(L, 1, "function has no upvalues");
    if (!(idx >= 1 && idx <= Func->nupvalues))
        luaL_argerror(L, 2, "index out of range");

    lua_pushvalue(L, 3);
    lua_setupvalue(L, -2, idx);
    return 1;
}

int debug_getconstants(lua_State* L)
{
    const auto Func = header_get_function(L);
    const auto p = (Proto*)Func->l.p;
    lua_createtable(L, p->sizek, 0);
    for (int i = 0; i < p->sizek; i++) {
        TValue k = p->k[i];
        if (k.tt == LUA_TNIL || k.tt == LUA_TFUNCTION || k.tt == LUA_TTABLE)
            lua_pushnil(L);
        else { luaC_threadbarrier(L); luaA_pushvalue(L, &k); }
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int debug_getconstant(lua_State* L)
{
    const auto Func = header_get_function(L);
    const auto idx = luaL_checkinteger(L, 2);
    const auto p = (Proto*)Func->l.p;
    if (p->sizek <= 0) luaL_argerror(L, 1, xorstr_("function has no constants"));
    if (!(idx >= 1 && idx <= p->sizek)) luaL_argerror(L, 2, xorstr_("index out of bounds"));
    const auto k = &p->k[idx - 1];
    if (k->tt == LUA_TNIL || k->tt == LUA_TTABLE || k->tt == LUA_TFUNCTION)
    {
        lua_pushnil(L); return 1;
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
    if (p->sizek <= 0) luaL_argerror(L, 1, xorstr_("function has no constants"));
    if (!(idx >= 1 && idx <= p->sizek)) luaL_argerror(L, 2, xorstr_("index out of range"));
    TValue* k = &p->k[idx - 1];
    if (k->tt == LUA_TFUNCTION || k->tt == LUA_TTABLE) return 0;
    if (k->tt == luaA_toobject(L, 3)->tt)
        setobj2s(L, k, luaA_toobject(L, 3));
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

Proto* clone_proto(lua_State* L, Proto* proto)
{
    Proto* clone = luaF_newproto(L);
    clone->sizek = proto->sizek;
    clone->k = luaM_newarray(L, proto->sizek, TValue, proto->memcat);
    for (int i = 0; i < proto->sizek; ++i) setobj2n(L, &clone->k[i], &proto->k[i]);
    uint8_t* lp = proto->lineinfo.Get();
    if (lp) {
        clone->lineinfo.Set(luaM_newarray(L, proto->sizelineinfo, uint8_t, proto->memcat));
        memcpy(clone->lineinfo.Get(), lp, proto->sizelineinfo);
    }
    LocVar* lvp = proto->locvars.Get();
    clone->locvars.Set(luaM_newarray(L, proto->sizelocvars, LocVar, proto->memcat));
    for (int i = 0; i < proto->sizelocvars; ++i) {
        const char* vn = getstr(lvp[i].varname);
        clone->locvars.Get()[i].varname = luaS_newlstr(L, vn, strlen(vn));
        clone->locvars.Get()[i].endpc = lvp[i].endpc;
        clone->locvars.Get()[i].reg = lvp[i].reg;
        clone->locvars.Get()[i].startpc = lvp[i].startpc;
    }
    clone->nups = proto->nups; clone->sizeupvalues = proto->sizeupvalues;
    clone->sizelineinfo = proto->sizelineinfo; clone->linegaplog2 = proto->linegaplog2;
    clone->sizelocvars = proto->sizelocvars; clone->linedefined = proto->linedefined;
    if (proto->sizeupvalues > 0) {
        clone->upvalues.Set(luaM_newarray(L, proto->sizeupvalues, TString*, proto->memcat));
        for (int i = 0; i < proto->sizeupvalues; ++i) {
            TString* n = proto->upvalues.Get()[i];
            clone->upvalues.Get()[i] = n ? luaS_newlstr(L, getstr(n), strlen(getstr(n))) : nullptr;
        }
    }
    else { clone->upvalues.Set(nullptr); }
    TString* dn = proto->debugname.Get();
    if (dn) clone->debugname.Set(luaS_newlstr(L, getstr(dn), strlen(getstr(dn))));
    TString* sn = proto->source.Get();
    if (sn) clone->source.Set(luaS_newlstr(L, getstr(sn), strlen(getstr(sn))));
    clone->numparams = proto->numparams; clone->is_vararg = proto->is_vararg;
    clone->maxstacksize = proto->maxstacksize; clone->bytecodeid = proto->bytecodeid;
    auto bc = Execution::CompileScript(xorstr_("return"));
    luau_load(L, xorstr_("@cloneproto"), bc.c_str(), bc.size(), 0);
    Closure* cl = clvalue(index2addr(L, -1));
    clone->sizecode = cl->l.p->sizecode;
    clone->code = luaM_newarray(L, clone->sizecode, Instruction, proto->memcat);
    for (size_t i = 0; i < cl->l.p->sizecode; i++) clone->code[i] = cl->l.p->code[i];
    lua_pop(L, 1);
    clone->codeentry = clone->code; clone->debuginsn.Set(nullptr);
    clone->sizep = proto->sizep;
    clone->p = luaM_newarray(L, proto->sizep, Proto*, proto->memcat);
    for (int i = 0; i < proto->sizep; ++i) clone->p[i] = clone_proto(L, proto->p[i]);
    return clone;
}

int debug_getproto(lua_State* L)
{
    luaL_checktype(L, 2, LUA_TNUMBER);
    Closure* closure = nullptr;
    int index = lua_tointeger(L, 2);
    bool active = luaL_optboolean(L, 3, false);
    if (lua_isfunction(L, 1)) {
        closure = clvalue(luaA_toobject(L, 1));
    }
    else if (lua_isnumber(L, 1)) {
        lua_Debug dbg; int lv = lua_tointeger(L, 1);
        int csz = static_cast<int>(L->ci - L->base_ci);
        if (lv <= 0 || csz <= lv) luaL_argerrorL(L, 1, xorstr_("level out of bounds"));
        if (!lua_getinfo(L, lv, xorstr_("f"), &dbg)) luaL_argerrorL(L, 1, xorstr_("level out of bounds"));
        if (!lua_isfunction(L, -1)) luaL_argerrorL(L, 1, xorstr_("not a function"));
        if (lua_iscfunction(L, -1)) luaL_argerrorL(L, 1, xorstr_("lua function expected"));
        closure = clvalue(luaA_toobject(L, -1)); lua_pop(L, 1);
    }
    if (!closure) luaL_argerrorL(L, 1, xorstr_("function or level expected"));
    if (closure->isC) luaL_argerrorL(L, 1, xorstr_("Lua closure expected"));
    Proto* p = closure->l.p;
    if (!p->p) luaL_error(L, xorstr_("proto list missing"));
    if (index <= 0 || index > (int)p->sizep) luaL_argerrorL(L, 2, xorstr_("index out of bounds"));
    Proto* wp = p->p[index - 1];
    if (!wp) { lua_pushnil(L); return 1; }
    if (!active) {
        Proto* cp = clone_proto(L, wp);
        Closure* nc = luaF_newLclosure(L, closure->nupvalues, L->gt, cp);
        luaC_checkGC(L); luaC_threadbarrier(L);
        luaD_checkstack(L, 1); luaC_checkGC(L); luaC_threadbarrier(L);
        L->top->value.gc = reinterpret_cast<GCObject*>(nc);
        L->top->tt = LUA_TFUNCTION; L->top++;
    }
    else {
        const global_State* g = L->global; int oc = 0;
        lua_newtable(L);
        lua_gc(L, LUA_GCSTOP, 0);
        for (lua_Page* pg = g->allgcopages; pg;) {
            lua_Page* nx = pg->listnext;
            char* start; char* end; int busy; int bsz;
            luaM_getpagewalkinfo(pg, &start, &end, &busy, &bsz);
            for (char* pos = start; pos != end; pos += bsz) {
                GCObject* gco = reinterpret_cast<GCObject*>(pos);
                if (gco->gch.tt != LUA_TFUNCTION || isdead(g, gco)) continue;
                const Closure* gc_cl = reinterpret_cast<Closure*>(pos);
                if (gc_cl->isC || gc_cl->l.p != wp) continue;
                luaC_checkGC(L); luaC_threadbarrier(L);
                luaD_checkstack(L, 1);
                L->top->value.gc = gco; L->top->tt = gco->gch.tt;
                L->top++;
                lua_rawseti(L, -2, ++oc);
            }
            pg = nx;
        }
        lua_gc(L, LUA_GCRESTART, 0);
    }
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
                strcmp(debugName, ("__hookfunction_dispatch")) == 0);
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

int debug_getstack(lua_State* L)
{
    lua_State* target = L;
    int arg = 0;

    if (lua_isthread(L, 1))
    {
        target = lua_tothread(L, 1);
        if (!target)
            luaL_argerror(L, 1, xorstr_("invalid thread"));
        arg = 1;
    }

    const int level = luaL_checkinteger(L, arg + 1);
    CallInfo* ci = resolve_visible_stack_level(target, level);
    if (!ci)
        luaL_argerror(L, arg + 1, xorstr_("level out of range"));

    if (ci < target->base_ci || ci > target->ci)
        luaL_error(L, xorstr_("invalid callinfo"));

    if (!ci->func || ci->func->tt != LUA_TFUNCTION)
        luaL_error(L, xorstr_("invalid stack frame"));

    Closure* closure = clvalue(ci->func);
    if (closure && closure->isC)
        luaL_error(L, xorstr_("cannot get stack of a C function"));

    if (!ci->base)
        luaL_error(L, xorstr_("callinfo base is null"));

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
    lua_State* target = L;
    int arg = 0;

    if (lua_isthread(L, 1))
    {
        target = lua_tothread(L, 1);
        if (!target)
            luaL_argerror(L, 1, xorstr_("invalid thread"));
        arg = 1;
    }

    const int level = luaL_checkinteger(L, arg + 1);
    const int idx = luaL_checkinteger(L, arg + 2) - 1;

    CallInfo* ci = resolve_visible_stack_level(target, level);
    if (!ci)
        luaL_argerror(L, arg + 1, xorstr_("level out of range"));

    if (ci < target->base_ci || ci > target->ci)
        luaL_error(L, xorstr_("invalid callinfo"));

    if (!ci->func || ci->func->tt != LUA_TFUNCTION)
        luaL_error(L, xorstr_("invalid stack frame"));

    Closure* closure = clvalue(ci->func);
    if (closure && closure->isC)
        luaL_error(L, xorstr_("cannot set stack of a C function"));

    if (!ci->base)
        luaL_error(L, xorstr_("callinfo base is null"));

    const int frame_size = stack_frame_size(target, ci, closure);
    if (idx < 0 || idx >= frame_size)
        luaL_argerror(L, arg + 2, xorstr_("index out of range"));

    const TValue* new_value = luaA_toobject(L, arg + 3);
    if (!new_value)
        luaL_argerror(L, arg + 3, xorstr_("invalid value"));

    setobj2s(target, ci->base + idx, new_value);
    luaC_barrierfast(target, closure);
    return 0;
}

int debug_getinfo(lua_State* L)
{
    if (!(lua_isfunction(L, 1) || lua_isnumber(L, 1)))
        luaL_argerror(L, 1, xorstr_("function or number"));
    int level = lua_isnumber(L, 1) ? lua_tointeger(L, 1) : -lua_gettop(L);
    auto desc = xorstr_("sluanf");
    lua_Debug ar;
    if (!lua_getinfo(L, level, desc, &ar)) luaL_argerror(L, 1, xorstr_("invalid level"));
    if (!lua_isfunction(L, -1)) luaL_argerror(L, 1, xorstr_("not a function"));
    Closure* ic = clvalue(luaA_toobject(L, -1));
    void* key = ic ? closure_key(ic) : nullptr;
    auto& state = get_state(L);

    lua_newtable(L);
    if (std::strchr(desc, 's')) {
        if (ic && !ic->isC && ic->l.p) {
            TString* st = ic->l.p->source.Get();
            const char* raw = st ? getstr(st) : nullptr;
            lua_pushstring(L, raw ? raw : (ar.source ? ar.source : xorstr_("=[C]")));
            lua_setfield(L, -2, xorstr_("source"));
            if (raw && (raw[0] == '=' || raw[0] == '@'))
                lua_pushstring(L, raw + 1);
            else
                lua_pushstring(L, ar.short_src[0] ? ar.short_src : xorstr_("=[C]"));
            lua_setfield(L, -2, xorstr_("short_src"));
        }
        else {
            lua_pushstring(L, ar.source ? ar.source : xorstr_("=[C]")); lua_setfield(L, -2, xorstr_("source"));
            lua_pushstring(L, ar.short_src[0] ? ar.short_src : xorstr_("=[C]")); lua_setfield(L, -2, xorstr_("short_src"));
        }
        lua_pushstring(L, ar.what ? ar.what : xorstr_("C")); lua_setfield(L, -2, xorstr_("what"));
        lua_pushinteger(L, ar.linedefined); lua_setfield(L, -2, xorstr_("linedefined"));
    }
    if (std::strchr(desc, 'l')) { lua_pushinteger(L, ar.currentline); lua_setfield(L, -2, xorstr_("currentline")); }
    if (std::strchr(desc, 'u')) { lua_pushinteger(L, ar.nupvals);     lua_setfield(L, -2, xorstr_("nups")); }
    if (std::strchr(desc, 'a')) {
        lua_pushinteger(L, ar.isvararg); lua_setfield(L, -2, xorstr_("is_vararg"));
        lua_pushinteger(L, ar.nparams);  lua_setfield(L, -2, xorstr_("numparams"));
    }
    if (std::strchr(desc, 'n')) {
        const char* nm = best_function_name(ar, ic);
        if (!nm) nm = xorstr_("?");
        if (key) { auto it = state.fake_names.find(key); if (it != state.fake_names.end()) nm = it->second.c_str(); }
        lua_pushstring(L, nm); lua_setfield(L, -2, xorstr_("name"));
    }
    if (std::strchr(desc, 'f')) {
        lua_pushvalue(L, -2); lua_remove(L, -3); lua_setfield(L, -2, xorstr_("func"));
    }
    return 1;
}

int debug_getregistry(lua_State* L)
{
    lua_pushvalue(L, LUA_REGISTRYINDEX); return 1;
}

int debug_getmetatable(lua_State* L)
{
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1)) lua_pushnil(L);
    return 1;
}

int debug_setmetatable(lua_State* L)
{
    luaL_checkany(L, 1);
    int t = lua_type(L, 2);
    if (t != LUA_TNIL && t != LUA_TTABLE) luaL_typeerror(L, 2, xorstr_("nil or table expected"));
    lua_pushvalue(L, 2); lua_setmetatable(L, 1);
    return 1;
}

namespace Debug
{

    void RegisterLibrary(lua_State* L)
    {
        get_state(L).clear(L);

        lua_newtable(L);

        int origTop = lua_gettop(Shared::RobloxState);
        lua_getglobal(Shared::RobloxState, xorstr_("debug"));
        if (lua_istable(Shared::RobloxState, -1))
        {
            lua_pushnil(Shared::RobloxState);
            while (lua_next(Shared::RobloxState, -2) != 0) {
                if (lua_isfunction(Shared::RobloxState, -1)) {
                    const char* k = lua_tostring(Shared::RobloxState, -2);
                    if (k) { lua_xmove(Shared::RobloxState, L, 1); lua_setfield(L, -2, k); }
                    else { lua_pop(Shared::RobloxState, 1); }
                }
                else { lua_pop(Shared::RobloxState, 1); }
            }
        }
        lua_settop(Shared::RobloxState, origTop);

        lua_getfield(L, -1, xorstr_("info"));
        if (lua_isfunction(L, -1))
            get_state(L).roblox_debug_info_ref = lua_ref(L, -1);
        lua_pop(L, 1);

        Utils::AddTableFunction(L, xorstr_("getupvalues"), debug_getupvalues);
        Utils::AddTableFunction(L, xorstr_("getupvalue"), debug_getupvalue);
        Utils::AddTableFunction(L, xorstr_("setupvalue"), debug_setupvalue);
        Utils::AddTableFunction(L, xorstr_("getconstants"), debug_getconstants);
        Utils::AddTableFunction(L, xorstr_("getconstant"), debug_getconstant);
        Utils::AddTableFunction(L, xorstr_("setconstant"), debug_setconstant);
        Utils::AddTableFunction(L, xorstr_("getproto"), debug_getproto);
        Utils::AddTableFunction(L, xorstr_("getprotos"), debug_getprotos);
        Utils::AddTableFunction(L, xorstr_("getstack"), debug_getstack);
        Utils::AddTableFunction(L, xorstr_("setstack"), debug_setstack);
        Utils::AddTableFunction(L, xorstr_("getinfo"), debug_getinfo);

        lua_setglobal(L, xorstr_("debug"));

        Utils::AddFunction(L, xorstr_("getupvalues"), debug_getupvalues);
        Utils::AddFunction(L, xorstr_("getupvalue"), debug_getupvalue);
        Utils::AddFunction(L, xorstr_("setupvalue"), debug_setupvalue);
        Utils::AddFunction(L, xorstr_("getconstants"), debug_getconstants);
        Utils::AddFunction(L, xorstr_("getconstant"), debug_getconstant);
        Utils::AddFunction(L, xorstr_("setconstant"), debug_setconstant);
        Utils::AddFunction(L, xorstr_("getproto"), debug_getproto);
        Utils::AddFunction(L, xorstr_("getprotos"), debug_getprotos);
        Utils::AddFunction(L, xorstr_("getstack"), debug_getstack);
        Utils::AddFunction(L, xorstr_("setstack"), debug_setstack);
        Utils::AddFunction(L, xorstr_("getinfo"), debug_getinfo);
    }
}
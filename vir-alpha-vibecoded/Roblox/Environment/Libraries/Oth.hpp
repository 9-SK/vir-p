#pragma once

#include <lapi.h>
#include <ldo.h>
#include <lgc.h>
#include <lfunc.h>
#include <lstate.h>
#include <lobject.h>

#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstring>

#include <Roblox/Shared/Shared.hpp>

namespace Oth
{
    template <typename T>
    __forceinline bool IsPtrValid(T value)
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

    struct alignas(64) OPG
    {
        static constexpr size_t K = 2048;
        struct E { uintptr_t p; uint8_t f; };
        E t[K]{};

        uint8_t g(uintptr_t a) noexcept
        {
            uintptr_t pg = a >> 12;
            E& e = t[pg & (K - 1)];
            if (e.p == pg) return e.f;
            MEMORY_BASIC_INFORMATION m{};
            uint8_t f = 4;
            if (VirtualQuery((void*)a, &m, sizeof(m)) &&
                m.State == MEM_COMMIT &&
                !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            {
                if (m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                    PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
                    f |= 1;
            }
            e = { pg, f };
            return f;
        }

        bool r(const void* p) noexcept { return p && (g((uintptr_t)p) & 1); }
        void inv() noexcept { std::memset(t, 0, sizeof(t)); }
    };

    inline OPG s_pg;

    inline bool alive(const Closure* c) noexcept
    {
        if (!c) return false;
        __try
        {
            if (!s_pg.r(c)) return false;
            if (c->tt != LUA_TFUNCTION || c->nupvalues > 128 || c->stacksize > 250)
                return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct GCR
    {
        std::unordered_map<Closure*, std::pair<int, int>> refs;

        void prot(lua_State* L, Closure* c)
        {
            if (!c || !L) return;
            auto [i, n] = refs.try_emplace(c, 0, 0);
            if (i->second.second == 0)
            {
                lua_rawcheckstack(L, 1);
                luaC_threadbarrier(L);
                setclvalue(L, L->top, c);
                L->top++;
                i->second.first = lua_ref(L, -1);
                lua_pop(L, 1);
            }
            ++i->second.second;
        }

        void unprot(lua_State* L, Closure* c)
        {
            if (!c || !L) return;
            auto i = refs.find(c);
            if (i == refs.end()) return;
            if (--i->second.second <= 0)
            {
                lua_unref(L, i->second.first);
                refs.erase(i);
            }
        }

        void drop() { refs.clear(); }
    };

    inline GCR s_gc;

    inline void opush(lua_State* L, Closure* c)
    {
        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        setclvalue(L, L->top++, c);
    }

    inline void oclone(lua_State* L, int i)
    {
        auto* s = clvalue(luaA_toobject(L, i));
        auto* d = luaF_newCclosure(L, s->nupvalues, s->env);
        d->c.f = s->c.f;
        d->c.cont = s->c.cont;
        d->c.debugname = s->c.debugname;
        for (int j = 0; j < s->nupvalues; ++j)
            setobj(L, &d->c.upvals[j], &s->c.upvals[j]);
        setclvalue(L, L->top, d);
        L->top++;
    }

    inline std::string serr(const char* r)
    {
        if (!r) return ("unknown error");
        const char* p = r, * l = nullptr;
        while (*p)
        {
            if (*p == '"' && p[1] == ']' && p[2] == ':')
            {
                const char* d = p + 3;
                while (*d >= '0' && *d <= '9') ++d;
                if (*d == ':' && d[1] == ' ') l = d + 2;
            }
            ++p;
        }
        return l ? std::string(l) : std::string(r);
    }

    inline std::unordered_map<Closure*, Closure*> s_wm;

    inline int _wp(lua_State* L);
    inline int _wc(lua_State* L, int st);

    inline int _wp(lua_State* L)
    {
        if (!L->ci || !L->ci->func || L->ci->func->tt != LUA_TFUNCTION)
            return 0;
        auto it = s_wm.find(clvalue(L->ci->func));
        if (it == s_wm.end() || !it->second) return 0;
        Closure* t = it->second;
        if (t->isC && t->c.f != _wp)
        {
            luaC_threadbarrier(L);
            L->ci->func->value.p = t;
            L->ci->flags &= ~LUA_CALLINFO_HANDLE;
            return t->c.f(L);
        }
        int n = lua_gettop(L);
        lua_rawcheckstack(L, 2);
        luaC_threadbarrier(L);
        setclvalue(L, L->top++, t);
        lua_insert(L, 1);
        L->ci->flags |= LUA_CALLINFO_HANDLE;
        ++L->baseCcalls;
        bool rb = false;
        if (L->nCcalls >= LUAI_MAXCCALLS - 2)
        {
            L->nCcalls -= 2;
            rb = true;
        }
        int st = lua_pcall(L, n, LUA_MULTRET, 0);
        if (rb) L->nCcalls += 2;
        --L->baseCcalls;
        if (st == LUA_YIELD || st == 1 || st == 6 || st == 127)
            return -1;
        if (st != LUA_OK)
        {
            const char* r = lua_tostring(L, -1);
            if (r && std::strcmp(r, ("attempt to yield across metamethod/C-call boundary")) == 0)
                return lua_yield(L, 0);
            auto e = serr(r);
            lua_pop(L, 1);
            lua_pushlstring(L, e.c_str(), e.size());
            lua_error(L);
        }
        expandstacklimit(L, L->top);
        if (L->status == LUA_YIELD || L->status == LUA_BREAK || L->status == SCHEDULED_REENTRY)
            return -1;
        return lua_gettop(L);
    }

    inline int _wc(lua_State* L, int st)
    {
        if (st == 1 || st == 6 || st == 127)
            return lua_gettop(L);
        if (st != LUA_OK)
        {
            const char* r = lua_tostring(L, -1);
            if (r && std::strcmp(r, ("error in error handling")) == 0)
            {
                lua_pop(L, 1);
                lua_pushstring(L, ("C stack overflow"));
                lua_error(L);
            }
            if (r && std::strcmp(r, ("attempt to yield across metamethod/C-call boundary")) == 0)
                return lua_yield(L, 0);
            auto e = serr(r);
            lua_pop(L, 1);
            lua_pushlstring(L, e.c_str(), e.size());
            lua_error(L);
        }
        return lua_gettop(L);
    }

    inline Closure* owrap(lua_State* L, int i)
    {
        Closure* inner = clvalue(luaA_toobject(L, i));
        lua_pushcclosurek(L, _wp, nullptr, 0, _wc);
        Closure* px = clvalue(luaA_toobject(L, -1));
        lua_State* rL = Shared::RobloxState;
        if (rL && rL->gt && IsPtrValid((uintptr_t)rL->gt))
            px->env = rL->gt;
        s_wm[px] = inner;
        s_gc.prot(L, px);
        s_gc.prot(L, inner);
        lua_pop(L, 1);
        return px;
    }

    struct OReq
    {
        Closure* orig;
        TString* nc;
        std::vector<TValue> args;
    };

    inline lua_CFunction g_hf = nullptr;
    inline std::unordered_map<Closure*, Closure*> g_oc, g_hk;
    inline std::unordered_map<lua_State*, lua_State*> g_hp, g_ph;
    inline std::unordered_map<lua_State*, Closure*> g_cb;
    inline std::unordered_map<lua_State*, int> g_pr;
    inline std::unordered_map<lua_State*, OReq> g_rq;
    inline std::unordered_set<lua_State*> g_pn;
    inline std::unordered_map<Closure*, Closure*> g_orig;
    inline std::unordered_map<Closure*, Closure*> g_fwd;

    inline int H(lua_State* L);
    inline int HC(lua_State* L, int st);

    inline bool ihk(Closure* c) { return c && g_oc.count(c) > 0; }

    inline void drest(Closure* o)
    {
        if (!o) return;
        auto i = g_oc.find(o);
        if (i != g_oc.end() && i->second)
            o->c.f = i->second->c.f;
    }

    inline void dclean(lua_State* L, Closure* o)
    {
        if (!o) return;
        drest(o);
        auto oi = g_oc.find(o);
        if (oi != g_oc.end())
        {
            s_gc.unprot(L, oi->second);
            g_oc.erase(oi);
        }
        auto hi = g_hk.find(o);
        if (hi != g_hk.end())
        {
            s_gc.unprot(L, hi->second);
            g_hk.erase(hi);
        }
        auto gi = g_orig.find(o);
        if (gi != g_orig.end())
            g_orig.erase(gi);
        auto fi = g_fwd.find(o);
        if (fi != g_fwd.end())
            g_fwd.erase(fi);
    }

    inline void hlclean(lua_State* L, lua_State* hl)
    {
        auto ri = g_pr.find(L);
        if (ri != g_pr.end())
        {
            lua_unref(L, ri->second);
            g_pr.erase(ri);
        }
        g_hp.erase(hl);
        g_cb.erase(hl);
        g_ph.erase(L);
        g_rq.erase(hl);
    }

    inline int oproxy_impl(lua_State* L)
    {
        Closure* self = clvalue(L->ci->func);
        if (self->nupvalues < 1 || self->c.upvals[0].tt != LUA_TFUNCTION)
            luaL_error(L, ("oth: bad proxy"));
        Closure* orig = clvalue(&self->c.upvals[0]);
        if (!alive(orig))
            luaL_error(L, ("oth: collected"));
        if (!g_hp.count(L))
        {
            int n = lua_gettop(L);
            lua_rawcheckstack(L, 2);
            luaC_threadbarrier(L);
            setclvalue(L, L->top++, orig);
            lua_insert(L, 1);
            lua_call(L, n, LUA_MULTRET);
            return lua_gettop(L);
        }
        OReq rq;
        rq.orig = orig;
        rq.nc = L->namecall;
        int n = lua_gettop(L);
        rq.args.reserve(n);
        for (int i = 1; i <= n; ++i)
            rq.args.push_back(*luaA_toobject(L, i));
        g_rq[L] = std::move(rq);
        lua_settop(L, 0);
        return lua_yield(L, 0);
    }

    inline int oproxy(lua_State* L)
    {
        DWORD __exc = 0;
        __try { return oproxy_impl(L); }
        __except (__exc = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {}
        luaL_error(L, ("oproxy exception (0x%08X)"), __exc);
        return 0;
    }

    inline int preq_impl(lua_State* L, lua_State* hl)
    {
        auto it = g_rq.find(hl);
        if (it == g_rq.end()) return -1;
        OReq rq = std::move(it->second);
        g_rq.erase(it);
        if (!alive(rq.orig))
        {
            lua_rawcheckstack(hl, 1);
            lua_pushstring(hl, ("oth: collected"));
            hl->baseCcalls++;
            int s = lua_resume(hl, L, 0);
            hl->baseCcalls--;
            return s;
        }
        lua_settop(L, 0);
        lua_rawcheckstack(L, (int)rq.args.size() + 2);
        luaC_threadbarrier(L);
        setclvalue(L, L->top++, rq.orig);
        if (rq.nc) L->namecall = rq.nc;
        for (auto& v : rq.args)
        {
            *L->top = v;
            ++L->top;
        }
        g_pn.insert(L);
        luaD_call(L, L->top - (int)rq.args.size() - 1, LUA_MULTRET);
        if (L->status == LUA_YIELD || L->status == LUA_BREAK || L->status == SCHEDULED_REENTRY)
            return -1;
        g_pn.erase(L);
        expandstacklimit(L, L->top);
        int nr = lua_gettop(L);
        if (nr > 0)
        {
            lua_rawcheckstack(hl, nr);
            lua_xmove(L, hl, nr);
        }
        hl->baseCcalls++;
        int s = lua_resume(hl, L, nr);
        hl->baseCcalls--;
        if (hl->ci && hl->ci->top < hl->top)
            hl->ci->top = hl->top;
        return s;
    }

    inline int preq(lua_State* L, lua_State* hl)
    {
        DWORD __exc = 0;
        __try { return preq_impl(L, hl); }
        __except (__exc = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {}
        luaL_error(L, ("preq exception (0x%08X)"), __exc);
        return 0;
    }

    inline int H_impl(lua_State* L)
    {
        if (!L->ci || !L->ci->func || L->ci->func->tt != LUA_TFUNCTION)
            return 0;
        Closure* self = clvalue(L->ci->func);
        int na = lua_gettop(L);
        if (g_hp.count(L))
        {
            auto oi = g_oc.find(self);
            if (oi == g_oc.end() || !oi->second || !alive(oi->second))
                return 0;
            OReq rq;
            rq.orig = oi->second;
            rq.nc = L->namecall;
            rq.args.reserve(na);
            for (int i = 1; i <= na; ++i)
                rq.args.push_back(*luaA_toobject(L, i));
            g_rq[L] = std::move(rq);
            lua_settop(L, 0);
            return lua_yield(L, 0);
        }
        auto hi = g_hk.find(self);
        if (hi == g_hk.end() || !hi->second) return 0;
        auto oi = g_oc.find(self);
        if (oi == g_oc.end() || !oi->second) return 0;
        Closure* hp = hi->second, * oc = oi->second;
        if (!alive(hp) || !alive(oc)) return 0;
        TString* snc = L->namecall;
        if (g_ph.count(L))
        {
            int n = lua_gettop(L);
            lua_rawcheckstack(L, n + 1);
            luaC_threadbarrier(L);
            setclvalue(L, L->top++, oc);
            lua_insert(L, 1);
            lua_call(L, n, LUA_MULTRET);
            return lua_gettop(L);
        }
        lua_rawcheckstack(L, 2);
        lua_State* hl = lua_newthread(L);
        g_pr[L] = lua_ref(L, -1);
        lua_pop(L, 1);
        if (L->userdata && !hl->userdata)
            hl->userdata = L->userdata;
        if (snc)
            hl->namecall = snc;
        g_hp[hl] = L;
        g_cb[hl] = oc;
        g_ph[L] = hl;
        lua_settop(L, na);
        lua_rawcheckstack(hl, na + 2);
        opush(hl, hp);
        lua_xmove(L, hl, na);
        hl->baseCcalls++;
        int st = lua_resume(hl, nullptr, na);
        hl->baseCcalls--;
        if (hl->ci && hl->ci->top < hl->top)
            hl->ci->top = hl->top;
        while (st == LUA_YIELD && g_rq.count(hl))
        {
            L->namecall = snc;
            st = preq(L, hl);
            if (st == -1) break;
            if (hl->ci && hl->ci->top < hl->top)
                hl->ci->top = hl->top;
        }
        L->namecall = snc;
        if (L->status == LUA_YIELD || L->status == LUA_BREAK || L->status == SCHEDULED_REENTRY)
            return -1;
        if (st == LUA_YIELD)
        {
            hlclean(L, hl);
            return 0;
        }
        hlclean(L, hl);
        if (st != LUA_OK && st != -1)
        {
            int ne = lua_gettop(hl);
            if (ne > 0)
            {
                lua_rawcheckstack(L, ne);
                lua_xmove(hl, L, ne);
            }
            else
                lua_pushstring(L, ("oth: err"));
            lua_error(L);
            return 0;
        }
        int nr = lua_gettop(hl);
        if (nr > 0)
        {
            lua_rawcheckstack(L, nr);
            lua_xmove(hl, L, nr);
        }
        expandstacklimit(L, L->top);
        return nr;
    }

    inline int H(lua_State* L)
    {
        DWORD __exc = 0;
        __try { return H_impl(L); }
        __except (__exc = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {}
        luaL_error(L, "H exception (0x%08X)", __exc);
        return 0;
    }

    inline int HC_impl(lua_State* L, int status)
    {
        auto pi = g_ph.find(L);
        if (pi == g_ph.end()) return 0;
        lua_State* hl = pi->second;
        if (g_pn.count(L))
        {
            g_pn.erase(L);
            expandstacklimit(L, L->top);
            int nr = lua_gettop(L);
            if (nr > 0)
            {
                lua_rawcheckstack(hl, nr);
                lua_xmove(L, hl, nr);
            }
            hl->baseCcalls++;
            int hs = lua_resume(hl, L, nr);
            hl->baseCcalls--;
            if (hl->ci && hl->ci->top < hl->top)
                hl->ci->top = hl->top;
            while (hs == LUA_YIELD && g_rq.count(hl))
            {
                hs = preq(L, hl);
                if (hs == -1) break;
                if (hl->ci && hl->ci->top < hl->top)
                    hl->ci->top = hl->top;
            }
            if (L->status == LUA_YIELD || L->status == LUA_BREAK || L->status == SCHEDULED_REENTRY)
                return -1;
            if (hs == LUA_YIELD)
            {
                hlclean(L, hl);
                return 0;
            }
            hlclean(L, hl);
            if (hs != LUA_OK && hs != -1)
            {
                int ne = lua_gettop(hl);
                if (ne > 0)
                {
                    lua_rawcheckstack(L, ne);
                    lua_xmove(hl, L, ne);
                }
                else
                    lua_pushstring(L, ("oth: aerr"));
                lua_error(L);
                return 0;
            }
            int n = lua_gettop(hl);
            if (n > 0)
            {
                lua_rawcheckstack(L, n);
                lua_xmove(hl, L, n);
            }
            return n;
        }
        int na = lua_gettop(L);
        if (na > 0)
        {
            lua_rawcheckstack(hl, na);
            lua_xmove(L, hl, na);
        }
        hl->baseCcalls++;
        int hs = lua_resume(hl, L, na);
        hl->baseCcalls--;
        if (hl->ci && hl->ci->top < hl->top)
            hl->ci->top = hl->top;
        if (L->status == LUA_YIELD || L->status == LUA_BREAK || L->status == SCHEDULED_REENTRY)
        {
            if (g_rq.count(hl))
                preq(L, hl);
            lua_rawcheckstack(L, 1);
            luaC_threadbarrier(L);
            L->top->value.gc = reinterpret_cast<GCObject*>(hl);
            L->top->tt = LUA_TTHREAD;
            ++L->top;
            g_pr[L] = lua_ref(L, -1);
            lua_pop(L, 1);
            g_ph[L] = hl;
            return lua_yield(L, 0);
        }
        hlclean(L, hl);
        if (hs != LUA_OK)
        {
            int ne = lua_gettop(hl);
            if (ne > 0)
            {
                lua_rawcheckstack(L, ne);
                lua_xmove(hl, L, ne);
            }
            else
                lua_pushstring(L, ("oth: cerr"));
            lua_error(L);
            return 0;
        }
        int n = lua_gettop(hl);
        if (n > 0)
        {
            lua_rawcheckstack(L, n);
            lua_xmove(hl, L, n);
        }
        return n;
    }

    inline int HC(lua_State* L, int status)
    {
        DWORD __exc = 0;
        __try { return HC_impl(L, status); }
        __except (__exc = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {}
        luaL_error(L, ("HC exception (0x%08X)"), __exc);
        return 0;
    }

    inline int api_hook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        Closure* original = clvalue(luaA_toobject(L, 1));
        Closure* hook = clvalue(luaA_toobject(L, 2));
        if (!original->isC)
            luaL_error(L, ("oth.hook: C closure expected"));
        if (hook->isC)
            luaL_error(L, ("oth.hook: Lua closure expected"));
        if (ihk(original))
            dclean(L, original);
        oclone(L, 1);
        Closure* oc = clvalue(luaA_toobject(L, -1));
        s_gc.prot(L, oc);
        lua_pop(L, 1);
        Closure* hp = owrap(L, 2);
        s_gc.prot(L, hp);
        g_oc[original] = oc;
        g_hk[original] = hp;
        if (!g_hf) g_hf = H;
        Shared::ExecutorClosures.insert(original);
        Shared::ExecutorClosures.insert(oc);
        original->c.f = H;
        original->c.cont = HC;
        opush(L, original);
        return 1;
    }

    inline int api_unhook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* c = clvalue(luaA_toobject(L, 1));
        if (!ihk(c))
        {
            lua_pushboolean(L, false);
            return 1;
        }
        dclean(L, c);
        lua_pushboolean(L, true);
        return 1;
    }

    inline int api_iht(lua_State* L)
    {
        lua_pushboolean(L, g_hp.count(L) > 0);
        return 1;
    }

    inline int api_got(lua_State* L)
    {
        auto i = g_hp.find(L);
        if (i == g_hp.end())
        {
            lua_pushnil(L);
            return 1;
        }
        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        L->top->value.gc = reinterpret_cast<GCObject*>(i->second);
        L->top->tt = LUA_TTHREAD;
        ++L->top;
        return 1;
    }

    inline int api_grc(lua_State* L)
    {
        auto i = g_cb.find(L);
        if (i == g_cb.end() || !i->second || !alive(i->second))
        {
            lua_pushnil(L);
            return 1;
        }
        lua_rawcheckstack(L, 2);
        luaC_threadbarrier(L);
        setclvalue(L, L->top++, i->second);
        lua_pushcclosure(L, oproxy, nullptr, 1);
        return 1;
    }

    inline int api_ih(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* c = clvalue(luaA_toobject(L, 1));
        lua_pushboolean(L, g_oc.count(c) > 0);
        return 1;
    }

    inline int api_gh(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        Closure* c = clvalue(luaA_toobject(L, 1));
        auto hi = g_hk.find(c);
        if (hi == g_hk.end() || !hi->second || !alive(hi->second))
        {
            lua_pushnil(L);
            return 1;
        }
        opush(L, hi->second);
        return 1;
    }

    inline int api_rh(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        Closure* o = clvalue(luaA_toobject(L, 1));
        Closure* h = clvalue(luaA_toobject(L, 2));
        if (h->isC)
            luaL_error(L, ("Lua closure expected"));
        if (g_oc.find(o) == g_oc.end())
            luaL_error(L, ("not hooked"));
        auto hi = g_hk.find(o);
        if (hi != g_hk.end())
            s_gc.unprot(L, hi->second);
        Closure* np = owrap(L, 2);
        s_gc.prot(L, np);
        g_hk[o] = np;
        lua_pushboolean(L, true);
        return 1;
    }

    inline int tc(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    inline bool IsHookedClosure(Closure* c)
    {
        return c && g_oc.count(c) > 0;
    }

    inline lua_CFunction GetOriginalCF(Closure* c)
    {
        if (!c) return nullptr;
        auto it = g_oc.find(c);
        if (it != g_oc.end() && it->second)
            return it->second->c.f;
        return nullptr;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        lua_newtable(L);
        Utils::AddTableFunction(L, ("hook"), api_hook);
        Utils::AddTableFunction(L, ("unhook"), api_unhook);
        Utils::AddTableFunction(L, ("get_root_callback"), api_grc);
        Utils::AddTableFunction(L, ("is_hook_thread"), api_iht);
        Utils::AddTableFunction(L, ("get_original_thread"), api_got);
        Utils::AddTableFunction(L, ("is_hooked"), api_ih);
        Utils::AddTableFunction(L, ("get_hook"), api_gh);
        Utils::AddTableFunction(L, ("replace_hook"), api_rh);
        lua_setglobal(L, ("oth"));
        Utils::AddFunction(L, ("trampoline_call"), tc);
    }

    inline void ResetLibrary()
    {
        s_pg.inv();
        g_hf = nullptr;
        g_oc.clear();
        g_hk.clear();
        g_hp.clear();
        g_cb.clear();
        g_ph.clear();
        g_pr.clear();
        g_rq.clear();
        g_pn.clear();
        g_orig.clear();
        g_fwd.clear();
        s_wm.clear();
        s_gc.drop();
    }
}
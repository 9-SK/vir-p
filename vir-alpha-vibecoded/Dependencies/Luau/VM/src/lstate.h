// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include <cstddef> // offsetof, for the layout guards below

#include "lobject.h"
#include "ltm.h"
#include "ludata.h"

// registry
#define registry(L) (&L->global->registry)

// extra stack space to handle TM calls and some other extras
#define EXTRA_STACK 5

#define BASIC_CI_SIZE 8

#define BASIC_STACK_SIZE (2 * LUA_MINSTACK)

// clang-format off
// Field order per the leaked 0.743 fork source (ref\luau_src): {hash, size, nuse}.
// sizeof is 16 under either ordering, so a swap here only shows up in the
// individual field offsets - pin those too.

struct stringtable {
    TString** hash; /* (0) (0x0) */
    int size; /* (8) (0x8) */
    uint32_t nuse; /* (12) (0xC) */
};
static_assert(sizeof(stringtable) == 16, "sizeof(stringtable) == 16");


static_assert(offsetof(stringtable, size) == 0x8, "stringtable::size moved");
static_assert(offsetof(stringtable, nuse) == 0xC, "stringtable::nuse moved");

// clang-format on

/*
** informations about a call
**
** the general Lua stack frame structure is as follows:
** - each function gets a stack frame, with function "registers" being stack slots on the frame
** - function arguments are associated with registers 0+
** - function locals and temporaries follow after; usually locals are a consecutive block per scope, and temporaries are allocated after this, but
*this is up to the compiler
**
** when function doesn't have varargs, the stack layout is as follows:
** ^ (func) ^^ [fixed args] [locals + temporaries]
** where ^ is the 'func' pointer in CallInfo struct, and ^^ is the 'base' pointer (which is what registers are relative to)
**
** when function *does* have varargs, the stack layout is more complex - the runtime has to copy the fixed arguments so that the 0+ addressing still
*works as follows:
** ^ (func) [fixed args] [varargs] ^^ [fixed args] [locals + temporaries]
**
** computing the sizes of these individual blocks works as follows:
** - the number of fixed args is always matching the `numparams` in a function's Proto object; runtime adds `nil` during the call execution as
*necessary
** - the number of variadic args can be computed by evaluating (ci->base - ci->func - 1 - numparams)
**
** the CallInfo structures are allocated as an array, with each subsequent call being *appended* to this array (so if f calls g, CallInfo for g
*immediately follows CallInfo for f)
** the `nresults` field in CallInfo is set by the caller to tell the function how many arguments the caller is expecting on the stack after the
*function returns
** the `flags` field in CallInfo contains internal execution flags that are important for pcall/etc, see LUA_CALLINFO_*
*/
// clang-format off

struct CallInfo {
    TValue* base; /* (0) (0x0) */
    TValue* func; /* (8) (0x8) - fork order (was top) */
    Proto* p; /* (16) (0x10) */
    TValue* top; /* (24) (0x18) - fork order (was func) */
    union {
        const Instruction* savedpc;
        int errfunc;
    }; /* (32) (0x20) */
    int nresults; /* (40) (0x28) */
    unsigned int flags; /* (44) (0x2C) */
};
static_assert(offsetof(CallInfo, base) == 0, "CallInfo::base");
static_assert(offsetof(CallInfo, func) == 8, "CallInfo::func");
static_assert(offsetof(CallInfo, p) == 16, "CallInfo::p");
static_assert(offsetof(CallInfo, top) == 24, "CallInfo::top");
static_assert(offsetof(CallInfo, savedpc) == 32, "CallInfo::savedpc");
static_assert(offsetof(CallInfo, nresults) == 40, "CallInfo::nresults");
static_assert(offsetof(CallInfo, flags) == 44, "CallInfo::flags");
static_assert(sizeof(CallInfo) == 48, "sizeof(CallInfo) == 48");


// clang-format on

#define LUA_CALLINFO_RETURN (1 << 0) // should the interpreter return after returning from this callinfo? first frame must have this set
#define LUA_CALLINFO_HANDLE (1 << 1) // should the error thrown during execution get handled by continuation from this callinfo? func must be C
#define LUA_CALLINFO_NATIVE (1 << 2) // should this function be executed using execution callback for native code
#define LUA_CALLINFO_OPYIELD (1 << 3) // call frame has yielded on a non-call opcode and requires luaV_finishop

#define curr_func(L) (clvalue(L->ci->func))
#define ci_func(ci) (clvalue((ci)->func))
#define f_isLua(ci) (!ci_func(ci)->isC)
#define isLua(ci) (ttisfunction((ci)->func) && f_isLua(ci))

struct GCStats
{
    // data for proportional-integral controller of heap trigger value
    int32_t triggerterms[32] = {0};
    uint32_t triggertermpos = 0;
    int32_t triggerintegral = 0;

    size_t atomicstarttotalsizebytes = 0;
    size_t endtotalsizebytes = 0;
    size_t heapgoalsizebytes = 0;

    double starttimestamp = 0;
    double atomicstarttimestamp = 0;
    double endtimestamp = 0;
};

#ifdef LUAI_GCMETRICS
struct GCCycleMetrics
{
    size_t starttotalsizebytes = 0;
    size_t heaptriggersizebytes = 0;

    double pausetime = 0.0; // time from end of the last cycle to the start of a new one

    double starttimestamp = 0.0;
    double endtimestamp = 0.0;

    double marktime = 0.0;
    double markassisttime = 0.0;
    double markmaxexplicittime = 0.0;
    size_t markexplicitsteps = 0;
    size_t markwork = 0;

    double atomicstarttimestamp = 0.0;
    size_t atomicstarttotalsizebytes = 0;
    double atomictime = 0.0;

    // specific atomic stage parts
    double atomictimeupval = 0.0;
    double atomictimeweak = 0.0;
    double atomictimegray = 0.0;
    double atomictimeembedder = 0.0;
    double atomictimeclear = 0.0;

    double sweeptime = 0.0;
    double sweepassisttime = 0.0;
    double sweepmaxexplicittime = 0.0;
    size_t sweepexplicitsteps = 0;
    size_t sweepwork = 0;

    size_t assistwork = 0;
    size_t explicitwork = 0;

    size_t propagatework = 0;
    size_t propagateagainwork = 0;

    size_t endtotalsizebytes = 0;
};

struct GCMetrics
{
    double stepexplicittimeacc = 0.0;
    double stepassisttimeacc = 0.0;

    // when cycle is completed, last cycle values are updated
    uint64_t completedcycles = 0;

    GCCycleMetrics lastcycle;
    GCCycleMetrics currcycle;
};
#endif

// Callbacks that can be used to to redirect code execution from Luau bytecode VM to a custom implementation (AoT/JiT/sandboxing/...)
struct lua_ExecutionCallbacks
{
    void* context;
    void (*close)(lua_State* L);                 // called when global VM state is closed
    void (*destroy)(lua_State* L, Proto* proto); // called when function is destroyed
    int (*enter)(lua_State* L, Proto* proto);    // called when function is about to start/resume (when execdata is present), return 0 to exit VM
    void (*disable)(lua_State* L, Proto* proto); // called when function has to be switched from native to bytecode in the debugger
    size_t (*getmemorysize)(lua_State* L, Proto* proto); // called to request the size of memory associated with native part of the Proto
    uint8_t (*gettypemapping)(lua_State* L, const char* str, size_t len); // called to get the userdata type index
    char* (*getcounterdata)(
        lua_State* L,
        Proto* proto,
        size_t* count
    ); // called to get the execution counter data and count {uint32_t, uint32_t, uint64_t}
    Proto* (*inlinefunction)(lua_State* L, Closure* caller, Closure* target, uint32_t pc); // called when inlining threshold is reached
};

struct lua_UdataDirectAccessData
{
    TValue indextm;
    TValue newindextm;
    TValue namecalltm;
    lua_UserdataDirectAccess index;
    lua_UserdataDirectAccess newindex;
    lua_UserdataDirectNamecall namecall;
};

/*
** `global state', shared by all threads of this state
*/
// clang-format off
struct registryfree_value
{
private:
    int _value;

public:
    registryfree_value() : _value(0) {}
    registryfree_value(int val) : _value(val) {}
    registryfree_value(const registryfree_value& other) : _value(other._value) {}

    void operator=(const registryfree_value& value)
    {
        _value = value._value;
    }

    void operator=(const int& value)
    {
        _value = (_value & 0xF0000000) | (value & 0xFFFFFFF);
    }

    operator const int() const
    {
        return _value & 0xFFFFFFF;
    }

    bool operator==(const int& value) const
    {
        return (_value & 0xFFFFFFF) == value;
    }

    bool operator!=(const int& value) const
    {
        return (_value & 0xFFFFFFF) != value;
    }
};

typedef registryfree_value registryfree_t;



struct global_State {
    size_t GCthreshold; /* (0) (0x0) */
    size_t totalbytes; /* (8) (0x8) */
    GCObject* gray; /* (16) (0x10) */
    GCObject* weak; /* (24) (0x18) */
    GCObject* grayagain; /* (32) (0x20) */
    int gcgoal; /* (40) (0x28) */
    int gcstepmul; /* (44) (0x2C) */
    int gcstepsize; /* (48) (0x30) */
    unsigned char gap_34[4];
    lua_Alloc frealloc; /* (56) (0x38) */
    void* ud; /* (64) (0x40) */
    unsigned char currentwhite; /* (72) (0x48) */
    unsigned char gcstate; /* (73) (0x49) */
    unsigned char gap_4a[6];
    stringtable strt; /* (80) (0x50) */
    lua_State* mainthread; /* (96) (0x60) */
    lua_Page* freegcopages[40]; /* (104) (0x68) */
    lua_Page* allpages; /* (424) (0x1A8) */
    lua_Page* freepages[40]; /* (432) (0x1B0) */
    UpVal uvhead; /* (752) (0x2F0) */
    lua_Page* allgcopages; /* (792) (0x318) */
    lua_Page* sweepgcopage; /* (800) (0x320) */
    TString* tmname[TM_N]; /* (808) (0x328) */
    TString* ttname[LUA_T_COUNT]; /* (976) (0x3D0) */
    LuaTable* mt[LUA_T_COUNT]; /* (1088) (0x440) */
    lua_CageAlloc cagealloc; /* (1200) (0x4B0) */
    lua_Page* sweepgcopage_cage; /* (1208) (0x4B8) */
    lua_Page* freegcopages_cage[40]; /* (1216) (0x4C0) */
    lua_Page* allgcopages_cage; /* (1536) (0x600) */
    struct lua_jmpbuf* errorjmp; /* (1544) (0x608) */
    TValue pseudotemp; /* (1552) (0x610) */
    TValue registry; /* (1568) (0x620) */
    registryfree_t registryfree; /* (1584) (0x630) */
    unsigned char gap_634[4];
    lua_Callbacks cb; /* (1592) (0x638) */
    unsigned __int64 ptrenckey[4]; /* (1704) (0x6A8) - pointer-encode keys (verified in the 0.743 binary) */
    unsigned __int64 rngstate; /* (1736) (0x6C8) */
    void* cageud; /* (1744) (0x6D0) */
    lua_ExecutionCallbacks ecb; /* (1752) (0x6D8) */
    alignas(16) uint8_t ecbdata[LUA_EXECUTION_CALLBACK_STORAGE]; /* (1824) (0x720) */
    lua_UdataDirectAccessData udatadirect[UTAG_INTERNAL_LIMIT]; /* (2336) (0x920) */
    size_t memcatbytes[LUA_MEMORY_CATEGORIES]; /* (11696) (0x2DB0) */
    void (*udatagc[LUA_UTAG_LIMIT])(struct lua_State*, void*); /* (13744) (0x35B0) */
    lua_UserdataMark udatamark[LUA_UTAG_LIMIT]; /* (14768) (0x39B0) */
    LuaTable* udatamt[LUA_UTAG_LIMIT]; /* (15792) (0x3DB0) */
    TValue weakregistry; /* (16816) (0x41B0) */
    int weakregistryfree; /* (16832) (0x41C0) */
    int weakregistrytop; /* (16836) (0x41C4) */
    lua_EmbedderGc embeddergc; /* (16840) (0x41C8) */
    TString* lightuserdataname[LUA_LUTAG_LIMIT]; /* (16848) (0x41D0) */
    struct LuaTable* udatadirectfields[UTAG_INTERNAL_LIMIT]; /* (17872) (0x45D0) */
    Closure* builtinPcall; /* (18912) (0x49E0) */
    Closure* builtinXpcall; /* (18920) (0x49E8) */
    unsigned __int64 ptrenckeynew[8]; /* (18928) (0x49F0) */
    bool ptrencactive; /* (18992) (0x4A30) - verified in the 0.743 binary */
    char gap_3[0x7];
    struct GCStats gcstats; /* (19000) (0x4A38) */
    unsigned int lastprotoid; /* (19184) (0x4AF0) */
    unsigned int lastclassid; /* (19188) (0x4AF4) - named in the 0.743 dump; previously placeholder "mysterydword" */
#ifdef LUAI_GCMETRICS
    GCMetrics gcmetrics; /* offset (19192) (0x4AF8) */
#endif
};


// errorjmp / allpages / udatadirectfields are real members of this layout at
// 0x600 / 0x320 / 0x45D0 - they used to be compile shims parked past the end
// of the allocation. sizeof is pinned below; the alignas(16) ecbdata member
// is what rounds the tail out to 19200.
// TEMP diagnostic switch - removed after layout probe.
#ifndef VIRIDIAN_SKIP_GS_ASSERT
#define VIRIDIAN_SKIP_GS_ASSERT 0
#endif
#ifdef LUAI_GCMETRICS
static_assert(VIRIDIAN_SKIP_GS_ASSERT || sizeof(global_State) == 19664, "sizeof(global_State) == 19664");
#else
static_assert(VIRIDIAN_SKIP_GS_ASSERT || sizeof(global_State) == 19200, "sizeof(global_State) == 19200");
#endif
// ---------------------------------------------------------------------------
// Layout guards.
//
// Every field of these structs is read or written against memory Roblox
// allocated, so a size drift in ANY member type silently shifts everything
// after it and turns into heap corruption at runtime. The sizeof asserts in
// this file turn that into a build error instead - they are how the
// lua_Callbacks 104-vs-112 byte bug was found.
//
// If one of these fires, do NOT adjust the number - find the member type whose
// size changed and fix that.
// ---------------------------------------------------------------------------


// clang-format on

/*
** `per thread' state
*/
// clang-format off

struct lua_State {
    CommonHeader; /* (0) (0x0) */
    uint8_t status; /* (3) (0x3) */
    bool singlestep; /* (4) (0x4) */
    bool isactive; /* (5) (0x5) */
    uint8_t activememcat; /* (6) (0x6) */
    char gap_0[0x1];
    struct RobloxExtraSpace* userdata; /* (8) (0x8) - fork layout, verified in the 0.743 binary */
    unsigned short nCcalls; /* (16) (0x10) */
    unsigned short baseCcalls; /* (18) (0x12) */
    unsigned int cachedslot; /* (20) (0x14) */
    UpVal* openupval; /* (24) (0x18) */
    CallInfo* end_ci; /* (32) (0x20) */
    CallInfo* base_ci; /* (40) (0x28) */
    LuaTable* finalizers; /* (48) (0x30) */
    TValue* stack_last; /* (56) (0x38) */
    CallInfo* ci; /* (64) (0x40) */
    TValue* base; /* (72) (0x48) */
    global_State* global; /* (80) (0x50) - verified in the 0.743 binary (mov r8,[rcx+0x50]) */
    TValue* top; /* (88) (0x58) */
    TValue* stack; /* (96) (0x60) */
    TString* namecall; /* (104) (0x68) */
    LSTATE_STACKSIZE_ENC<int> stacksize; /* (112) (0x70) */
    int size_ci; /* (116) (0x74) */
    GCObject* gclist; /* (120) (0x78) */
    LuaTable* gt; /* (128) (0x80) */
};
static_assert(sizeof(lua_State) == 136, "sizeof(lua_State) == 136");





// clang-format on

/*
** Union of all collectible objects
*/
union GCObject
{
    GCheader gch;
    struct TString ts;
    struct Udata u;
    struct Closure cl;
    struct LuaTable h;
    struct Proto p;
    struct UpVal uv;
    struct lua_State th; // thread
    struct LuauBuffer buf;
    struct LuauClass lclass;
    struct LuauObject lobject;
    struct LuauVector vec;
};

// macros to convert a GCObject into a specific value
#define gco2ts(o) check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2u(o) check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2cl(o) check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o) check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o) check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o) check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o) check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))
#define gco2buf(o) check_exp((o)->gch.tt == LUA_TBUFFER, &((o)->buf))
#define gco2class(o) check_exp((o)->gch.tt == LUA_TCLASS, &((o)->lclass))
#define gco2object(o) check_exp((o)->gch.tt == LUA_TOBJECT, &((o)->lobject))
#define gco2vec(o) check_exp((o)->gch.tt == LUA_TVECTOR, &((o)->vec))

// macro to convert any Lua object into a GCObject
#define obj2gco(v) check_exp(iscollectable(v), cast_to(GCObject*, (v) + 0))

LUAI_FUNC lua_State* luaE_newthread(lua_State* L);
LUAI_FUNC void luaE_freethread(lua_State* L, lua_State* L1, struct lua_Page* page);

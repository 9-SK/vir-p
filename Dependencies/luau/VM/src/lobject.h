// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include <cstddef> // offsetof, for the layout guards

#include "lua.h"
#include "lcommon.h"

/*
** Union of all collectible objects
*/
typedef union GCObject GCObject;

/*
** Common Header for all collectible objects (in macro form, to be included in other objects)
*/
// clang-format off
// Field order per ref\luau_src (the leaked 0.743 fork) and the verified
// ledger (ref\analysis\engine_confirmed_layouts.md):
//     tt (0), memcat (1), marked (2)
// The previous order (tt, marked, memcat) came from the old "0.7xx dump"
// and silently flipped the GC color byte and the memory-category byte on
// every object our VM creates - same size, so no offset check ever failed.
#define CommonHeader \
    uint8_t tt; \
    uint8_t memcat; \
    uint8_t marked
// clang-format on

/*
** Common header in struct form
*/
typedef struct GCheader
{
    CommonHeader;
} GCheader;

/*
** Union of all Lua values
*/
typedef union
{
    GCObject* gc;
    void* p;
    double n;
    int b;
    int64_t l;
    float v[2]; // v[0], v[1] live here; v[2] lives in TValue::extra
} Value;

/*
** Tagged Values
*/

typedef struct lua_TValue
{
    Value value;
    int extra[LUA_EXTRA_SIZE];
    int tt;
} TValue;

#if LUA_VECTOR_SIZE == 4
#define condvector4(vec4expr, vec3expr) vec4expr
#else
#define condvector4(vec4expr, vec3expr) vec3expr
#endif

#if LUA_VECTOR_DOUBLE == 1
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecdoubleexpr
#else
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecfloatexpr
#endif

// Macros to test type
#define ttisnil(o) (ttype(o) == LUA_TNIL)
#define ttisnumber(o) (ttype(o) == LUA_TNUMBER)
#define ttisinteger(o) (ttype(o) == LUA_TINTEGER)
#define ttisstring(o) (ttype(o) == LUA_TSTRING)
#define ttistable(o) (ttype(o) == LUA_TTABLE)
#define ttisfunction(o) (ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o) (ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o) (ttype(o) == LUA_TUSERDATA)
#define ttisthread(o) (ttype(o) == LUA_TTHREAD)
#define ttisbuffer(o) (ttype(o) == LUA_TBUFFER)
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA)
#define ttisvector(o) (ttype(o) == LUA_TVECTOR)
#define ttisupval(o) (ttype(o) == LUA_TUPVAL)
#define ttisclass(o) (ttype(o) == LUA_TCLASS)
#define ttisobject(o) (ttype(o) == LUA_TOBJECT)

// Macros to access values
#define ttype(o) ((o)->tt)
#define gcvalue(o) check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o) check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o) check_exp(ttisnumber(o), (o)->value.n)
#define lvalue(o) check_exp(ttisinteger(o), (o)->value.l)
#define vvalue(o) check_exp(ttisvector(o), condvectordouble((o)->value.gc->vec.v, (o)->value.v))
#define tsvalue(o) check_exp(ttisstring(o), &(o)->value.gc->ts)
#define uvalue(o) check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define clvalue(o) check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o) check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o) check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o) check_exp(ttisthread(o), &(o)->value.gc->th)
#define bufvalue(o) check_exp(ttisbuffer(o), &(o)->value.gc->buf)
#define upvalue(o) check_exp(ttisupval(o), &(o)->value.gc->uv)
#define classvalue(o) check_exp(ttisclass(o), &(o)->value.gc->lclass)
#define objectvalue(o) check_exp(ttisobject(o), &(o)->value.gc->lobject)

#define l_isfalse(o) (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

#define lightuserdatatag(o) check_exp(ttislightuserdata(o), (o)->extra[0])

// Internal tags used by the VM
#define LU_TAG_ITERATOR LUA_UTAG_LIMIT

/*
** for internal debug only
*/
#define checkconsistency(obj) LUAU_ASSERT(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g, obj) LUAU_ASSERT(!iscollectable(obj) || ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

// Macros to set values
#define setnilvalue(obj) ((obj)->tt = LUA_TNIL)

#define setnvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.n = (x); \
        i_o->tt = LUA_TNUMBER; \
    }

#define setlvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.l = (x); \
        i_o->tt = LUA_TINTEGER; \
    }

#if LUA_VECTOR_DOUBLE == 1
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        LuauVector* i_vec = luaVec_newvector((L), double(x), double(y), double(z), condvector4(double(w), 0)); \
        i_o->value.gc = cast_to(GCObject*, i_vec); \
        i_o->tt = LUA_TVECTOR; \
        checkliveness((L)->global, i_o); \
    }
#else
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        float* i_v = i_o->value.v; \
        i_v[0] = float(x); \
        i_v[1] = float(y); \
        i_v[2] = float(z); \
        condvector4(i_v[3] = float(w), (void)(w)); \
        i_o->tt = LUA_TVECTOR; \
    }
#endif

#define setpvalue(obj, x, tag) \
    { \
        TValue* i_o = (obj); \
        i_o->value.p = (x); \
        i_o->extra[0] = (tag); \
        i_o->tt = LUA_TLIGHTUSERDATA; \
    }

#define setbvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.b = (x); \
        i_o->tt = LUA_TBOOLEAN; \
    }

#define setsvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TSTRING; \
        checkliveness(L->global, i_o); \
    }

#define setuvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUSERDATA; \
        checkliveness(L->global, i_o); \
    }

#define setthvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTHREAD; \
        checkliveness(L->global, i_o); \
    }

#define setbufvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TBUFFER; \
        checkliveness(L->global, i_o); \
    }

#define setclvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TFUNCTION; \
        checkliveness(L->global, i_o); \
    }

#define sethvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTABLE; \
        checkliveness(L->global, i_o); \
    }

#define setptvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TPROTO; \
        checkliveness(L->global, i_o); \
    }

#define setupvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUPVAL; \
        checkliveness(L->global, i_o); \
    }

#define setobj(L, obj1, obj2) \
    { \
        const TValue* o2 = (obj2); \
        TValue* o1 = (obj1); \
        *o1 = *o2; \
        checkliveness(L->global, o1); \
    }

#define setclassvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TCLASS; \
        checkliveness(L->global, i_o); \
    }


#define setobjectvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TOBJECT; \
        checkliveness(L->global, i_o); \
    }

/*
** different types of sets, according to destination
*/

// to stack
#define setobj2s setobj
// from table to same table (no barrier)
#define setobjt2t setobj
// to table (needs barrier)
#define setobj2t setobj
// to new object (no barrier)
#define setobj2n setobj
// to class instance or static member (needs barrier)
#define setobj2class setobj

#define setttype(obj, tt) (ttype(obj) = (tt))

#define iscollectable(o) (ttype(o) >= LUA_TSTRING)

// wrapper for returning userdata properties directly
struct DirectFieldResult
{
    lua_State* L;
    TValue* slot;
};

typedef TValue* StkId; // index to stack elements

/*
** String headers for string table
*/

struct TString {
    CommonHeader; /* (0) (0x0) */
    uint16_t atomflag; /* (4) (0x4) */
    int16_t atom; /* (6) (0x6) */
    TString* next; /* (8) (0x8) */
    TSTRING_HASH_ENC<unsigned int> hash; /* (16) (0x10) */
    unsigned int len; /* (20) (0x14) */
    char data[1]; /* (24) (0x18) */
};
static_assert(sizeof(TString) == 32, "sizeof(TString) == 32");



#define getstr(ts) (ts)->data
#define svalue(o) getstr(tsvalue(o))

typedef struct TKey
{
    ::Value value;
    int extra[LUA_EXTRA_SIZE];
    unsigned tt : 4;
    int next : 28; // for chaining
} TKey;

typedef struct LuaNode
{
    TValue val;
    TKey key;
} LuaNode;

struct LuaTable {
    CommonHeader; /* (0) (0x0) */
    uint8_t lsizenode; /* (3) (0x3) */
    uint8_t readonly; /* (4) (0x4) */
    uint8_t nodemask8; /* (5) (0x5) */
    uint8_t tmcache; /* (6) (0x6) */
    uint8_t safeenv; /* (7) (0x7) */
    int sizearray; /* (8) (0x8) */
    union {
        int lastfree;
        int aboundary;
    }; /* (12) (0xC) */
    LuaTable* metatable; /* (16) (0x10) - fork order (was 0x20) */
    TValue* array; /* (24) (0x18) */
    GCObject* gclist; /* (32) (0x20) - fork order (was 0x28) */
    LuaNode* node; /* (40) (0x28) - fork order (was 0x10) */
};
static_assert(offsetof(LuaTable, tmcache) == 6, "LuaTable::tmcache");
static_assert(offsetof(LuaTable, readonly) == 4, "LuaTable::readonly");
static_assert(offsetof(LuaTable, lsizenode) == 3, "LuaTable::lsizenode");
static_assert(offsetof(LuaTable, nodemask8) == 5, "LuaTable::nodemask8");
static_assert(offsetof(LuaTable, safeenv) == 7, "LuaTable::safeenv");
static_assert(offsetof(LuaTable, metatable) == 16, "LuaTable::metatable");
static_assert(offsetof(LuaTable, array) == 24, "LuaTable::array");
static_assert(offsetof(LuaTable, gclist) == 32, "LuaTable::gclist");
static_assert(offsetof(LuaTable, node) == 40, "LuaTable::node");
static_assert(sizeof(LuaTable) == 48, "sizeof(LuaTable) == 48");

// 0.743 note: UDATA_META_ENC is absent from the dump's macro list. Nothing in
// the module dereferences this field; layout kept as-is (tag@3, len@4,
// metatable@8, data@16) - re-verify before any new consumer appears.
struct Udata {
    CommonHeader; /* (0) (0x0) */
    unsigned char tag; /* (3) (0x3) */
    int len; /* (4) (0x4) */
    UDATA_META_ENC<struct LuaTable*> metatable; /* (8) (0x8) */
    char data[1]; /* (16) (0x10) */
};
static_assert(sizeof(Udata) == 24, "sizeof(Udata) == 24");


typedef struct LuauBuffer
{
    CommonHeader;

    unsigned int len;

    alignas(8) char data[1];
} Buffer;

typedef struct LuauVector
{
    CommonHeader;
    // 1 byte padding

    LUA_VECTOR_TYPE v[LUA_VECTOR_SIZE];
} LuauVector;

enum FeedbackVectorSlotKind
{
    CALL_TARGET
};

struct FeedbackVectorSlot
{
    FeedbackVectorSlotKind kind;

    union
    {
        struct
        {
            uint32_t pc;
            uint32_t proto;
            uint32_t hits;
        } call_target;
    };
};

/*
** Function Prototypes
*/
// LocVar is defined further down, but Proto::locvars needs the name here.
// Without this the template argument fails to parse and every later use of
// Closure/Proto reports bogus "is not a member" errors.
struct LocVar;

struct Proto {
    CommonHeader; /* (0) (0x0) */
    unsigned char is_vararg; /* (3) (0x3) */
    unsigned char numparams; /* (4) (0x4) */
    unsigned char nups; /* (5) (0x5) */
    unsigned char flags; /* (6) (0x6) */
    unsigned char maxstacksize; /* (7) (0x7) */
    GCObject* gclist; /* (8) (0x8) */
    PROTO_DEBUGNAME_ENC<TString*> debugname; /* (16) (0x10) */
    Proto** p; /* (24) (0x18) */
    TValue* k; /* (32) (0x20) */
    unsigned int* code; /* (40) (0x28) */
    PROTO_DEBUGINSN_ENC<unsigned char*> debuginsn; /* (48) (0x30) */
    PROTO_ABSLINEINFO_ENC<int*> abslineinfo; /* (56) (0x38) */
    PROTO_USERDATA_ENC<uint64_t*> userdata; /* (64) (0x40) */
    PROTO_SOURCE_ENC<TString*> source; /* (72) (0x48) */
    PROTO_LOCVARS_ENC<struct LocVar*> locvars; /* (80) (0x50) */
    unsigned int* codeentry; /* (88) (0x58) */
    void* execdata; /* (96) (0x60) */
    uintptr_t exectarget; /* (104) (0x68) */
    PROTO_LINEINFO_ENC<unsigned char*> lineinfo; /* (112) (0x70) */
    PROTO_UPVALUES_ENC<TString**> upvalues; /* (120) (0x78) */
    PROTO_TYPEINFO_ENC<unsigned char*> typeinfo; /* (128) (0x80) */
    int sizetypeinfo; /* (136) (0x88) */
    int sizelineinfo; /* (140) (0x8C) */
    int sizeupvalues; /* (144) (0x90) */
    int sizecode; /* (148) (0x94) */
    int sizek; /* (152) (0x98) */
    int sizep; /* (156) (0x9C) */
    int linedefined; /* (160) (0xA0) */
    int linegaplog2; /* (164) (0xA4) */
    int bytecodeid; /* (168) (0xA8) */
    int sizelocvars; /* (172) (0xAC) */
    struct FeedbackVectorSlot* feedbackvec; /* (176) (0xB0) */
    int feedbackvecsize; /* (184) (0xB8) */
    int funid; /* (188) (0xBC) */
    Proto* optimized; /* (192) (0xC0) */
    Proto* deoptimized; /* (200) (0xC8) */
    uint64_t cost; /* (208) (0xD0) */
};
static_assert(offsetof(Proto, is_vararg) == 3, "Proto::is_vararg");
static_assert(offsetof(Proto, numparams) == 4, "Proto::numparams");
static_assert(offsetof(Proto, nups) == 5, "Proto::nups");
static_assert(offsetof(Proto, flags) == 6, "Proto::flags");
static_assert(offsetof(Proto, maxstacksize) == 7, "Proto::maxstacksize");
static_assert(offsetof(Proto, gclist) == 0x08, "Proto::gclist");
static_assert(offsetof(Proto, debugname) == 0x10, "Proto::debugname");
static_assert(offsetof(Proto, p) == 0x18, "Proto::p");
static_assert(offsetof(Proto, k) == 0x20, "Proto::k");
static_assert(offsetof(Proto, code) == 0x28, "Proto::code");
static_assert(offsetof(Proto, debuginsn) == 0x30, "Proto::debuginsn");
static_assert(offsetof(Proto, abslineinfo) == 0x38, "Proto::abslineinfo");
static_assert(offsetof(Proto, userdata) == 0x40, "Proto::userdata");
static_assert(offsetof(Proto, source) == 0x48, "Proto::source");
static_assert(offsetof(Proto, locvars) == 0x50, "Proto::locvars");
static_assert(offsetof(Proto, codeentry) == 0x58, "Proto::codeentry");
static_assert(offsetof(Proto, execdata) == 0x60, "Proto::execdata");
static_assert(offsetof(Proto, exectarget) == 0x68, "Proto::exectarget");
static_assert(offsetof(Proto, lineinfo) == 0x70, "Proto::lineinfo");
static_assert(offsetof(Proto, upvalues) == 0x78, "Proto::upvalues");
static_assert(offsetof(Proto, typeinfo) == 0x80, "Proto::typeinfo");
static_assert(sizeof(Proto) == 0xd8, "sizeof(Proto) == 0xd8");


// clang-format on

typedef struct LocVar
{
    TString* varname;
    int startpc; // first point where variable is active
    int endpc;   // first point where variable is dead
    uint8_t reg; // register slot, relative to base, where variable is stored
} LocVar;

/*
** Upvalues
*/

typedef struct UpVal
{
    CommonHeader;
    uint8_t markedopen; // set if reachable from an alive thread (only valid during atomic)

    // 4 byte padding (x64)

    TValue* v; // points to stack or to its own value
    union
    {
        TValue value; // the value (when closed)
        struct
        {
            // global double linked list (when open)
            struct UpVal* prev;
            struct UpVal* next;

            // thread linked list (when open)
            struct UpVal* threadnext;
        } open;
    } u;
} UpVal;

#define upisopen(up) ((up)->v != &(up)->u.value)

/*
** Closures
*/

struct Closure {
    CommonHeader; /* offset (0) (0x0) */
    unsigned char isC; /* offset (3) (0x3) */
    unsigned char preload; /* offset (4) (0x4) */
    unsigned char nupvalues; /* offset (5) (0x5) - fork order (was stacksize) */
    unsigned char stacksize; /* offset (6) (0x6) - fork order (was nupvalues) */
    char gap_0[0x1];
    GCObject* gclist; /* offset (8) (0x8) */
    LuaTable* env; /* offset (16) (0x10) */
    union {
        struct {
            lua_CFunction f; /* (0) (0x0) */
            TString* debugname; /* (8) (0x8) - fork order (was cont) */
            CLOSURE_CONT_ENC<lua_Continuation> cont; /* (16) (0x10) - fork order (was debugname) */
            TValue upvals[1]; /* (24) (0x18) */
        } c;
        struct {
            Proto* p; /* (0) (0x0) */
            TValue uprefs[1]; /* (8) (0x8) */
        } l;
    };
};
static_assert(offsetof(Closure, isC) == 3, "Closure::isC");
static_assert(offsetof(Closure, preload) == 4, "Closure::preload");
static_assert(offsetof(Closure, nupvalues) == 5, "Closure::nupvalues");
static_assert(offsetof(Closure, stacksize) == 6, "Closure::stacksize");
static_assert(offsetof(Closure, c.f) == 24, "Closure::c.f");
static_assert(offsetof(Closure, c.debugname) == 32, "Closure::c.debugname");
static_assert(offsetof(Closure, c.cont) == 40, "Closure::c.cont");
static_assert(sizeof(Closure) == 64, "sizeof(Closure) == 64");


#define iscfunction(o) (ttype(o) == LUA_TFUNCTION && clvalue(o)->isC)
#define isLfunction(o) (ttype(o) == LUA_TFUNCTION && !clvalue(o)->isC)

/*
** Tables
*/

// copy a value into a key
#define setnodekey(L, node, obj) \
    { \
        LuaNode* n_ = (node); \
        const TValue* i_o = (obj); \
        n_->key.value = i_o->value; \
        memcpy(n_->key.extra, i_o->extra, sizeof(n_->key.extra)); \
        n_->key.tt = i_o->tt; \
        checkliveness(L->global, i_o); \
    }

// copy a value from a key
#define getnodekey(L, obj, node) \
    { \
        TValue* i_o = (obj); \
        const LuaNode* n_ = (node); \
        i_o->value = n_->key.value; \
        memcpy(i_o->extra, n_->key.extra, sizeof(i_o->extra)); \
        i_o->tt = n_->key.tt; \
        checkliveness(L->global, i_o); \
    }

// clang-format off
// clang-format on

typedef struct LuauClass
{
    CommonHeader;

    GCObject* gclist;

    TString* name;

    unsigned int classid;
    unsigned char gap_1c[12];

    // Mapping from offset to static members (only methods for now).
    TValue* staticmembers;

    // Mapping from member name to offset.
    // For static members, subtracting numberofinstancemembers from the offset gives the actual index into staticmembers.
    LuaTable* memberstooffset;

    // Mapping from offset to member name. Instance member offsets are stored before static member offsets.
    TString** offsettomember;

    // Metatable for this *class object*. At time of writing this only contains
    // __call, but we may add more metamethods to class objects in the future.
    LuaTable* metatable;

    // Metatable for instances of this class. NULL until the first metamethod
    // is added via luaR_addclassmember.
    LuaTable* instancemetatable;

    // Number of instance members that we expect instances of this class object
    // to have.
    uint32_t numberofinstancemembers;

    // Total number of members that we expect this class object to have between
    // instance and static members.
    //
    // We store this number as an optimization. It's pretty rare that we need
    // to reference the specific number of static members, but it's very common
    // to reference the total number of members (for validating hot paths in
    // the interpreter) and the number of instance members (branching on
    // instance or static members, creating class instances).
    uint32_t numberofallmembers;
    unsigned char gap_58[8];

} LuauClass;

typedef struct LuauObject
{
    CommonHeader;

    GCObject* gclist;

    // The class object that this value is an instance of.
    LuauClass* lclass;

    // The number of members that this instance contains. We need this in order
    // to free ourselves if we got swept in the same GC cycle as our class
    // pointer.
    uint32_t numberofmembers;

    // The fields of this instance.
    TValue* members;

} LuauObject;

/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s, size) (check_exp((size & (size - 1)) == 0, (cast_to(int, (s) & ((size) - 1)))))

#define twoto(x) ((int)(1 << (x)))
#define sizenode(t) (twoto((t)->lsizenode))

#define luaO_nilobject reinterpret_cast<TValue*>(Offsets::Luau::LuaO_NilObject)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x) (luaO_log2((x) - 1) + 1)

LUAI_FUNC int luaO_log2(unsigned int x);
LUAI_FUNC int luaO_rawequalObj(const TValue* t1, const TValue* t2);
LUAI_FUNC int luaO_rawequalKey(const TKey* t1, const TValue* t2);
LUAI_FUNC int luaO_str2d(const char* s, double* result);
LUAI_FUNC int luaO_str2l(const char* s, int64_t* result, int base = 10);
LUAI_FUNC const char* luaO_pushvfstring(lua_State* L, const char* fmt, va_list argp);
LUAI_FUNC const char* luaO_pushfstring(lua_State* L, const char* fmt, ...);
LUAI_FUNC const char* luaO_chunkid(char* buf, size_t buflen, const char* source, size_t srclen);


// Size guards for the member types that global_State / LuaTable are built from.
// Three separate layout bugs (RobloxExtraSpace, lua_Callbacks, stringtable)
// were member types inherited from older headers rather than re-derived, and
// two of them had the right sizeof so the offsetof asserts never fired. Pin
// sizes as well as offsets.
//
// TValue 16 - global_State has pseudotemp at 0x610 and registry at 0x620.
// LuaNode 32 - TValue + TKey; LuaTable::node is indexed by this stride, so a
// wrong size sends every hash lookup to the wrong slot.
static_assert(sizeof(TValue) == 0x10, "TValue must be 16 bytes");
static_assert(sizeof(TKey) == 0x10, "TKey must be 16 bytes");
static_assert(sizeof(LuaNode) == 0x20, "LuaNode must be 32 bytes");
static_assert(offsetof(LuaNode, val) == 0x0, "LuaNode::val moved");
static_assert(offsetof(LuaNode, key) == 0x10, "LuaNode::key moved");

// LuaTable layout - mirrors the client's dump-locked layout byte for byte; the
// flag bytes are not in upstream Luau order, do not "fix" them against upstream
// sources. Actual struct: tmcache 0x03, readonly 0x04, lsizenode 0x05,
// nodemask8 0x06, safeenv 0x07, sizearray 0x08, lastfree 0x0C, node 0x10,
// array 0x18, metatable 0x20, gclist 0x28.
// NOTE: this order came from the exbeta base (737-era). The 0.743 dump section
// for LuaTable was not re-verified field-by-field - if a future dump disagrees,
// fix the struct and this comment together.

// TString, from global->tmname[0] = Roblox's own "__index":
//   +0x14 len = 7 and +0x18 data = "__index"
static_assert(offsetof(TString, next) == 0x8, "TString::next moved");
static_assert(offsetof(TString, hash) == 0x10, "TString::hash moved");
static_assert(offsetof(TString, len) == 0x14, "TString::len moved");
static_assert(offsetof(TString, data) == 0x18, "TString::data moved");
#include <RBX/environment/libs/script/script.hpp>
#include <RBX/environment/env.hpp>

#include <lstate.h>
#include <lgc.h>
#include <lualib.h>
#include <ltable.h>
#include <wininet.h>
#include <urlmon.h>
#include <cpr/cpr.h>
#include <luacode.h>
#include <Luau/Bytecode.h>
#include <Luau/Compiler.h>
#include <winhttp.h>
#include <zstd/xxhash.h>
#include <zstd/zstd.h>

#pragma comment(lib, "zstd_static.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "winhttp.lib")

#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/environment/yielding/yielding.hpp>
#include <RBX/update.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <Dependencies/Luau2/VM/src/lmem.h>
#include <RBX/environment/libs/closures/closures.hpp>


static LuaTable* RenvCache = nullptr;
static lua_State* RenvCacheState = nullptr;

typedef void(__fastcall* PushInstanceWeakPtrT)(lua_State* L, std::weak_ptr<uintptr_t>);

namespace HelperFuncs
{
    __forceinline bool CheckMemory(uintptr_t address) {
        if (address < 0x10000 || address > 0x7FFFFFFFFFFF) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.Protect & PAGE_NOACCESS || mbi.State != MEM_COMMIT) {
            return false;
        }

        return true;
    }
}

namespace Script
{
    std::map<void*, std::string> ScriptHashes;
    std::vector<ScriptableCacheEntry> s_ScriptableCache;
    std::unordered_map<std::string, bool> s_DefaultPropStates;

    void add_defualtprop_state(const char* prop_name, bool was_scriptable) {
        std::string key(prop_name);
        if (s_DefaultPropStates.find(key) == s_DefaultPropStates.end()) {
            s_DefaultPropStates[key] = was_scriptable;
        }
    }

    bool find_upd_scriptablecache(uintptr_t instance, const char* prop_name, bool new_state) {
        for (auto& entry : s_ScriptableCache) {
            if (entry.instance == instance && entry.property_name == prop_name) {
                entry.is_scriptable = new_state;
                return true;
            }
        }
        return false;
    }

    int get_cachedscriptable_prop(uintptr_t instance, const char* prop_name) {
        for (const auto& entry : s_ScriptableCache) {
            if (entry.instance == instance && entry.property_name == prop_name) {
                return entry.is_scriptable ? 1 : 0;
            }
        }
        return -1;
    }

    int getcacheddefscriptableprop(const char* prop_name) {
        auto it = s_DefaultPropStates.find(prop_name);
        if (it != s_DefaultPropStates.end()) {
            return it->second ? 1 : 0;
        }
        return -1;
    }

    bool IsValidScriptClass(const char* className)
    {
        if (!className) return false;
        return strcmp(className, ("LocalScript")) == 0 ||
            strcmp(className, ("ModuleScript")) == 0 ||
            strcmp(className, ("Script")) == 0;
    }

    std::string Blake2Hash(const char* data, size_t len)
    {
        if (!data || len == 0) return std::string(96, '0');

        uint64_t h[8] = {
            0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
            0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
            0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
        };

        for (size_t i = 0; i < len; i++) {
            uint64_t val = static_cast<unsigned char>(data[i]);
            h[i % 8] ^= val;
            h[i % 8] = (h[i % 8] << 7) | (h[i % 8] >> 57);
            h[(i + 1) % 8] += val;
        }

        for (int round = 0; round < 12; round++) {
            for (int i = 0; i < 8; i++) {
                h[i] ^= h[(i + 1) % 8];
                h[i] = (h[i] << 13) | (h[i] >> 51);
            }
        }

        char output[97];
        for (int i = 0; i < 6; i++) {
            sprintf_s(output + (i * 16), 17, ("%016llx"), h[i]);
        }
        output[96] = '\0';

        return std::string(output);
    }

    bool IsScriptClass(const std::string& className)
    {
        return className == ("LocalScript") || className == ("ModuleScript") || className == ("Script");
    }

    namespace Util
    {
        inline bool CheckMemory(uintptr_t address) {
            if (address < 0x10000 || address > 0x7FFFFFFFFFFF) {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if (mbi.Protect & PAGE_NOACCESS || mbi.State != MEM_COMMIT) {
                return false;
            }

            return true;
        }
    }

    namespace
    {
        constexpr std::size_t BytecodeSizeLimit = 64u * 1024u * 1024u;
        constexpr std::size_t ContainerHeaderSize = 8u;
        constexpr std::uintptr_t ScanBegin = 0x80;
        constexpr std::uintptr_t ScanEnd = 0x400;
        constexpr char ContainerSignature[4] = { 'R', 'S', 'B', '1' };
        constexpr std::uint64_t ContainerPlainMarker = 0xE009325B4A107A52ull;
        constexpr std::uint32_t ContainerFrameMagic = 0xFD2FB528u;
        constexpr std::uint32_t ContainerHashSeed = 42u;
        constexpr std::uint8_t ContainerHashStep = 41u;
        constexpr std::uint8_t BytecodeVersionCeiling = 31u;

        bool ReadRaw(std::uintptr_t address, void* buffer, std::size_t size)
        {
            if (size == 0 || !Util::CheckMemory(address) || !Util::CheckMemory(address + size - 1))
                return false;

            __try
            {
                std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        struct NativeString
        {
            union
            {
                char buffer[16];
                char* data;
            };

            std::size_t size;
            std::size_t capacity;
        };

        bool ReadStdString(std::uintptr_t address, std::string& out)
        {
            out.clear();

            NativeString value{};
            if (!ReadRaw(address, &value, sizeof(value)))
                return false;

            if (value.size <= ContainerHeaderSize || value.size > value.capacity
                || value.capacity > BytecodeSizeLimit)
                return false;

            if (value.capacity < sizeof(value.buffer))
            {
                out.assign(value.buffer, value.size);
                return true;
            }

            out.resize(value.size);

            if (!ReadRaw(reinterpret_cast<std::uintptr_t>(value.data), out.data(), value.size))
            {
                out.clear();
                return false;
            }

            return true;
        }

        bool DecodeBytecode(const std::string& source, std::string& out)
        {
            out.clear();

            if (source.size() <= ContainerHeaderSize)
                return false;

            std::vector<std::uint8_t> data(source.begin(), source.end());

            std::uint64_t marker = 0;
            std::memcpy(&marker, data.data(), sizeof(marker));

            if (marker == ContainerPlainMarker)
            {
                std::memcpy(data.data(), ContainerSignature, sizeof(ContainerSignature));
                std::memset(data.data() + sizeof(ContainerSignature), 0, sizeof(std::uint32_t));
            }
            else
            {
                std::uint8_t key[4]{};
                for (std::size_t i = 0; i < sizeof(key); ++i)
                {
                    const std::uint8_t masked = static_cast<std::uint8_t>(data[i] ^ ContainerSignature[i]);
                    key[i] = static_cast<std::uint8_t>(masked - static_cast<std::uint8_t>(i * ContainerHashStep));
                }

                for (std::size_t i = 0; i < data.size(); ++i)
                    data[i] ^= static_cast<std::uint8_t>(key[i % sizeof(key)] + static_cast<std::uint8_t>(i * ContainerHashStep));

                std::uint32_t expectedHash = 0;
                std::memcpy(&expectedHash, key, sizeof(expectedHash));

                if (XXH32(data.data(), data.size(), ContainerHashSeed) != expectedHash)
                    return false;
            }

            std::uint32_t decodedSize = 0;
            std::memcpy(&decodedSize, data.data() + sizeof(ContainerSignature), sizeof(decodedSize));

            if (decodedSize == 0)
            {
                out.assign(reinterpret_cast<const char*>(data.data()) + ContainerHeaderSize,
                    data.size() - ContainerHeaderSize);

                return !out.empty();
            }

            if (decodedSize > BytecodeSizeLimit || data.size() - ContainerHeaderSize <= sizeof(std::uint32_t))
                return false;

            std::uint32_t frameMagic = 0;
            std::memcpy(&frameMagic, data.data() + ContainerHeaderSize, sizeof(frameMagic));

            if (frameMagic != ContainerFrameMagic)
                return false;

            out.resize(decodedSize);

            const std::size_t written = ZSTD_decompress(
                out.data(),
                out.size(),
                data.data() + ContainerHeaderSize,
                data.size() - ContainerHeaderSize);

            if (ZSTD_isError(written) || written != decodedSize)
            {
                out.clear();
                return false;
            }

            return true;
        }

        bool LooksLikeBytecode(const std::string& bytecode)
        {
            if (bytecode.empty())
                return false;

            const std::uint8_t version = static_cast<std::uint8_t>(bytecode.front());
            return version != 0 && version <= BytecodeVersionCeiling;
        }

        bool ReadContainer(std::uintptr_t address, std::string& out)
        {
            std::string encoded;

            if (ReadStdString(address + Offsets::DataModel::ClassDescriptor, encoded)
                && DecodeBytecode(encoded, out) && LooksLikeBytecode(out))
                return true;

            if (ReadStdString(address, encoded) && DecodeBytecode(encoded, out) && LooksLikeBytecode(out))
                return true;

            out.clear();
            return false;
        }

        bool ReadBytecodeAt(std::uintptr_t field, std::string& out)
        {
            std::uintptr_t container = 0;

            if (ReadRaw(field, &container, sizeof(container)) && Util::CheckMemory(container)
                && ReadContainer(container, out))
                return true;

            return ReadContainer(field, out);
        }

        std::uintptr_t& LearnedOffset(bool moduleScript)
        {
            static std::uintptr_t localScriptOffset = 0;
            static std::uintptr_t moduleScriptOffset = 0;

            return moduleScript ? moduleScriptOffset : localScriptOffset;
        }

        std::string ReadBytecode(uintptr_t addr) {
            uintptr_t str = addr + 0x10;
            size_t len = *(size_t*)(str + 0x10);
            size_t cap = *(size_t*)(str + 0x18);
            uintptr_t data_ptr = (cap > 0x0f) ? *(uintptr_t*)(str + 0x00) : str;
            return std::string(reinterpret_cast<const char*>(data_ptr), len);
        }
    }

    bool FetchBytecode(std::uintptr_t script, bool moduleScript, std::string& out)
    {
        static std::mutex guard;
        const std::lock_guard<std::mutex> lock(guard);

        const std::uintptr_t primary = moduleScript
            ? Offsets::Scripts::ModuleScriptByteCode
            : Offsets::Scripts::LocalScriptByteCode;

        std::uintptr_t& learned = LearnedOffset(moduleScript);

        if (primary != 0 && ReadBytecodeAt(script + primary, out))
            return true;

        if (learned != 0 && learned != primary && ReadBytecodeAt(script + learned, out))
            return true;

        for (std::uintptr_t offset = ScanBegin; offset <= ScanEnd; offset += sizeof(std::uintptr_t))
        {
            if (offset == primary || offset == learned)
                continue;

            if (ReadBytecodeAt(script + offset, out))
            {
                learned = offset;
                return true;
            }
        }

        return false;
    }

    int ScriptError(lua_State* L, const char* message)
    {
        const char* error = message ? message : ("script error");
        int top = lua_gettop(L);
        lua_getglobal(L, ("error"));
        if (lua_isfunction(L, -1))
        {
            lua_pushstring(L, error);
            lua_call(L, 1, 0);
            return 0;
        }

        lua_settop(L, top);
        lua_pushnil(L);
        lua_pushstring(L, error);
        return 2;
    }

    int getscriptbytecode(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const char* typeName = luaL_typename(L, 1);
        if (!typeName || std::strcmp(typeName, ("Instance")) != 0)
            luaL_typeerrorL(L, 1, ("Instance"));

        void** userdata = static_cast<void**>(lua_touserdata(L, 1));
        if (!userdata || !*userdata || !Util::CheckMemory(reinterpret_cast<std::uintptr_t>(*userdata)))
            luaL_argerrorL(L, 1, ("invalid instance"));

        const std::uintptr_t script = reinterpret_cast<std::uintptr_t>(*userdata);

        const int top = lua_gettop(L);
        lua_getfield(L, 1, ("ClassName"));

        std::string className;
        if (lua_isstring(L, -1))
            className = lua_tostring(L, -1);

        lua_settop(L, top);

        if (!IsScriptClass(className))
            luaL_typeerrorL(L, 1, ("LocalScript, ModuleScript or Script"));

        std::string bytecode;
        bool decoded = false;

        try
        {
            decoded = FetchBytecode(script, className == ("ModuleScript"), bytecode);
        }
        catch (...)
        {
            decoded = false;
        }

        if (!decoded || bytecode.empty())
        {
            lua_pushnil(L);
            return 1;
        }

        lua_pushlstring(L, bytecode.data(), bytecode.size());
        return 1;
    }

    bool IsModuleScriptValue(lua_State* L, int index)
    {
        if (!lua_isuserdata(L, index))
            return false;

        index = lua_absindex(L, index);
        const int top = lua_gettop(L);

        lua_getfield(L, index, ("ClassName"));
        const bool isModule = lua_isstring(L, -1) &&
            strcmp(lua_tostring(L, -1), ("ModuleScript")) == 0;

        lua_settop(L, top);
        return isModule;
    }

    int getloadedmodules(lua_State* L)
    {
        lua_newtable(L);

        typedef struct {
            lua_State* pLua;
            int itemsFound;
            std::map< uintptr_t, bool > map;
        } GCOContext;

        auto gcCtx = GCOContext{ L, 0 };

        const auto ullOldThreshold = L->global->GCthreshold;
        L->global->GCthreshold = SIZE_MAX;

        luaM_visitgco(L, &gcCtx, [](void* ctx, lua_Page* pPage, GCObject* pGcObj) -> bool {
            const auto pCtx = static_cast<GCOContext*>(ctx);
            const auto ctxL = pCtx->pLua;

            if (isdead(ctxL->global, pGcObj))
                return false;

            if (const auto gcObjType = pGcObj->gch.tt;
                gcObjType == LUA_TFUNCTION) {
                ctxL->top->value.gc = pGcObj;
                ctxL->top->tt = gcObjType;
                ctxL->top++;

                lua_getfenv(ctxL, -1);

                if (!lua_isnil(ctxL, -1)) {
                    lua_getfield(ctxL, -1, ("script"));

                    if (lua_isuserdata(ctxL, -1)) {
                        uintptr_t script_addr = *static_cast<uintptr_t*>(lua_touserdata(ctxL, -1));

                        lua_getfield(ctxL, -1, ("ClassName"));
                        const char* class_name = lua_isstring(ctxL, -1) ? lua_tostring(ctxL, -1) : nullptr;
                        const bool is_module = class_name && strcmp(class_name, ("ModuleScript")) == 0;
                        lua_pop(ctxL, 1);

                        if (script_addr && pCtx->map.find(script_addr) == pCtx->map.end() && is_module) {
                            pCtx->map.insert({ script_addr, true });
                            lua_rawseti(ctxL, -4, ++pCtx->itemsFound);
                        }
                        else {
                            lua_pop(ctxL, 1);
                        }
                    }
                    else {
                        lua_pop(ctxL, 1);
                    }
                }

                lua_pop(ctxL, 2);
            }
            return false;
            });

        L->global->GCthreshold = ullOldThreshold;

        return 1;
    }

    __forceinline int getscripts(lua_State* L) {
        struct instancecontext {
            lua_State* L;
            int n;
        } Context = { L, 0 };
        lua_createtable(L, 0, 0);
        const auto ullOldThreshold = L->global->GCthreshold;
        L->global->GCthreshold = SIZE_MAX;
        luaM_visitgco(L, &Context, [](void* ctx, lua_Page* page, GCObject* gco) -> bool {
            auto gCtx = static_cast<instancecontext*>(ctx);
            if (isdead(gCtx->L->global, gco)) return false;
            if (gco->gch.tt == LUA_TUSERDATA) {
                TValue* top = gCtx->L->top;
                top->value.p = reinterpret_cast<void*>(gco);
                top->tt = LUA_TUSERDATA;
                gCtx->L->top++;
                if (!strcmp(luaL_typename(gCtx->L, -1), ("Instance"))) {
                    lua_getfield(gCtx->L, -1, ("ClassName"));
                    const char* cls = lua_isstring(gCtx->L, -1) ? lua_tolstring(gCtx->L, -1, 0) : nullptr;
                    if (cls && (!strcmp(cls, ("LocalScript")) || !strcmp(cls, ("ModuleScript")) || !strcmp(cls, ("CoreScript")))) {
                        lua_pop(gCtx->L, 1);
                        gCtx->n++;
                        lua_rawseti(gCtx->L, -2, gCtx->n);
                    }
                    else if (cls && !strcmp(cls, ("Script"))) {
                        lua_pop(gCtx->L, 1);
                        lua_getfield(gCtx->L, -1, ("RunContext"));
                        if (lua_isuserdata(gCtx->L, -1) || lua_isstring(gCtx->L, -1)) {
                            lua_getfield(gCtx->L, -1, ("Name"));
                            const char* runContext = lua_isstring(gCtx->L, -1) ? lua_tostring(gCtx->L, -1) : nullptr;
                            lua_pop(gCtx->L, 1);
                            if (runContext && !strcmp(runContext, ("Client"))) {
                                lua_pop(gCtx->L, 1);
                                gCtx->n++;
                                lua_rawseti(gCtx->L, -2, gCtx->n);
                            }
                            else {
                                lua_pop(gCtx->L, 2);
                            }
                        }
                        else {
                            lua_pop(gCtx->L, 2);
                        }
                    }
                    else {
                        lua_pop(gCtx->L, 2);
                    }
                }
                else {
                    lua_pop(gCtx->L, 1);
                }
            }
            return true;
            });
        L->global->GCthreshold = ullOldThreshold;
        return 1;
    }

    void PushGCObjects(lua_State* L, bool includeTables)
    {
        lua_newtable(L);
        lua_newtable(L);

        lua_pushstring(L, ("kvs"));
        lua_setfield(L, -2, ("__mode"));
        lua_setmetatable(L, -2);

        typedef struct {
            lua_State* luaThread;
            bool includeTables;
            int itemsFound;
        } GCOContext;

        auto GCContext = GCOContext{ L, includeTables, 0 };

        const auto oldGCThreshold = L->global->GCthreshold;
        L->global->GCthreshold = SIZE_MAX;

        luaM_visitgco(L, &GCContext, [](void* ctx, lua_Page* page, GCObject* gcObj) -> bool {
            const auto context = static_cast<GCOContext*>(ctx);
            const auto luaThread = context->luaThread;

            if (isdead(luaThread->global, gcObj))
                return false;

            const auto gcObjectType = gcObj->gch.tt;
            if (gcObjectType == LUA_TFUNCTION || gcObjectType == LUA_TTHREAD || gcObjectType == LUA_TUSERDATA ||
                gcObjectType == LUA_TLIGHTUSERDATA || gcObjectType == LUA_TBUFFER ||
                (gcObjectType == LUA_TTABLE && context->includeTables)) {
                luaThread->top->value.gc = gcObj;
                luaThread->top->tt = gcObjectType;
                incr_top(luaThread);

                const auto newTableIndex = ++context->itemsFound;
                lua_rawseti(luaThread, -2, newTableIndex);
            }

            return false;
            });

        L->global->GCthreshold = oldGCThreshold;
    }

    int getgc(lua_State* L) {
        PushGCObjects(L, luaL_optboolean(L, 1, false));
        return 1;
    };

    int getrenv(lua_State* L)
    {
        // Return the REAL game globals (with working print), not a
        // disconnected clone. Mirrors Miscellaneous::getgenv (which correctly
        // returns the exploit env via Shared::ExploitThread) but for the game
        // env via Shared::RobloxState. The old luaH_clone + RenvCache path
        // froze a snapshot (empty if taken before game globals populated) and
        // print in the copy was missing/wrong. Live reference keeps print
        // working. No locks, balanced stack, no signature change.
        lua_check(L, 0);
        lua_State* GameState = Shared::RobloxState;
        if (!GameState)
            GameState = L->global->mainthread;

        if (!GameState)
        {
            lua_pushnil(L);
            return 1;
        }

        if (GameState == L)
        {
            lua_pushvalue(L, LUA_GLOBALSINDEX);
            return 1;
        }

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        luaC_threadbarrier(GameState);
        lua_pushvalue(GameState, LUA_GLOBALSINDEX);
        lua_xmove(GameState, L, 1);
        return 1;
    }

    void PushTable(lua_State* L, LuaTable* Table)
    {
        luaC_threadbarrier(L);
        sethvalue(L, L->top, Table);
        incr_top(L);
    }

    int getcallingscript(lua_State* L)
    {
        // Frame 0 is always this C function: skip it, then walk outward
        // past executor-owned frames (our C closures, hook dispatchers,
        // newcclosure wrappers, loadstring chunks) the same way checkcaller
        // identifies executor frames (function_array / WrappedClosures /
        // executor chunk sources). The first surviving script-backed frame
        // is the genuine calling script. Without the skips we return the
        // immediate executor/C caller (e.g. the driver script) instead of
        // the real caller.
        // Hardened (checkcaller silent-death fix): fail closed on a dead
        // thread, probe each frame before touching Frame->func, and route
        // function_array / chunk-source matches through the probed Closures
        // helpers. Identical true-set on live data; stale entries fail
        // closed to nil. Leaf-locked map probes only, no Lua API under locks.
        if (!L)
            return 0;
        if (!L->ci || !L->base_ci)
        {
            lua_pushnil(L);
            return 1;
        }
        bool skippedSelf = false;
        for (CallInfo* Frame = L->ci; Frame >= L->base_ci; --Frame)
        {
            if (!Util::CheckMemory(reinterpret_cast<std::uintptr_t>(Frame)))
                break;
            if (!Frame->func || !Util::CheckMemory(reinterpret_cast<std::uintptr_t>(Frame->func)))
                continue;
            if (!ttisfunction(Frame->func))
                continue;

            Closure* Function = clvalue(Frame->func);
            if (!Function)
                continue;

            // Probe the body before ANY ->isC/->l.p/->env deref: a concurrent
            // restore (c.f/proto swap tearing the same Closure*) must skip,
            // not fault. Pointer hashes below never deref, so they stay first.
            if (!Closures::ClosureHeaderReadable(Function))
                continue;

            if (!skippedSelf)
            {
                skippedSelf = true;
                continue;
            }

            bool isExecutorFrame = false;
            if (Function->isC)
            {
                // Copy-out: all four set probes under the leaf lock, Lua work
                // below runs unlocked. Pure OR, same true-set on stable data;
                // a concurrent hook/restore rehash can no longer fault the
                // walk mid-test (silent death in whatever test loops here).
                bool isHookRecorded = false;
                bool isKnownExecutor = false;
                {
                    std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                    isHookRecorded = Closures::HookRecords.count(Function) > 0;
                    isKnownExecutor = Shared::ExecutorClosures.count(Function) > 0 ||
                        Closures::WrappedClosures.count(Function) > 0 ||
                        Closures::NewCClosureRefs.count(Function) > 0;
                }
                if (isKnownExecutor ||
                    isHookRecorded)
                {
                    isExecutorFrame = true;
                }
                else if (Closures::SafeFunctionArrayMatch(Function))
                {
                    // Probed + isC union-guarded (mirrors
                    // Closures::checkcaller). The raw known->isC / known->c.f
                    // read faults on GC'd actor entries still sitting in the
                    // process-wide vector.
                    isExecutorFrame = true;
                }
            }
            else
            {
                const Closure* asProto = reinterpret_cast<const Closure*>(Function->l.p);
                if (Function->l.p && Closures::ClosureHeaderReadable(asProto) && Function->l.p->source)
                {
                    // Bounded-NUL match (mirrors Closures::checkcaller): raw
                    // strcmp over-reads a freed source TString.
                    if (Closures::SafeExecutorChunkSource(getstr(Function->l.p->source)))
                    {
                        isExecutorFrame = true;
                    }
                }
                if (!isExecutorFrame)
                {
                    std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                    if (Closures::WrappedClosures.count(Function))
                        isExecutorFrame = true;
                }
            }

            if (isExecutorFrame)
                continue;

            if (!Function->env)
                continue;
            // Probe the env table before pushing it raw: a freed/destroyed
            // script env (teleport/script-destroy mid-walk) must skip here
            // instead of pushing a dangling LuaTable* that faults the
            // lua_getfield below with no log line. Live envs pass unchanged.
            if (!Util::CheckMemory(reinterpret_cast<std::uintptr_t>(Function->env)))
                continue;

            PushTable(L, Function->env);
            lua_getfield(L, -1, ("script"));
            lua_remove(L, -2);

            if (!lua_isnil(L, -1))
                return 1;

            lua_pop(L, 1);
        }

        lua_pushnil(L);
        return 1;
    }

    int getsenv(lua_State* L) {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        auto script = *reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 1));
        if (!script) {
            lua_pushnil(L);
            return 1;
        }

        lua_getfield(L, 1, ("ClassName"));
        std::string className;
        if (lua_isstring(L, -1))
            className = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (className != ("LocalScript") && className != ("Script") && className != ("ModuleScript")) {
            lua_pushnil(L);
            return 1;
        }

        // Loop invariant: L->global is stable for the duration of the call.
        lua_State* const mainThread = L->global->mainthread;
        for (lua_Page* cur = L->global->allgcopages; cur;) {
            lua_Page* next = luaM_getnextpage(cur);

            char* start;
            char* end;
            int busy;
            int block;
            luaM_getpagewalkinfo(cur, &start, &end, &busy, &block);

            for (char* pos = start; pos != end; pos += block) {
                auto gco = reinterpret_cast<GCObject*>(pos);

                if (!gco || isdead(L->global, gco) || gco->gch.tt != LUA_TTHREAD)
                    continue;

                auto thread = reinterpret_cast<lua_State*>(pos);

                if (!thread->userdata)
                    continue;

                // One lock() instead of expired()+lock(): same outcome, one
                // atomic refcount op instead of two, no TOCTOU window.
                auto scriptHandle = thread->userdata->script.lock();
                if (!scriptHandle)
                    continue;

                if ((uintptr_t)scriptHandle.get() != script)
                    continue;

                if (thread->global->mainthread != mainThread) {
                    lua_pushnil(L);
                    return 1;
                }

                luaC_threadbarrier(L);
                luaC_threadbarrier(thread);

                lua_pushvalue(thread, LUA_GLOBALSINDEX);
                lua_xmove(thread, L, 1);
                return 1;
            }

            cur = next;
        }

        // Fallback: no live thread carries this script (collected thread,
        // ModuleScript which never owns a thread, lost userdata association).
        // Scan live closures for an environment table whose `script` field is
        // this instance, mirroring the getloadedmodules function-env walk.
        struct GetsenvFallbackContext
        {
            lua_State* L;
            std::uintptr_t script;
            int envRef = LUA_NOREF;
        };
        GetsenvFallbackContext fallback{ L, script, LUA_NOREF };

        const auto oldThreshold = L->global->GCthreshold;
        L->global->GCthreshold = SIZE_MAX;

        luaM_visitgco(L, &fallback, [](void* context, lua_Page*, GCObject* gco) -> bool {
            auto* ctx = static_cast<GetsenvFallbackContext*>(context);
            lua_State* state = ctx->L;

            if (ctx->envRef != LUA_NOREF)
                return false;
            if (!gco || isdead(state->global, gco) || gco->gch.tt != LUA_TFUNCTION)
                return false;

            const int base = lua_gettop(state);
            lua_rawcheckstack(state, 4);

            state->top->value.gc = gco;
            state->top->tt = LUA_TFUNCTION;
            incr_top(state); // [func]

            lua_getfenv(state, -1); // [func, env]
            if (!lua_istable(state, -1))
            {
                lua_settop(state, base);
                return false;
            }

            lua_getfield(state, -1, ("script")); // [func, env, script]
            bool match = false;
            if (lua_isuserdata(state, -1))
            {
                void** userdata = static_cast<void**>(lua_touserdata(state, -1));
                match = (userdata && *userdata &&
                    reinterpret_cast<std::uintptr_t>(*userdata) == ctx->script);
            }

            if (match)
            {
                lua_pushvalue(state, -2); // [func, env, script, env]
                ctx->envRef = lua_ref(state, -1);
            }

            lua_settop(state, base);
            return false;
            });

        L->global->GCthreshold = oldThreshold;

        if (fallback.envRef != LUA_NOREF)
        {
            lua_getref(L, fallback.envRef);
            lua_unref(L, fallback.envRef);
            return 1;
        }

        lua_pushnil(L);
        return 1;
    }

    int getscriptthread(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        auto script = *reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 1));
        if (!script) {
            lua_pushnil(L);
            return 1;
        }

        lua_getfield(L, 1, ("ClassName"));
        std::string className;
        if (lua_isstring(L, -1))
            className = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (className != ("LocalScript") && className != ("ModuleScript") &&
            className != ("Script") && className != ("CoreScript")) {
            lua_pushnil(L);
            return 1;
        }

        for (lua_Page* cur = L->global->allgcopages; cur;) {
            lua_Page* next = luaM_getnextpage(cur);

            char* start;
            char* end;
            int busy;
            int block;
            luaM_getpagewalkinfo(cur, &start, &end, &busy, &block);

            for (char* pos = start; pos != end; pos += block) {
                auto gco = reinterpret_cast<GCObject*>(pos);

                if (!gco || isdead(L->global, gco) || gco->gch.tt != LUA_TTHREAD)
                    continue;

                auto thread = reinterpret_cast<lua_State*>(pos);

                if (!thread->userdata || thread->userdata->script.expired())
                    continue;

                if ((uintptr_t)thread->userdata->script.lock().get() != script)
                    continue;

                if (thread->global->mainthread != L->global->mainthread) {
                    lua_pushnil(L);
                    return 1;
                }

                lua_pushthread(thread);
                lua_xmove(thread, L, 1);
                return 1;
            }

            cur = next;
        }

        lua_pushnil(L);
        return 1;
    }
    int checkcaller(lua_State* L) {
        // :Fire()-dispatch threads simulate the game invoking the handler,
        // so checkcaller() is false on them. This MUST come first: these
        // threads are scriptless (script.expired() == true, like our own
        // execution threads) and may inherit MaxCapabilities, so the fast
        // paths below would otherwise misreport them as executor threads.
        // Our own threads (#32: any identity) still fall through to the
        // expired()/capabilities fast paths below and return true.
        // Hardened (silent-death fix): same boolean contract as before, but
        // function_array entries go through SafeFunctionArrayMatch (null +
        // ClosureHeaderReadable + isC union guard) and chunk sources through
        // SafeExecutorChunkSource (bounded NUL). Dangling actor entries fail
        // closed to false instead of faulting. Leaf-locked map probes only,
        // no Lua API under locks.
        if (!L)
            return 0;
        if (L && Shared::IsFireDispatchThread(L)) {
            lua_pushboolean(L, false);
            return 1;
        }
        if (Shared::ExploitThread && L == Shared::ExploitThread) {
            lua_pushboolean(L, true);
            return 1;
        }
        if (L && L->userdata &&
            (L->userdata->script.expired() ||
                L->userdata->capabilities == MaxCapabilities)) {
            lua_pushboolean(L, true);
            return 1;
        }
        if (!Environment)
        {
            lua_pushboolean(L, false);
            return 1;
        }
        lua_Debug ar;
        for (int level = 1; level <= 10; level++) {
            if (!lua_getinfo(L, level, ("f"), &ar))
                break;
            if (lua_isfunction(L, -1)) {
                // Mirror Closures::checkcaller: probe before deref (concurrent
                // restore can tear the body mid-walk), fast hash before slow
                // probed scan. Same booleans, fail-closed, no locks.
                Closure* cl = clvalue(index2addr(L, -1));
                if (Closures::ClosureHeaderReadable(cl)) {
                    bool isWrapped = false;
                    {
                        std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
                        isWrapped = Closures::WrappedClosures.count(cl) > 0;
                    }
                    if (isWrapped) {
                        lua_pop(L, 1);
                        lua_pushboolean(L, true);
                        return 1;
                    }
                    if (cl->isC) {
                        if (Closures::SafeFunctionArrayMatch(cl)) {
                            lua_pop(L, 1);
                            lua_pushboolean(L, true);
                            return 1;
                        }
                    }
                    else if (cl->l.p) {
                        const Closure* asClosure = reinterpret_cast<const Closure*>(cl->l.p);
                        if (Closures::ClosureHeaderReadable(asClosure) && cl->l.p->source) {
                            if (Closures::SafeExecutorChunkSource(getstr(cl->l.p->source))) {
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
    bool ReadProtectedString(uintptr_t address, std::string& out)
    {
        out.clear();

        static const size_t dataOffsets[] = { 0x10, 0x18 };
        static const size_t sizeOffsets[] = { 0x28, 0x20 };

        for (size_t dataOff : dataOffsets)
        {
            for (size_t sizeOff : sizeOffsets)
            {
                if (!HelperFuncs::CheckMemory(address + dataOff + sizeof(uintptr_t)))
                    continue;
                if (!HelperFuncs::CheckMemory(address + sizeOff + sizeof(size_t)))
                    continue;

                uintptr_t data = *reinterpret_cast<uintptr_t*>(address + dataOff);
                size_t len = *reinterpret_cast<size_t*>(address + sizeOff);

                if (len == 0 || len > 64 * 1024 * 1024)
                    continue;

                if (!HelperFuncs::CheckMemory(data) || !HelperFuncs::CheckMemory(data + len - 1))
                    continue;

                out.assign(reinterpret_cast<const char*>(data), len);
                return true;
            }
        }
        return false;
    }

    bool GetInstanceClassName(lua_State* L, int index, std::string& className)
    {
        int top = lua_gettop(L);
        lua_getfield(L, index, ("ClassName"));
        if (!lua_isstring(L, -1))
        {
            lua_settop(L, top);
            return false;
        }

        const char* value = lua_tostring(L, -1);
        className = value ? value : ("");
        lua_settop(L, top);
        return true;
    }

    int getscriptclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        std::string className;
        if (!GetInstanceClassName(L, 1, className))
            return ScriptError(L, ("Invalid Instance"));

        if (className != ("ModuleScript") && className != ("LocalScript") &&
            className != ("Script") && className != ("CoreScript"))
            return ScriptError(L, ("Script expected"));

        void* userdata = lua_touserdata(L, 1);
        if (!userdata)
            return ScriptError(L, ("Invalid Instance"));

        const std::uintptr_t script = *reinterpret_cast<const uintptr_t*>(userdata);

        if (!script)
            return ScriptError(L, ("Invalid Instance"));

        const bool UsesHolder = className != ("ModuleScript");

        const uintptr_t HolderR = UsesHolder ? Offsets::Scripts::LocalScriptByteCode : Offsets::Scripts::ModuleScriptByteCode;

        const uintptr_t HolderAddress = script + HolderR;

        if (!HelperFuncs::CheckMemory(HolderAddress))
            return ScriptError(L, ("Invalid bytecode holder address"));

        const std::uintptr_t holder = *reinterpret_cast<const std::uintptr_t*>(HolderAddress);

        if (!holder)
            return ScriptError(L, ("Null bytecode holder"));

        std::string encrypted;

        static const uintptr_t baseOffsets[] = { 0x18, 0x00, 0x10, 0x08 };
        bool found = false;
        for (uintptr_t baseOff : baseOffsets)
        {
            if (ReadProtectedString(holder + baseOff, encrypted) && !encrypted.empty())
            {
                found = true;
                break;
            }
        }

        if (!found || encrypted.empty())
            return ScriptError(L, ("string read failed"));

        if (encrypted.empty())
        {
            lua_pushnil(L);
            return 1;
        }

        lua_State* thread = lua_newthread(L);
        if (!thread)
            return ScriptError(L, ("Failed to create thread"));

        const int thread_ref = lua_ref(L, -1);
        lua_pop(L, 1);

        luaL_sandboxthread(thread);
        task_scheduler::SetIdentity(thread, 8, false);

        lua_pushvalue(L, 1);
        lua_xmove(L, thread, 1);
        lua_setglobal(thread, ("script"));

        const int loadResult = Roblox::LuaVMLoad(thread, &encrypted, ("getscriptclosure"), 0);

        if (loadResult != LUA_OK)
        {
            const char* error = lua_tostring(thread, -1);

            lua_settop(thread, 0);
            lua_unref(L, thread_ref);
            lua_pushnil(L);
            return 1;
        }

        if (!lua_isLfunction(thread, -1))
        {

            lua_settop(thread, 0);
            lua_unref(L, thread_ref);
            lua_pushnil(L);
            return 1;
        }

        Closure* closure = clvalue(luaA_toobject(thread, -1));

        if (!closure || closure->isC || !closure->l.p)
        {
            lua_settop(thread, 0);
            lua_unref(L, thread_ref);
            lua_pushnil(L);
            return 1;
        }

        task_scheduler::SetProtoCapabilities(closure->l.p, &MaxCapabilities);

        lua_xmove(thread, L, 1);
        lua_settop(thread, 0);
        lua_unref(L, thread_ref);

        return 1;
    }

    typedef void(__fastcall* PushInstanceWeakPtrT)(lua_State* L, std::weak_ptr<uintptr_t>);
    static PushInstanceWeakPtrT PushInstanceWeakPtr = (PushInstanceWeakPtrT)Roblox::PushInstance;

    int getscriptfromthread(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TTHREAD);

        lua_State* thread = lua_tothread(L, 1);

        if (!thread || !thread->userdata || thread->userdata->script.expired()) {
            lua_pushnil(L);
            return 1;
        }

        auto script = thread->userdata->script.lock();
        Roblox::InstanceBridge::Weak(L, script);

        return 1;
    }

    int getrunningscriptsv2(lua_State* L)
    {
        lua_newtable(L);
        int tableIndex = lua_gettop(L);
        int i = 0;
        std::unordered_map<std::shared_ptr<std::uintptr_t*>, bool> running_scripts = { };
        lua_rawcheckstack(L, 5);
        luaC_threadbarrier(L);
        lua_pushvalue(L, LUA_REGISTRYINDEX);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_isthread(L, -1)) {
                lua_State* thread = lua_tothread(L, -1);
                if (thread && thread->userdata != nullptr && !thread->userdata->script.expired()) {
                    if (thread->global->mainthread == L->global->mainthread) {
                        const auto script = *reinterpret_cast<std::shared_ptr<std::uintptr_t*>*>(&thread->userdata->script);
                        if (!running_scripts.contains(script)) {
                            Roblox::InstanceBridge::SharedInstance(L, script);
                            lua_getfield(L, -1, ("ClassName"));
                            const char* className = lua_tostring(L, -1);
                            lua_pop(L, 1);

                            bool isValidScript = false;

                            if (className) {
                                if (strcmp(className, ("LocalScript")) == 0 || strcmp(className, ("ModuleScript")) == 0) {
                                    isValidScript = true;
                                }
                                else if (strcmp(className, ("Script")) == 0) {
                                    lua_getfield(L, -1, ("RunContext"));
                                    if (lua_type(L, -1) != LUA_TNIL) {
                                        if (lua_isnumber(L, -1)) {
                                            int runContext = lua_tointeger(L, -1);
                                            isValidScript = (runContext == 2);
                                        }
                                        else {
                                            lua_getfield(L, -1, ("Value"));
                                            if (lua_isnumber(L, -1)) {
                                                int runContext = lua_tointeger(L, -1);
                                                isValidScript = (runContext == 2);
                                            }
                                            lua_pop(L, 1);
                                        }
                                    }
                                    lua_pop(L, 1);
                                }
                            }

                            if (isValidScript) {
                                running_scripts[script] = true;
                                lua_rawseti(L, tableIndex, ++i);
                            }
                            else {
                                lua_pop(L, 1);
                            }
                        }
                    }
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return 1;
    }

    int getallthreads(lua_State* L)
    {
        lua_newtable(L);
        int idx = 0;

        lua_State* mainthread = L->global->mainthread;

        lua_rawcheckstack(L, 4);
        luaC_threadbarrier(L);
        lua_pushvalue(L, LUA_REGISTRYINDEX);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_isthread(L, -1)) {
                lua_State* thread = lua_tothread(L, -1);
                if (thread && thread->global->mainthread == mainthread) {
                    lua_pushvalue(L, -1);
                    lua_rawseti(L, -4, ++idx);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        return 1;
    }

    int getscripthash(lua_State* L)
    {
        if (!L || !lua_isuserdata(L, 1)) {
            lua_pushstring(L, std::string(96, '0').c_str());
            return 1;
        }

        int top = lua_gettop(L);

        lua_getfield(L, 1, "ClassName");
        if (!lua_isstring(L, -1)) {
            lua_settop(L, top);
            lua_pushstring(L, std::string(96, '0').c_str());
            return 1;
        }

        const char* cn = lua_tostring(L, -1);
        if (!IsValidScriptClass(cn)) {
            lua_settop(L, top);
            lua_pushstring(L, std::string(96, '0').c_str());
            return 1;
        }

        // Myriad getscripthash: bytecode-less scripts must return nil, not a
        // hash of Name/Source/ptr. "No bytecode" is exactly getscriptbytecode's
        // failure mode (FetchBytecode false or empty out). Valid scripts fall
        // through to the identical hash path below (byte-identical).
        bool isModuleScript = (cn && std::strcmp(cn, ("ModuleScript")) == 0);
        void* scriptPtrEarly = *(void**)lua_touserdata(L, 1);
        lua_settop(L, top);

        if (!scriptPtrEarly)
        {
            lua_pushnil(L);
            return 1;
        }
        {
            std::string bcCheck;
            bool bcOk = false;
            try
            {
                bcOk = FetchBytecode(reinterpret_cast<std::uintptr_t>(scriptPtrEarly), isModuleScript, bcCheck);
            }
            catch (...)
            {
                bcOk = false;
            }
            if (!bcOk || bcCheck.empty())
            {
                lua_pushnil(L);
                return 1;
            }
        }

        std::string hashData;

        lua_getfield(L, 1, "Name");
        if (lua_isstring(L, -1)) {
            const char* name = lua_tostring(L, -1);
            if (name) hashData += name;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "Source");
        if (lua_isstring(L, -1)) {
            size_t len;
            const char* source = lua_tolstring(L, -1, &len);
            if (source && len > 0) {
                hashData.append(source, len);
            }
        }
        lua_pop(L, 1);

        void* ptr = *(void**)lua_touserdata(L, 1);
        if (ptr) {
            char ptrStr[32];
            sprintf_s(ptrStr, sizeof(ptrStr), "%p", ptr);
            hashData += ptrStr;
        }

        std::string hash = Blake2Hash(hashData.c_str(), hashData.length());

        if (ptr) {
            ScriptHashes[ptr] = hash;
        }

        lua_pushstring(L, hash.c_str());
        return 1;
    }


    void register_lib(lua_State* L)
    {
        if (!L) return;

        Environment->make_global(L, ("getloadedmodules"), getloadedmodules, Script::funcnameinc++);
        Environment->make_global(L, ("getrunningscripts"), getrunningscriptsv2, Script::funcnameinc++);
        Environment->make_global(L, ("checkcaller"), checkcaller, Script::funcnameinc++);
        Environment->make_global(L, ("getscriptfromthread"), getscriptfromthread, Script::funcnameinc++);
        Environment->make_global(L, ("getscripts"), getscripts, Script::funcnameinc++);
        Environment->make_global(L, ("getrenv"), getrenv, Script::funcnameinc++);
        Environment->make_global(L, ("getscripthash"), getscripthash, Script::funcnameinc++);
        Environment->make_global(L, ("getcallingscript"), getcallingscript, Script::funcnameinc++);
        Environment->make_global(L, ("getgc"), getgc, Script::funcnameinc++);
        Environment->make_global(L, ("getsenv"), getsenv, Script::funcnameinc++);
        Environment->make_global(L, ("getmenv"), getsenv, Script::funcnameinc++);
        Environment->make_global(L, ("getscriptclosure"), getscriptclosure, Script::funcnameinc++);
        Environment->make_global(L, ("getscriptfunction"), getscriptclosure, Script::funcnameinc++);
        Environment->make_global(L, ("getscriptthread"), getscriptthread, Script::funcnameinc++);
        Environment->make_global(L, ("getscriptbytecode"), getscriptbytecode, Script::funcnameinc++);
        Environment->make_global(L, ("dumpstring"), getscriptbytecode, Script::funcnameinc++);
        Environment->make_global(L, ("getallthreads"), getallthreads, Script::funcnameinc++);
    }
}

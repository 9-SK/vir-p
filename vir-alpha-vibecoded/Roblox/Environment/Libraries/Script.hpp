#pragma once

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
            if (size == 0 || !Utils::CheckMemory(address) || !Utils::CheckMemory(address + size - 1))
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

            if (ReadRaw(field, &container, sizeof(container)) && Utils::CheckMemory(container)
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

        bool IsScriptClass(const std::string& className)
        {
            return className == "LocalScript" || className == "ModuleScript" || className == "Script";
        }

        std::string ReadBytecode(uintptr_t addr) {
            uintptr_t str = addr + 0x10;
            size_t len = *(size_t*)(str + 0x10);
            size_t cap = *(size_t*)(str + 0x18);
            uintptr_t data_ptr = (cap > 0x0f) ? *(uintptr_t*)(str + 0x00) : str;
            return std::string(reinterpret_cast<const char*>(data_ptr), len);
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
                sprintf_s(output + (i * 16), 17, "%016llx", h[i]);
            }
            output[96] = '\0';

            return std::string(output);
        }
    }

    static std::uint8_t GetTargetBytecodeVersion()
    {
        static std::uint8_t cached = 0;
        if (cached != 0)
            return cached;
        std::string dummy = Luau::compile("return 0", {}, {});
        if (!dummy.empty())
            cached = static_cast<std::uint8_t>(dummy[0]);

        if (cached == 0)
            cached = static_cast<std::uint8_t>(LBC_VERSION_TARGET);

        return cached;
    }
    int getscriptbytecode_suns(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const char* typeName = luaL_typename(L, 1);
        if (!typeName || std::strcmp(typeName, ("Instance")) != 0)
            luaL_typeerrorL(L, 1, ("Instance"));

        void** userdata = static_cast<void**>(lua_touserdata(L, 1));
        if (!userdata || !*userdata || !Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(*userdata)))
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

        const std::uint8_t targetVersion = GetTargetBytecodeVersion();
        const std::uint8_t scriptVersion = static_cast<std::uint8_t>(bytecode[0]);

        if (scriptVersion != targetVersion && scriptVersion != 0)
        {

            bytecode[0] = static_cast<char>(targetVersion);
        }

        lua_pushlstring(L, bytecode.data(), bytecode.size());
        return 1;
    }

    int getscriptbytecode(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TUSERDATA);

        const char* typeName = luaL_typename(L, 1);
        if (!typeName || std::strcmp(typeName, "Instance") != 0)
            luaL_typeerrorL(L, 1, "Instance");

        void** userdata = static_cast<void**>(lua_touserdata(L, 1));
        if (!userdata || !*userdata || !Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(*userdata)))
            luaL_argerrorL(L, 1, "invalid instance");

        const std::uintptr_t script = reinterpret_cast<std::uintptr_t>(*userdata);

        const int top = lua_gettop(L);
        lua_getfield(L, 1, "ClassName");

        std::string className;
        if (lua_isstring(L, -1))
            className = lua_tostring(L, -1);

        lua_settop(L, top);

        if (!IsScriptClass(className))
            luaL_typeerrorL(L, 1, "LocalScript, ModuleScript or Script");

        std::string bytecode;
        bool decoded = false;

        try
        {
            decoded = FetchBytecode(script, className == "ModuleScript", bytecode);
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

    typedef void(__fastcall* PushInstanceWeakPtrT)(lua_State* L, std::weak_ptr<uintptr_t>);
    static PushInstanceWeakPtrT PushInstanceWeakPtr = (PushInstanceWeakPtrT)Hyperion::PushInstance;

    int getscriptfromthread(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TTHREAD);

        lua_State* thread = lua_tothread(L, 1);

        if (!thread || !thread->userdata || thread->userdata->script.expired()) {
            lua_pushnil(L);
            return 1;
        }

        auto script = thread->userdata->script.lock();
        Hyperion::InstanceBridge::Weak(L, script);

        return 1;
    }



    int getscripts(lua_State* L) {
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
                if (!strcmp(luaL_typename(gCtx->L, -1), "Instance")) {
                    lua_getfield(gCtx->L, -1, "ClassName");
                    const char* cls = lua_isstring(gCtx->L, -1) ? lua_tolstring(gCtx->L, -1, 0) : nullptr;
                    if (cls && (!strcmp(cls, "LocalScript") || !strcmp(cls, "ModuleScript") || !strcmp(cls, "CoreScript"))) {
                        lua_pop(gCtx->L, 1);
                        gCtx->n++;
                        lua_rawseti(gCtx->L, -2, gCtx->n);
                    }
                    else if (cls && !strcmp(cls, "Script")) {
                        lua_pop(gCtx->L, 1);
                        lua_getfield(gCtx->L, -1, "RunContext");
                        if (lua_isuserdata(gCtx->L, -1) || lua_isstring(gCtx->L, -1)) {
                            lua_getfield(gCtx->L, -1, "Name");
                            const char* runContext = lua_isstring(gCtx->L, -1) ? lua_tostring(gCtx->L, -1) : nullptr;
                            lua_pop(gCtx->L, 1);
                            if (runContext && !strcmp(runContext, "Client")) {
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
    bool IsValidScriptClass(const char* className)
    {
        if (!className) return false;
        return strcmp(className, ("LocalScript")) == 0 ||
            strcmp(className, ("ModuleScript")) == 0 ||
            strcmp(className, ("Script")) == 0;
    }


    std::map<void*, std::string> ScriptHashes;
    std::unordered_map<std::string, bool> s_DefaultPropStates;
    int getscripthash(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const char* typeName = luaL_typename(L, 1);
        if (!typeName || std::strcmp(typeName, "Instance") != 0)
            luaL_typeerrorL(L, 1, "Instance");

        void** userdata = static_cast<void**>(lua_touserdata(L, 1));
        if (!userdata || !*userdata || !Utils::CheckMemory(reinterpret_cast<std::uintptr_t>(*userdata))) {
            lua_pushnil(L);
            return 1;
        }
        const std::uintptr_t script = reinterpret_cast<std::uintptr_t>(*userdata);

        int top = lua_gettop(L);
        lua_getfield(L, 1, "ClassName");
        std::string className;
        if (lua_isstring(L, -1)) className = lua_tostring(L, -1);
        lua_settop(L, top);

        if (!IsValidScriptClass(className.c_str())) {
            lua_pushnil(L);
            return 1;
        }

        std::string bytecode;
        bool decoded = false;
        try {
            decoded = FetchBytecode(script, className == "ModuleScript", bytecode);
        }
        catch (...) {
            decoded = false;
        }

        if (!decoded || bytecode.empty() || !LooksLikeBytecode(bytecode)) {
            lua_pushnil(L); 
            return 1;
        }

        std::string hash = Blake2Hash(bytecode.data(), bytecode.size());

        void* ptr = *(void**)lua_touserdata(L, 1);
        if (ptr) ScriptHashes[ptr] = hash;

        lua_pushstring(L, hash.c_str());
        return 1;
    }


    inline void PushTable(lua_State* L, LuaTable* Table)
    {
        luaC_threadbarrier(L);
        sethvalue(L, L->top, Table);
        incr_top(L);
    }

    int getcallingscript(lua_State* L)
    {
        for (CallInfo* Frame = L->ci; Frame >= L->base_ci; --Frame)
        {
            if (!ttisfunction(Frame->func))
                continue;

            Closure* Function = clvalue(Frame->func);
            if (!Function->env)
                continue;

            PushTable(L, Function->env);
            lua_getfield(L, -1, "script");
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

        lua_getfield(L, 1, "ClassName");
        std::string className = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (className != "LocalScript" && className != "Script") {
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

                luaC_threadbarrier(L);
                luaC_threadbarrier(thread);

                lua_pushvalue(thread, LUA_GLOBALSINDEX);
                lua_xmove(thread, L, 1);
                return 1;
            }

            cur = next;
        }

        lua_pushnil(L);
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

    int getrunningscripts(lua_State* L)
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
                            Hyperion::InstanceBridge::Shared(L, script);
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
    inline std::string read_bytecode(uintptr_t addr) {
        uintptr_t str = addr + 0x10;
        size_t len = *(size_t*)(str + 0x10);
        size_t cap = *(size_t*)(str + 0x18);
        uintptr_t data_ptr = (cap > 0x0f) ? *(uintptr_t*)(str) : str;
        return std::string(reinterpret_cast<const char*>(data_ptr), len);
    }

    int getscriptclosure(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        std::string className;
        if (!GetInstanceClassName(L, 1, className))
            return ScriptError(L, "Invalid Instance");

        if (className != "ModuleScript" && className != "LocalScript" &&
            className != "Script" && className != "CoreScript")
            return ScriptError(L, "Script expected");

        uintptr_t ScriptPtr = *reinterpret_cast<uintptr_t*>(lua_touserdata(L, 1));
        if (!ScriptPtr || !Utils::CheckMemory(ScriptPtr))
        {
            lua_pushnil(L);
            return 1;
        }

        uintptr_t ProtectedString = 0;
        if (className == "ModuleScript")
            ProtectedString = *reinterpret_cast<uintptr_t*>(ScriptPtr + Offsets::Scripts::ModuleScriptByteCode);
        else
            ProtectedString = *reinterpret_cast<uintptr_t*>(ScriptPtr + Offsets::Scripts::LocalScriptByteCode);

        if (!ProtectedString || !Utils::CheckMemory(ProtectedString + Offsets::Scripts::Size))
        {
            lua_pushnil(L);
            return 1;
        }

        uintptr_t dataPtr = *reinterpret_cast<uintptr_t*>(ProtectedString + Offsets::Scripts::Pointer);
        size_t dataSize = *reinterpret_cast<size_t*>(ProtectedString + Offsets::Scripts::Size);

        if (!dataPtr || dataSize == 0 || dataSize > 64 * 1024 * 1024
            || !Utils::CheckMemory(dataPtr) || !Utils::CheckMemory(dataPtr + dataSize - 1))
        {
            lua_pushnil(L);
            return 1;
        }

        std::string Encrypted(reinterpret_cast<const char*>(dataPtr), dataSize);
        if (Encrypted.empty())
        {
            lua_pushnil(L);
            return 1;
        }

        lua_State* Thread = lua_newthread(L);
        lua_pop(L, 1);

        luaL_sandboxthread(Thread);
        TaskScheduler::SetIdentity(Thread, 8, false);

        lua_pushvalue(L, 1);
        lua_xmove(L, Thread, 1);
        lua_setglobal(Thread, "script");

        int result = Hyperion::LuaVM_Load(Thread, &Encrypted, "getscriptclosure", 0);
        if (result != LUA_OK || !lua_isLfunction(Thread, -1))
        {
            lua_pushnil(L);
            return 1;
        }

        Closure* cl = clvalue(luaA_toobject(Thread, -1));
        if (!cl || cl->isC || !cl->l.p)
        {
            lua_pushnil(L);
            return 1;
        }

        TaskScheduler::SetProtoCapabilities(cl->l.p, &MaxCapabilities);

        lua_pop(Thread, lua_gettop(Thread));

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        L->top->value.gc = reinterpret_cast<GCObject*>(cl);
        L->top->tt = LUA_TFUNCTION;
        L->top++;

        return 1;
    }

    int getscriptclosure_v3(lua_State* L)
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
        TaskScheduler::SetIdentity(thread, 8, false);

        lua_pushvalue(L, 1);
        lua_xmove(L, thread, 1);
        lua_setglobal(thread, ("script"));

        const int loadResult = Hyperion::LuaVM_Load(thread, &encrypted, ("getscriptclosure"), 0);

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

        TaskScheduler::SetProtoCapabilities(closure->l.p, &MaxCapabilities);

        lua_xmove(thread, L, 1);
        lua_settop(thread, 0);
        lua_unref(L, thread_ref);

        return 1;
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
                    lua_getfield(ctxL, -1, "script");

                    if (lua_isuserdata(ctxL, -1)) {
                        uintptr_t script_addr = *static_cast<uintptr_t*>(lua_touserdata(ctxL, -1));

                        lua_getfield(ctxL, -1, "ClassName");
                        const char* class_name = lua_isstring(ctxL, -1) ? lua_tostring(ctxL, -1) : nullptr;
                        const bool is_module = class_name && strcmp(class_name, "ModuleScript") == 0;
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

    void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, "getscriptbytecode", getscriptbytecode);
        Utils::AddFunction(L, "getscriptbytecoderoblox", getscriptbytecode_suns);
        Utils::AddFunction(L, "dumpstring", getscriptbytecode);
        Utils::AddFunction(L, "getscriptfromthread", getscriptfromthread);
        Utils::AddFunction(L, "getscripts", getscripts);
        Utils::AddFunction(L, "getscripthash", getscripthash);
        Utils::AddFunction(L, "getcallingscript", getcallingscript);
        Utils::AddFunction(L, "getsenv", getsenv);
        Utils::AddFunction(L, "getscriptenv", getsenv);
        Utils::AddFunction(L, "getgc", getgc);
        Utils::AddFunction(L, "getrunningscripts", getrunningscripts);
        Utils::AddFunction(L, "getscriptclosure", getscriptclosure);
        Utils::AddFunction(L, "getscriptfunction", getscriptclosure);
        Utils::AddFunction(L, "getloadedmodules", getloadedmodules);
        Utils::AddFunction(L, "getallthreads", getallthreads);
    }
}
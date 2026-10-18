#include "Miscellaneous.hpp"

#include <lgc.h>
#include <lstate.h>
#include <lualib.h>

#include <Windows.h>
#include <shlobj.h>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <RBX/execution/execution.hpp>
#include <RBX/environment/libs/script/script.hpp>
#include <RBX/environment/libs/http/http.hpp>
#include <map>
#include <string>
#include <sstream>
#include <lualib.h>
#include <optional>
#include <algorithm>
#include <Windows.h>

#include <cpr/cpr.h>
#include <Dependencies/nlohmann/json.hpp>
#include <RBX/environment/env.hpp>
#include <RBX/environment/yielding/yielding.hpp>

// ── decompile() result cache deps (local to this TU) ──
#include <chrono>
#include <deque>
#include <mutex>

#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/execution/execution.hpp>
#include <RBX/environment/yielding/yielding.hpp>
#include <RBX/environment/env.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>


namespace Miscellaneous {

    // ══════════════════════════════════════════════════════════════
    //  Existing functions
    // ══════════════════════════════════════════════════════════════

    int SetIdentity(lua_State* L) {
        lua_normalisestack(L, 1);

        if (lua_type(L, 1) != LUA_TNUMBER)
            luaL_typeerrorL(L, 1, lua_typename(L, LUA_TNUMBER));

        int identity = (int)lua_tointeger(L, 1);
        task_scheduler::SetThreadCapabilities(L, identity, identity == 8 ? MaxCapabilities : identity);
        return 0;
    }

    int setthreadidentity(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        int identity = (int)lua_tointeger(L, 1);

        if (!L->userdata)
            return 0;

        uint64_t caps = 0xFFFFFFFFFFFFFF00 | Roblox::GetCapabilities(&identity);

        if (identity == 8)
        {
            caps = MaxCapabilities;
        }

        L->userdata->identity.identity = identity;
        L->userdata->capabilities = caps;

        const uintptr_t identity_ptr = Roblox::GetIdentityStruct(*reinterpret_cast<uintptr_t*>(Offsets::RobloxThread::IdentityPtr));

        if (identity_ptr) {
            *reinterpret_cast<uint64_t*>(identity_ptr) = identity;
            *reinterpret_cast<uint64_t*>(identity_ptr + Offsets::Capabilities::Capabilities) = caps;
        }

        return 0;
    }

    int getthreadidentity(lua_State* L)
    {
        if (!L->userdata)
        {
            lua_pushinteger(L, 0);
            return 1;
        }
        lua_pushinteger(L, L->userdata->identity.identity);
        return 1;
    }

    int getgenv(lua_State* L)
    {
        if (Shared::ExploitThread == L || !Shared::ExploitThread)
        {
            lua_pushvalue(L, LUA_GLOBALSINDEX);
            return 1;
        }

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        luaC_threadbarrier(Shared::ExploitThread);
        lua_pushvalue(Shared::ExploitThread, LUA_GLOBALSINDEX);
        lua_xmove(Shared::ExploitThread, L, 1);

        return 1;
    }

    int identifyexecutor(lua_State* L)
    {
        lua_pushstring(L, ("Viridian"));
        lua_pushstring(L, ("1.0"));

        return 2;
    }

    int getexecutorname(lua_State* L)
    {
        lua_pushstring(L, ("Viridian"));

        return 1;
    }

    int GetObjects(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TSTRING);

        lua_getglobal(L, ("game"));
        lua_getfield(L, -1, ("GetService"));
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 2);
            luaL_error(L, ("game:GetService is unavailable"));
            return 0;
        }
        lua_pushvalue(L, -2);
        lua_pushstring(L, ("InsertService"));
        if (lua_pcall(L, 2, 1, 0) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            char message[512];
            strncpy_s(message, err ? err : ("failed to get InsertService"), _TRUNCATE);
            luaL_error(L, ("%s"), message);
            return 0;
        }
        lua_remove(L, -2);

        lua_getfield(L, -1, ("LoadLocalAsset"));
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 2);
            luaL_error(L, ("InsertService:LoadLocalAsset is unavailable"));
            return 0;
        }

        lua_pushvalue(L, -2);
        lua_pushvalue(L, 2);
        if (lua_pcall(L, 2, 1, NULL) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            char message[512];
            strncpy_s(message, err ? err : ("LoadLocalAsset failed"), _TRUNCATE);
            luaL_error(L, ("%s"), message);
            return 0;
        }

        if (lua_type(L, -1) == LUA_TSTRING)
            luaL_error(L, lua_tostring(L, -1));

        lua_createtable(L, 1, NULL);
        lua_pushvalue(L, -2);
        lua_rawseti(L, -2, 1);

        lua_remove(L, -3);
        lua_remove(L, -2);

        return 1;
    }

    // Process-wide FPS cap value for the setfpscap/getfpscap round-trip.
    // setfpscap stores here (and still programs the scheduler delay as
    // before); getfpscap returns it. Default 60 matches the stock client
    // cap and the previous pre-set getfpscap fallback. getfpscap used to
    // read an unrelated static (TaskSchedulerTargetFps) that setfpscap
    // never wrote, so the value never round-tripped.
    static double s_FpsCapValue = 60.0;

    int setfpscap(lua_State* L) {
        luaL_checktype(L, 1, LUA_TNUMBER);
        double fps = lua_tonumber(L, 1);

        s_FpsCapValue = fps;

        uintptr_t ptr = *reinterpret_cast<uintptr_t*>(Offsets::Scheduler::TaskScheduler);
        if (!ptr)
            return 0;

        double delay = 0.0;
        if (fps > 0.0)
            delay = 1.0 / fps;

        *reinterpret_cast<double*>(ptr + Offsets::Scheduler::Fps) = delay;
        return 0;
    }

    int getfpscap(lua_State* L)
    {
        lua_pushnumber(L, s_FpsCapValue);
        return 1;
    }

    int queueonteleport(lua_State* L)
    {
        size_t length = 0;
        const char* code = luaL_checklstring(L, 1, &length);
        std::lock_guard<std::mutex> lock(Shared::TeleportQueueMutex);
        Shared::TeleportQueue.emplace_back(code, length);
        return 0;
    }

    int clearteleportqueue(lua_State*)
    {
        std::lock_guard<std::mutex> lock(Shared::TeleportQueueMutex);
        Shared::TeleportQueue.clear();
        return 0;
    }

    int gethui(lua_State* L)
    {
        lua_getglobal(L, ("game"));
        if (!lua_isuserdata(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

        lua_getfield(L, -1, ("GetService"));
        if (!lua_isfunction(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return 1; }

        lua_pushvalue(L, -2);
        lua_pushstring(L, ("CoreGui"));
        if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            lua_pushnil(L);
            return 1;
        }

        if (!lua_isuserdata(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }
        int coreGuiIdx = lua_gettop(L);

        lua_getfield(L, -1, ("FindFirstChildOfClass"));
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

        lua_pushvalue(L, coreGuiIdx);
        lua_pushstring(L, ("ScreenGui"));
        if (lua_pcall(L, 2, 1, 0) == LUA_OK && lua_isuserdata(L, -1)) {
            return 1;
        }

        lua_getglobal(L, ("Instance"));
        lua_getfield(L, -1, ("new"));
        lua_pushstring(L, ("ScreenGui"));
        lua_call(L, 1, 1);
        lua_remove(L, -2);

        if (lua_isuserdata(L, -1)) {
            lua_pushvalue(L, -1);
            lua_setfield(L, coreGuiIdx, ("Name"));
            lua_pushvalue(L, -1);
            lua_setfield(L, coreGuiIdx, ("Parent"));
            return 1;
        }

        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }

    // ── decompile() disk cache (persists across sessions) ──
    // Mirrors the HTTP disk-cache pattern: %LOCALAPPDATA%\Viridian\<name>\
    // + hex-hash filename, best-effort with silent failure → memory-only
    // fallback. No sidecar needed: empty/missing file = miss (failures are
    // never cached, so a missing file always means miss-or-failure).
    static std::mutex s_DecompileDiskMutex;
    static constexpr size_t kDecompileDiskMaxBytes = 1u * 1024u * 1024u;

    static std::string DecompileDiskHex(const std::string& key)
    {
        size_t h = std::hash<std::string>{}(key);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llx", (unsigned long long)h);
        return std::string(buf);
    }

    static bool DecompileDiskPath(const std::string& key, std::string* out, bool ensureDir)
    {
        char base[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, (DWORD)sizeof(base));
        if (n == 0 || n >= sizeof(base))
            return false;
        std::string viridianDir = std::string(base) + "\\Viridian";
        std::string cacheDir = viridianDir + "\\decompilecache\\";
        if (ensureDir)
        {
            // Best-effort: create parent then leaf, ignore failures.
            CreateDirectoryA(viridianDir.c_str(), nullptr);
            CreateDirectoryA(cacheDir.c_str(), nullptr);
        }
        if (out)
            *out = cacheDir + DecompileDiskHex(key) + ".lua";
        return true;
    }

    static bool DecompileDiskLookup(const std::string& key, std::string* out)
    {
        std::string path;
        if (!DecompileDiskPath(key, &path, false))
            return false;
        std::lock_guard<std::mutex> lock(s_DecompileDiskMutex);
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open())
            return false;
        std::streamsize size = f.tellg();
        if (size <= 0 || (size_t)size > kDecompileDiskMaxBytes)
            return false;
        f.seekg(0, std::ios::beg);
        std::string body;
        body.resize((size_t)size);
        if (!f.read(body.data(), size))
            return false;
        if (body.empty())
            return false;
        if (out)
            *out = std::move(body);
        return true;
    }

    static void DecompileDiskStore(const std::string& key, const std::string& result)
    {
        if (key.size() > kDecompileDiskMaxBytes || result.empty() || result.size() > kDecompileDiskMaxBytes)
            return;
        std::string path;
        if (!DecompileDiskPath(key, &path, true))
            return;
        std::lock_guard<std::mutex> lock(s_DecompileDiskMutex);
        // Best-effort: any I/O failure is silently ignored.
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return;
        f.write(result.data(), (std::streamsize)result.size());
    }

    int decompile(lua_State* L) {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 1, &len);
        // Copy immediately: the Lua string pointer must not be held across
        // the blocking POST (a GC/move could invalidate it on another thread).
        std::string key(data, len);

        // ── Result cache: decompile(bytecode) is pure in its input, so a
        //    cached source can be returned byte-identical with no staleness.
        //    FIFO, 32 entries max, skip keys/results > 1MB. Failures are
        //    never cached (transient network errors must retry live).
        static std::unordered_map<std::string, std::string> s_Cache;
        static std::deque<std::string> s_Order;
        static std::mutex s_Mutex;
        static constexpr size_t kMaxEntries = 32u;
        static constexpr size_t kMaxEntryBytes = 1u * 1024u * 1024u;

        // Shared eviction path: both the disk-warm path below and the
        // POST-store path insert through here (single copy, identical
        // behavior to the original inline block: FIFO, 32 entries max,
        // insert-or-overwrite).
        auto memStore = [&](const std::string& k, const std::string& v) {
            auto it = s_Cache.find(k);
            if (it == s_Cache.end()) {
                if (s_Cache.size() >= kMaxEntries && !s_Order.empty()) {
                    std::string oldest = std::move(s_Order.front());
                    s_Order.pop_front();
                    s_Cache.erase(oldest);
                }
                s_Order.push_back(k);
                s_Cache.emplace(k, v);
            }
            else {
                it->second = v;
            }
        };

        if (key.size() <= kMaxEntryBytes) {
            {
                std::lock_guard<std::mutex> lock(s_Mutex);
                auto it = s_Cache.find(key);
                if (it != s_Cache.end()) {
                    lua_pushlstring(L, it->second.data(), it->second.size());
                    return 1;
                }
            }
            // Memory miss → try disk (file I/O runs with s_Mutex released).
            // On hit, warm the memory cache through the same eviction path
            // the POST path uses, then return via the identical push path.
            std::string diskHit;
            if (DecompileDiskLookup(key, &diskHit)) {
                std::string ret;
                {
                    std::lock_guard<std::mutex> lock(s_Mutex);
                    memStore(key, diskHit);
                    ret = diskHit;
                }
                lua_pushlstring(L, ret.data(), ret.size());
                return 1;
            }
        }

        auto start = std::chrono::steady_clock::now();

        cpr::Response r = cpr::Post(
            cpr::Url{ "https://api.luacid.dev/decompile?type_annotations=functions&generated_names=readable&indent=tab" },
            cpr::Header{ {"Content-Type", "application/octet-stream"} },
            cpr::Body{ key },
            cpr::Timeout{ 8000 }
        );

        if (r.status_code != 200) {
            std::string msg = "-- Request failed: HTTP " + std::to_string(r.status_code) + "\n" + r.text;
            lua_pushlstring(L, msg.c_str(), msg.size());
            return 1;
        }

        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        std::istringstream in(r.text);
        std::string line, out;
        bool first = true;
        while (std::getline(in, line)) {
            if (line.rfind("--[[ luacid.dev ]]", 0) == 0) continue;
            if (line.rfind("-- Luau bytecode version", 0) == 0) continue;
            if (!first) out += "\n";
            out += line;
            first = false;
        }

        char header[64];
        std::snprintf(header, sizeof(header), "-- [Shitsploit] Decompiled in %.1fs\n\n", elapsed);

        std::string result = std::string(header) + out;

        // Store on success only, through the same byte-identical shape.
        // Copies are taken under lock; the disk write below runs with
        // s_Mutex released (never hold it during file I/O). Empty results
        // are skipped (can't occur in practice — the header is always
        // present — but a zero-byte file must always read back as a miss).
        std::string diskKey, diskResult;
        if (key.size() <= kMaxEntryBytes && !result.empty() && result.size() <= kMaxEntryBytes) {
            {
                std::lock_guard<std::mutex> lock(s_Mutex);
                memStore(key, result);
            }
            diskKey = key;
            diskResult = result;
        }
        if (!diskResult.empty())
            DecompileDiskStore(diskKey, diskResult);

        lua_pushlstring(L, result.c_str(), result.size());
        return 1;
    }

    static std::string escape_string(const char* s) {
        if (!s) return "";
        std::string result;
        for (const char* p = s; *p; ++p) {
            switch (*p) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += *p; break;
            }
        }
        return result;
    }

    static void serialize_instance(lua_State* L, int instanceIdx, int depth, std::string& out) {
        if (!lua_isuserdata(L, instanceIdx)) return;

        lua_getglobal(L, "typeof");
        lua_pushvalue(L, instanceIdx);
        lua_call(L, 1, 1);
        const char* tp = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!tp || strcmp(tp, "Instance") != 0) return;

        lua_getfield(L, instanceIdx, "ClassName");
        const char* className = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Instance";
        lua_pop(L, 1);

        lua_getfield(L, instanceIdx, "Name");
        const char* instName = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);

        std::string indent(depth * 4, ' ');
        std::string varName = "inst_" + std::to_string(reinterpret_cast<uintptr_t>(lua_touserdata(L, instanceIdx)));

        out += indent + "local " + varName + " = Instance.new(\"" + className + "\")\n";
        out += indent + varName + ".Name = \"" + escape_string(instName) + "\"\n";

        lua_getfield(L, instanceIdx, "ClassName");
        const char* cn = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);

        if (strcmp(cn, "Part") == 0 || strcmp(cn, "MeshPart") == 0 || strcmp(cn, "UnionOperation") == 0 ||
            strcmp(cn, "SpawnLocation") == 0 || strcmp(cn, "WedgePart") == 0 || strcmp(cn, "CornerWedgePart") == 0) {
            lua_getfield(L, instanceIdx, "Position");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "X"); double px = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Y"); double py = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Z"); double pz = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_pop(L, 1);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s%s.Position = Vector3.new(%g, %g, %g)\n", indent.c_str(), varName.c_str(), px, py, pz);
                out += buf;
            }
            else { lua_pop(L, 1); }

            lua_getfield(L, instanceIdx, "Size");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "X"); double sx = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Y"); double sy = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Z"); double sz = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_pop(L, 1);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s%s.Size = Vector3.new(%g, %g, %g)\n", indent.c_str(), varName.c_str(), sx, sy, sz);
                out += buf;
            }
            else { lua_pop(L, 1); }

            lua_getfield(L, instanceIdx, "BrickColor");
            if (!lua_isnil(L, -1) && !lua_isnone(L, -1)) {
                lua_getfield(L, -1, "Name");
                const char* bc = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
                lua_pop(L, 1);
                lua_pop(L, 1);
                if (bc) {
                    out += indent + varName + ".BrickColor = BrickColor.new(\"" + bc + "\")\n";
                }
            }
            else { lua_pop(L, 1); }

            lua_getfield(L, instanceIdx, "Anchored");
            if (!lua_isnil(L, -1)) {
                out += indent + varName + ".Anchored = " + (lua_toboolean(L, -1) ? "true" : "false") + "\n";
            }
            lua_pop(L, 1);
        }

        lua_getfield(L, instanceIdx, "Parent");
        lua_pop(L, 1);

        out += "\n";
    }

    static void serialize_children(lua_State* L, int instanceIdx, int depth, std::string& out) {
        // Cap C-stack recursion: legit trees nest far shallower; beyond this
        // the subtree is skipped instead of exhausting the native stack.
        if (depth > 64) return;
        lua_getfield(L, instanceIdx, "GetChildren");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
        lua_pushvalue(L, instanceIdx);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) { lua_pop(L, 1); return; }
        if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

        int tableIdx = lua_gettop(L);
        int len = (int)lua_objlen(L, tableIdx);
        for (int i = 1; i <= len; ++i) {
            lua_rawgeti(L, tableIdx, i);
            if (lua_isuserdata(L, -1)) {
                serialize_instance(L, -1, depth, out);
                serialize_children(L, lua_gettop(L), depth + 1, out);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    int saveinstance(lua_State* L) {
        const char* savePath = nullptr;
        int top = lua_gettop(L);

        if (top >= 2 && lua_istable(L, 2)) {
            lua_getfield(L, 2, "FileName");
            if (lua_isstring(L, -1)) {
                savePath = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
        else if (top >= 1 && lua_isstring(L, 1)) {
            savePath = lua_tostring(L, 1);
        }

        lua_getglobal(L, "game");
        if (!lua_isuserdata(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, ("game not found"));
            return 0;
        }
        int gameIdx = lua_gettop(L);

        std::string output;
        output += "-- Generated by executor saveinstance\n";
        output += "local game = game\n";

        lua_getfield(L, gameIdx, "PlaceId");
        char buf[256];
        snprintf(buf, sizeof(buf), "local PlaceId = %g\n", lua_tonumber(L, -1));
        output += buf;
        lua_pop(L, 1);

        lua_getfield(L, gameIdx, "JobId");
        lua_pop(L, 1);

        output += "\n";

        serialize_instance(L, gameIdx, 0, output);
        serialize_children(L, gameIdx, 1, output);

        lua_pop(L, 1);

        wchar_t pathBuf[MAX_PATH] = {};
        if (savePath && savePath[0]) {
            if (!Shared::workspace_folder.empty()) {
                std::wstring wsFolder = Shared::workspace_folder.wstring();
                wcscpy_s(pathBuf, MAX_PATH, wsFolder.c_str());
                wchar_t fileName[MAX_PATH] = {};
                MultiByteToWideChar(CP_UTF8, 0, savePath, -1, fileName, MAX_PATH);
                wcscat_s(pathBuf, MAX_PATH, L"\\");
                wcscat_s(pathBuf, MAX_PATH, fileName);
            }
            else {
                MultiByteToWideChar(CP_UTF8, 0, savePath, -1, pathBuf, MAX_PATH);
            }
        }
        else {
            if (!Shared::workspace_folder.empty()) {
                std::wstring wsFolder = Shared::workspace_folder.wstring();
                wcscpy_s(pathBuf, MAX_PATH, wsFolder.c_str());
                wcscat_s(pathBuf, MAX_PATH, L"\\savefile.lua");
            }
            else {
                wchar_t desktop[MAX_PATH] = {};
                SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, desktop);
                wcscpy_s(pathBuf, MAX_PATH, desktop);
                wcscat_s(pathBuf, MAX_PATH, L"\\savefile.lua");
            }
        }

        std::ofstream file(pathBuf);
        if (file.is_open()) {
            file << output;
            file.close();
            lua_pushboolean(L, true);
        }
        else {
            lua_pushboolean(L, false);
        }
        return 1;
    }

    // ══════════════════════════════════════════════════════════════
    //  Registration
    // ══════════════════════════════════════════════════════════════

    void register_lib(lua_State* L)
    {
        Environment->make_global(L, ("getgenv"), getgenv, Miscellaneous::funcnameinc++);
        Environment->make_global(L, ("decompile"), decompile, Miscellaneous::funcnameinc++);
        Environment->make_global(L, ("saveinstance"), saveinstance, Miscellaneous::funcnameinc++);
        Environment->make_global(L, ("identifyexecutor"), identifyexecutor, Miscellaneous::funcnameinc++);
        Environment->make_global(L, ("getexecutorname"), getexecutorname, Miscellaneous::funcnameinc++);
        Environment->make_aliases(L, setthreadidentity, Miscellaneous::funcnameinc, { ("setthreadidentity"), ("setidentity"), ("setthreadcontext") });
       Environment->make_aliases(L, getthreadidentity, Miscellaneous::funcnameinc, { ("getthreadidentity"), ("getidentity"), ("getthreadcontext") });
       Environment->make_global(L, ("setfpscap"), setfpscap, Miscellaneous::funcnameinc++);
       Environment->make_global(L, ("getfpscap"), getfpscap, Miscellaneous::funcnameinc++);
       Environment->make_global(L, ("GetObjects"), GetObjects, Miscellaneous::funcnameinc++);
      Environment->make_aliases(L, queueonteleport, Miscellaneous::funcnameinc, { ("queueonteleport"), ("queue_on_teleport") });
     Environment->make_aliases(L, clearteleportqueue, Miscellaneous::funcnameinc, { ("clearteleportqueue"), ("clear_teleport_queue"), ("clearqueueonteleport") });
       Environment->make_aliases(L, gethui, Miscellaneous::funcnameinc, { ("gethui"), ("get_hidden_ui"), ("get_hidden_gui") });
    }
}

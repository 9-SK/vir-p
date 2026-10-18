#pragma once

#include <lgc.h>
#include <lstate.h>
#include <lualib.h>
#include <string>
#include <vector>

#include <Roblox/Shared/Shared.hpp>
#include <ltable.h>
#include <Windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

static LuaTable* RenvCache = nullptr;
static lua_State* RenvCacheState = nullptr;
namespace Miscellaneous
{
    namespace
    {
        inline bool DecodeCompressedBytecode(const std::string& compressed, std::string& output)
        {
            output.clear();

            if (compressed.size() < 8)
                return false;

            std::vector<uint8_t> data(compressed.begin(), compressed.end());
            uint8_t key[4] = {
                static_cast<uint8_t>(data[0] ^ 0x52),
                static_cast<uint8_t>((data[1] ^ 0x53) - 41),
                static_cast<uint8_t>((data[2] ^ 0x42) - 82),
                static_cast<uint8_t>((data[3] ^ 0x31) - 123),
            };

            for (size_t i = 0; i < data.size(); ++i)
                data[i] ^= static_cast<uint8_t>(key[i & 3] + 41 * i);

            uint32_t expectedHash = uint32_t(key[0]) | (uint32_t(key[1]) << 8) | (uint32_t(key[2]) << 16) | (uint32_t(key[3]) << 24);
            uint32_t actualHash = XXH32(data.data(), data.size(), 42);
            if (actualHash != expectedHash)
                return false;

            uint32_t decompressedSize =
                uint32_t(data[4]) | (uint32_t(data[5]) << 8) | (uint32_t(data[6]) << 16) | (uint32_t(data[7]) << 24);
            if (decompressedSize == 0 || decompressedSize > 64 * 1024 * 1024)
                return false;

            output.resize(decompressedSize);
            size_t result = ZSTD_decompress(output.data(), output.size(), data.data() + 8, data.size() - 8);
            if (ZSTD_isError(result))
            {
                output.clear();
                return false;
            }

            output.resize(result);
            return true;
        }
    }

    int getgenv(lua_State* L)
    {
    
        if (Shared::VoltState == L)
        {
            lua_pushvalue(L, LUA_GLOBALSINDEX);
            return 1;
        }

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        luaC_threadbarrier(Shared::VoltState);
        lua_pushvalue(Shared::VoltState, LUA_GLOBALSINDEX);
        lua_xmove(Shared::VoltState, L, 1);

        return 1;
    }

    int setfpscap(lua_State* L) {
        luaL_checktype(L, 1, LUA_TNUMBER);
        double fps = lua_tonumber(L, 1);

        uintptr_t ptr = *reinterpret_cast<uintptr_t*>(Offsets::TaskScheduler::TaskSchedulerPtr);
        if (!ptr)
            return 0;

        double delay = 0.0;
        if (fps > 0.0)
            delay = 1.0 / fps;

        *reinterpret_cast<double*>(ptr + Offsets::TaskScheduler::Fps) = delay;
        return 0;
    }

    int getfpscap(lua_State* L)
    {
        __try {
            float cap = *reinterpret_cast<float*>(Offsets::Flags::TaskSchedulerTargetFps);
            lua_pushnumber(L, cap);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            lua_pushnumber(L, 60);
        }
        return 1;
    }
    int getrenv(lua_State* L)
    {
        lua_check(L, 0);
        lua_State* RobloxState = L->global->mainthread;

        if (RenvCacheState != RobloxState)
        {
            RenvCache = nullptr;
            RenvCacheState = RobloxState;
        }

        if (!RenvCache)
        {
            LuaTable* clone = luaH_clone(L, RobloxState->gt);

            lua_rawcheckstack(L, 2);
            luaC_threadbarrier(L);
            luaC_threadbarrier(RobloxState);
            L->top->value.p = clone;
            L->top->tt = LUA_TTABLE;
            L->top++;

            lua_pushvalue(L, -1);
            lua_ref(L, -1);
            lua_pop(L, 1);

            lua_rawgeti(L, LUA_REGISTRYINDEX, 2);
            lua_setfield(L, -2, ("_G"));
            lua_rawgeti(L, LUA_REGISTRYINDEX, 4);
            lua_setfield(L, -2, ("shared"));

            RenvCache = clone;
            lua_pop(L, 1);
        }

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);
        L->top->value.p = RenvCache;
        L->top->tt = LUA_TTABLE;
        L->top++;

        return 1;
    }

    int identifyexecutor(lua_State* L)
    {
    
        lua_pushstring(L, "viridian");
        lua_pushstring(L, "1.1.5");

        return 2;
    };

    int getexecutorname(lua_State* L)
    {
    
        lua_pushstring(L, "viridian/1.1.5");

        return 1;
    };

    int getobjects(lua_State* L)
    {
    
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TSTRING);

        lua_getglobal(L, "game");
        lua_getfield(L, -1, "GetService");
        lua_pushvalue(L, -2);
        lua_pushstring(L, "InsertService");
        lua_call(L, 2, 1);
        lua_remove(L, -2);

        lua_getfield(L, -1, "LoadLocalAsset");

        lua_pushvalue(L, -2);
        lua_pushvalue(L, 2);
        lua_pcall(L, 2, 1, NULL);

        if (lua_type(L, -1) == LUA_TSTRING)
            luaL_error(L, lua_tostring(L, -1));

        lua_createtable(L, 1, NULL);
        lua_pushvalue(L, -2);
        lua_rawseti(L, -2, 1);

        lua_remove(L, -3);
        lua_remove(L, -2);

        return 1;
    }

    int getreg(lua_State* L)
    {
        
        luaL_trimstack(L, (int)0);

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);

        lua_pushvalue(L, LUA_REGISTRYINDEX);
        return 1;
    }

    int getmenv(lua_State* L)
    {
        
        if (!lua_isuserdata(L, 1)) {
            lua_pushnil(L);
            return 1;
        }

        lua_getfield(L, 1, "ClassName");
        const char* cn = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
        lua_pop(L, 1);

        if (!cn || strcmp(cn, "ModuleScript") != 0) {
            lua_pushnil(L);
            return 1;
        }

        lua_getglobal(L, "require");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            return 1;
        }

        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 1, 0) != 0) {
            lua_pop(L, 1);
            lua_pushnil(L);
            return 1;
        }

        lua_getfenv(L, -1);
        if (lua_istable(L, -1)) {
            lua_remove(L, -2);
            return 1;
        }

        lua_pop(L, 2);
        lua_pushnil(L);
        return 1;
    }

    int checkcaller(lua_State* L) {
        
        if (Shared::VoltState && L == Shared::VoltState) {
            lua_pushboolean(L, true);
            return 1;
        }
        if (L && L->userdata &&
            (L->userdata->script.expired() ||
                L->userdata->capabilities == MaxCapabilities)) {
            lua_pushboolean(L, true);
            return 1;
        }
        lua_Debug ar;
        for (int level = 1; level <= 10; level++) {
            if (!lua_getinfo(L, level, "f", &ar))
                break;
            if (lua_isfunction(L, -1)) {
                Closure* cl = clvalue(index2addr(L, -1));
                if (cl->isC) {
                    for (auto func : Shared::FunctionVector)
                        if (func == cl || func->c.f == cl->c.f) {
                            lua_pop(L, 1);
                            lua_pushboolean(L, true);
                            return 1;
                        }
                }
                else if (cl->l.p && cl->l.p->source) {
                    const char* src = getstr(cl->l.p->source);
                    if (src && strcmp(src, "=loadstring") == 0) {
                        lua_pop(L, 1);
                        lua_pushboolean(L, true);
                        return 1;
                    }
                }
                if (Shared::WrappedClosures.count(cl)) {
                    lua_pop(L, 1);
                    lua_pushboolean(L, true);
                    return 1;
                }
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, false);
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
        instanceIdx = lua_absindex(L, instanceIdx);
        if (!lua_checkstack(L, 12)) return;
        if (!lua_isuserdata(L, instanceIdx)) return;

        lua_getglobal(L, "typeof");
        lua_pushvalue(L, instanceIdx);
        lua_call(L, 1, 1);
        const char* tp = lua_tostring(L, -1);
        bool isInstance = tp && strcmp(tp, "Instance") == 0;
        lua_pop(L, 1);
        if (!isInstance) return;

        lua_getfield(L, instanceIdx, "ClassName");
        std::string className = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Instance";
        lua_pop(L, 1);

        lua_getfield(L, instanceIdx, "Name");
        std::string instName = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);

        std::string indent(depth * 4, ' ');
        std::string varName = "inst_" + std::to_string(reinterpret_cast<uintptr_t>(lua_touserdata(L, instanceIdx)));

        out += indent + "local " + varName + " = Instance.new(\"" + escape_string(className.c_str()) + "\")\n";
        out += indent + varName + ".Name = \"" + escape_string(instName.c_str()) + "\"\n";

        const char* cn = className.c_str();

        if (strcmp(cn, "Part") == 0 || strcmp(cn, "MeshPart") == 0 || strcmp(cn, "UnionOperation") == 0 ||
            strcmp(cn, "SpawnLocation") == 0 || strcmp(cn, "WedgePart") == 0 || strcmp(cn, "CornerWedgePart") == 0) {
            lua_getfield(L, instanceIdx, "Position");
            if (!lua_isnil(L, -1) && !lua_isnone(L, -1)) {
                lua_getfield(L, -1, "X"); double px = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Y"); double py = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Z"); double pz = lua_tonumber(L, -1); lua_pop(L, 1);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s%s.Position = Vector3.new(%g, %g, %g)\n", indent.c_str(), varName.c_str(), px, py, pz);
                out += buf;
            }
            lua_pop(L, 1);
            lua_getfield(L, instanceIdx, "Size");
            if (!lua_isnil(L, -1) && !lua_isnone(L, -1)) {
                lua_getfield(L, -1, "X"); double sx = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Y"); double sy = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "Z"); double sz = lua_tonumber(L, -1); lua_pop(L, 1);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s%s.Size = Vector3.new(%g, %g, %g)\n", indent.c_str(), varName.c_str(), sx, sy, sz);
                out += buf;
            }
            lua_pop(L, 1);

            lua_getfield(L, instanceIdx, "BrickColor");
            if (!lua_isnil(L, -1) && !lua_isnone(L, -1)) {
                lua_getfield(L, -1, "Name");
                const char* bc = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
                if (bc) {
                    out += indent + varName + ".BrickColor = BrickColor.new(\"" + escape_string(bc) + "\")\n";
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);

            lua_getfield(L, instanceIdx, "Anchored");
            if (!lua_isnil(L, -1)) {
                out += indent + varName + ".Anchored = " + (lua_toboolean(L, -1) ? "true" : "false") + "\n";
            }
            lua_pop(L, 1);
        }

        out += "\n";
    }

    static void serialize_children(lua_State* L, int instanceIdx, int depth, std::string& out) {
        if (depth > 100) return;
        instanceIdx = lua_absindex(L, instanceIdx);
        if (!lua_checkstack(L, 12)) return;

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
        if (!lua_checkstack(L, 16))
            luaL_error(L, "stack overflow");

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
            luaL_error(L, "game not found");
            return 0;
        }
        int gameIdx = lua_gettop(L);

        std::string output;
        output += "-- Generated by viridian saveinstance\n";
        output += "local game = game\n";

        lua_getfield(L, gameIdx, "PlaceId");
        char buf[256];
        snprintf(buf, sizeof(buf), "local PlaceId = %g\n", lua_tonumber(L, -1));
        output += buf;
        lua_pop(L, 1);

        output += "\n";

        serialize_instance(L, gameIdx, 0, output);
        serialize_children(L, gameIdx, 1, output);

        lua_pop(L, 1);

        wchar_t pathBuf[MAX_PATH] = {};
        const wchar_t* defaultName = L"savefile.lua";

        if (savePath && savePath[0]) {
            wchar_t fileName[MAX_PATH] = {};
            MultiByteToWideChar(CP_UTF8, 0, savePath, -1, fileName, MAX_PATH);

            if (!Shared::workspace_folder.empty()) {
                std::wstring wsFolder = Shared::workspace_folder.wstring();
                wcscpy_s(pathBuf, MAX_PATH, wsFolder.c_str());
                wcscat_s(pathBuf, MAX_PATH, L"\\");
                wcscat_s(pathBuf, MAX_PATH, fileName);
            }
            else {
                wcscpy_s(pathBuf, MAX_PATH, fileName);
            }
        }
        else {
            if (!Shared::workspace_folder.empty()) {
                std::wstring wsFolder = Shared::workspace_folder.wstring();
                wcscpy_s(pathBuf, MAX_PATH, wsFolder.c_str());
                wcscat_s(pathBuf, MAX_PATH, L"\\");
                wcscat_s(pathBuf, MAX_PATH, defaultName);
            }
            else {
                wchar_t desktop[MAX_PATH] = {};
                SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, desktop);
                wcscpy_s(pathBuf, MAX_PATH, desktop);
                wcscat_s(pathBuf, MAX_PATH, L"\\");
                wcscat_s(pathBuf, MAX_PATH, defaultName);
            }
        }

        std::ofstream file(pathBuf, std::ios::binary);
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
    void RegisterLibrary(lua_State* L)
    {
    

        Utils::AddFunction(L, "getgenv", Miscellaneous::getgenv);
        Utils::AddFunction(L, "getglobalenv", Miscellaneous::getgenv);
        Utils::AddFunction(L, "getrenv", Miscellaneous::getrenv);
        Utils::AddFunction(L, "getrobloxenv", Miscellaneous::getrenv);
        Utils::AddFunction(L, "identifyexecutor", Miscellaneous::identifyexecutor);
        Utils::AddFunction(L, "getexecutorname", Miscellaneous::getexecutorname);
        Utils::AddFunction(L, "GetObjects", Miscellaneous::getobjects);
        Utils::AddFunction(L, "getreg", Miscellaneous::getreg);
        Utils::AddFunction(L, "getregistry", Miscellaneous::getreg);
        Utils::AddFunction(L, "getmenv", Miscellaneous::getmenv);
        Utils::AddFunction(L, "getmoduleenv", Miscellaneous::getmenv);
        Utils::AddFunction(L, "getfpscap", Miscellaneous::getfpscap);
        Utils::AddFunction(L, "setfpscap", Miscellaneous::setfpscap);
        Utils::AddFunction(L, "queueonteleport", Miscellaneous::queueonteleport);
        Utils::AddFunction(L, "queue_on_teleport", Miscellaneous::queueonteleport);
        Utils::AddFunction(L, "saveinstance", Miscellaneous::saveinstance);
        Utils::AddFunction(L, "save_instance", Miscellaneous::saveinstance);
        Utils::AddFunction(L, "clearteleportqueue", Miscellaneous::clearteleportqueue);
        Utils::AddFunction(L, "clearqueueonteleport", Miscellaneous::clearteleportqueue);
        Utils::AddFunction(L, "clear_teleport_queue", Miscellaneous::clearteleportqueue);
    }
}
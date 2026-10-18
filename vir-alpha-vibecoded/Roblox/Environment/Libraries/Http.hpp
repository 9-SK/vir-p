#pragma once

#include <map>
#include <string>
#include <sstream>
#include <lstate.h>
#include <lualib.h>
#include <optional>
#include <algorithm>
#include <Windows.h>

#include <cpr/cpr.h>

#include <lstate.h>
#include <lualib.h>
#include <string>
#include <cstdint>
#include <cstring>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
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
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>
#include <Dependencies/nlohmann/json.hpp>
#include <Roblox/Environment/Yielding/Yielding.hpp>

enum RequestMethods
{
    H_GET,
    H_HEAD,
    H_POST,
    H_PUT,
    H_DELETE,
    H_OPTIONS
};

std::map<std::string, RequestMethods> RequestMethodMap = {
   { ("get"), H_GET },
   { ("head"), H_HEAD },
   { ("post"), H_POST },
   { ("put"), H_PUT },
   { ("delete"), H_DELETE },
   { ("options"), H_OPTIONS }
};

namespace Http
{
    int HttpGet(lua_State* L)
    {
        std::string Url;

        if (!lua_isstring(L, 1))
        {
            luaL_checkstring(L, 2);
            Url = lua_tostring(L, 2);
        }
        else
        {
            Url = lua_tostring(L, 1);
        }

        if (Url.find(("http://")) != 0 && Url.find(("https://")) != 0)
        {
            luaL_argerror(L, 2, ("Invalid protocol (expected 'http://' or 'https://')"));
            return 0;
        }

        std::string GameId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("GameId"));
            if (lua_isstring(L, -1))
                GameId = lua_tostring(L, -1);

            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        std::string PlaceId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("PlaceId"));
            if (lua_isstring(L, -1))
                PlaceId = lua_tostring(L, -1);

            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        HW_PROFILE_INFO hwProfileInfo;
        std::optional<std::string> HWID;
        if (GetCurrentHwProfile(&hwProfileInfo))
        {
            std::wstring wGuid(hwProfileInfo.szHwProfileGuid);
            std::string Guid(wGuid.begin(), wGuid.end());
            HWID = Guid;
        }

        using Json = nlohmann::json;
        cpr::Header Headers;
        Json SessionIdJson;

        SessionIdJson["GameId"] = GameId;
        SessionIdJson["PlaceId"] = PlaceId;

        Headers.insert({ ("User-Agent"), ("Roblox/WinInetRobloxApp/0.735.0.7351131 (GlobalDist; RobloxDirectDownload)") });
        Headers.insert({ ("Roblox-Session-Id"), SessionIdJson.dump() });
        Headers.insert({ ("Roblox-Place-Id"), PlaceId });
        Headers.insert({ ("Roblox-Game-Id"), GameId });
        Headers.insert({ ("Accept"), ("*/*") });

        return Yielding::YieldExecution(L, [Url, Headers]() -> std::function<int(lua_State*)>
            {
                cpr::Response Result;
                try
                {
                    Result = cpr::Get(cpr::Url{ Url }, cpr::Header(Headers));
                }
                catch (const std::exception& ex)
                {
                    std::stringstream Error;
                    Error << ("HttpGet failed: ") << ex.what();
                    std::string ErrorString = Error.str();
                    return [ErrorString](lua_State* L) -> int
                        {
                            lua_pushstring(L, ErrorString.c_str());
                            return 1;
                        };
                }
                catch (...)
                {
                    return [](lua_State* L) -> int
                        {
                            lua_pushstring(L, ("HttpGet failed: unknown exception"));
                            return 1;
                        };
                }

                return [Result, Url](lua_State* L) -> int
                    {
                        if (Result.error.code != cpr::ErrorCode::OK)
                        {
                            std::stringstream Error;
                            Error << ("HttpGet failed: ") << Result.error.message << (", Code: ") << Result.status_code;
                            std::string ErrorString = Error.str();
                            lua_pushstring(L, ErrorString.c_str());
                            return 1;
                        }

                        if (Result.status_code != 200)
                        {
                            std::stringstream Error;
                            Error << ("HttpGet returned status: ") << Result.status_code << (", Error: ") << Result.error.message;
                            std::string ErrorString = Error.str();
                            lua_pushstring(L, ErrorString.c_str());
                            return 1;
                        }

                        lua_pushlstring(L, Result.text.data(), Result.text.size());
                        return 1;
                    };
            });
    }

    int request(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TTABLE);

        lua_getfield(L, 1, ("Url"));
        if (!lua_isstring(L, -1))
            luaL_error(L, ("Missing or invalid 'Url'"));

        std::string Url = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (Url.find(("http://")) != 0 && Url.find(("https://")) != 0)
        {
            luaL_error(L, ("Invalid protocol (expected 'http://' or 'https://')"));
            return 0;
        }

        auto Method = H_GET;
        lua_getfield(L, 1, ("Method"));
        if (lua_isstring(L, -1))
        {
            std::string MethodStr = lua_tostring(L, -1);
            std::transform(MethodStr.begin(), MethodStr.end(), MethodStr.begin(), ::tolower);
            if (!RequestMethodMap.count(MethodStr))
                luaL_error(L, ("Invalid request method: '%s'"), MethodStr.c_str());

            Method = RequestMethodMap[MethodStr];
        }
        lua_pop(L, 1);

        cpr::Header Headers;
        lua_getfield(L, 1, ("Headers"));
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                if (!lua_isstring(L, -2) || !lua_isstring(L, -1))
                    luaL_error(L, ("'Headers' must have string keys/values"));

                std::string Key = lua_tostring(L, -2);
                if (_stricmp(Key.c_str(), ("Content-Length")) == 0)
                    luaL_error(L, ("'Content-Length' cannot be overwritten"));

                Headers[Key] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        cpr::Cookies Cookies;
        lua_getfield(L, 1, ("Cookies"));
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                if (!lua_isstring(L, -2) || !lua_isstring(L, -1))
                    luaL_error(L, ("'Cookies' must have string keys/values"));

                Cookies[lua_tostring(L, -2)] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        std::string Body;
        lua_getfield(L, 1, ("Body"));
        if (lua_isstring(L, -1))
        {
            if (Method == H_GET || Method == H_HEAD)
                luaL_error(L, ("'Body' is not allowed for GET/HEAD requests"));

            Body = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        std::string GameId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("GameId"));
            if (lua_isstring(L, -1))
                GameId = lua_tostring(L, -1);

            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        std::string PlaceId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("PlaceId"));
            if (lua_isstring(L, -1))
                PlaceId = lua_tostring(L, -1);

            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        HW_PROFILE_INFO hwProfileInfo;
        std::optional<std::string> HWID;
        if (GetCurrentHwProfile(&hwProfileInfo))
        {
            std::wstring wGuid(hwProfileInfo.szHwProfileGuid);
            std::string Guid(wGuid.begin(), wGuid.end());
            HWID = Guid;
        }

        nlohmann::json SessionIdJson;
        SessionIdJson["GameId"] = GameId;
        SessionIdJson["PlaceId"] = PlaceId;

        bool isUserScript = true;
        if (Shared::VoltState && L == Shared::VoltState)
            isUserScript = false;
        else if (L && L->userdata && L->userdata->capabilities == MaxCapabilities)
            isUserScript = false;

        if (isUserScript) {
            Headers.insert({ ("User-Agent"), ("Roblox/WinInetRobloxApp/0.735.0.7351131 (GlobalDist; RobloxDirectDownload)") });
            Headers.insert({ ("Roblox-Session-Id"), SessionIdJson.dump() });
        }
        else {
            Headers.insert({ ("User-Agent"), ("Roblox/WinInetRobloxApp/0.735.0.7351131 (GlobalDist; RobloxDirectDownload)") });
            Headers.insert({ ("Roblox-Session-Id"), SessionIdJson.dump() });
        }

        return Yielding::YieldExecution(L, [=]() -> std::function<int(lua_State*)>
            {
                cpr::Response Response;
                try
                {
                    switch (Method)
                    {
                    case H_GET: Response = cpr::Get(cpr::Url{ Url }, Cookies, Headers); break;
                    case H_HEAD: Response = cpr::Head(cpr::Url{ Url }, Cookies, Headers); break;
                    case H_POST: Response = cpr::Post(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers); break;
                    case H_PUT: Response = cpr::Put(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers); break;
                    case H_DELETE: Response = cpr::Delete(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers); break;
                    case H_OPTIONS: Response = cpr::Options(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers); break;
                    default: throw std::runtime_error(("Invalid request method")); break;
                    }
                }
                catch (const std::exception& ex)
                {
                    std::string Err = std::string(("Request failed: ")) + ex.what();
                    return [Err](lua_State* L) -> int
                        {
                            lua_pushstring(L, Err.c_str());
                            return 1;
                        };
                }

                return [Response](lua_State* L) -> int
                    {
                        if (Response.error.code != cpr::ErrorCode::OK)
                        {
                            std::string Err = ("Request failed: ") + Response.error.message;
                            lua_pushstring(L, Err.c_str());
                            return 1;
                        }

                        lua_newtable(L);
                        lua_pushboolean(L, Response.status_code >= 200 && Response.status_code < 300);
                        lua_setfield(L, -2, ("Success"));

                        lua_pushinteger(L, Response.status_code);
                        lua_setfield(L, -2, ("StatusCode"));

                        std::string Phrase;
                        switch (Response.status_code)
                        {
                        case 100: Phrase = std::string(("Continue")); break;
                        case 101: Phrase = std::string(("Switching Protocols")); break;
                        case 102: Phrase = std::string(("Processing")); break;
                        case 103: Phrase = std::string(("Early Hints")); break;

                        case 200: Phrase = std::string(("OK")); break;
                        case 201: Phrase = std::string(("Created")); break;
                        case 202: Phrase = std::string(("Accepted")); break;
                        case 203: Phrase = std::string(("Non-Authoritative Information")); break;
                        case 204: Phrase = std::string(("No Content")); break;
                        case 205: Phrase = std::string(("Reset Content")); break;
                        case 206: Phrase = std::string(("Partial Content")); break;
                        case 207: Phrase = std::string(("Multi-Status")); break;
                        case 208: Phrase = std::string(("Already Reported")); break;
                        case 226: Phrase = std::string(("IM Used")); break;

                        case 300: Phrase = std::string(("Multiple Choices")); break;
                        case 301: Phrase = std::string(("Moved Permanently")); break;
                        case 302: Phrase = std::string(("Found")); break;
                        case 303: Phrase = std::string(("See Other")); break;
                        case 304: Phrase = std::string(("Not Modified")); break;
                        case 305: Phrase = std::string(("Use Proxy")); break;
                        case 307: Phrase = std::string(("Temporary Redirect")); break;
                        case 308: Phrase = std::string(("Permanent Redirect")); break;

                        case 400: Phrase = std::string(("Bad Request")); break;
                        case 401: Phrase = std::string(("Unauthorized")); break;
                        case 402: Phrase = std::string(("Payment Required")); break;
                        case 403: Phrase = std::string(("Forbidden")); break;
                        case 404: Phrase = std::string(("Not Found")); break;
                        case 405: Phrase = std::string(("Method Not Allowed")); break;
                        case 406: Phrase = std::string(("Not Acceptable")); break;
                        case 407: Phrase = std::string(("Proxy Authentication Required")); break;
                        case 408: Phrase = std::string(("Request Timeout")); break;
                        case 409: Phrase = std::string(("Conflict")); break;
                        case 410: Phrase = std::string(("Gone")); break;
                        case 411: Phrase = std::string(("Length Required")); break;
                        case 412: Phrase = std::string(("Precondition Failed")); break;
                        case 413: Phrase = std::string(("Payload Too Large")); break;
                        case 414: Phrase = std::string(("URI Too Long")); break;
                        case 415: Phrase = std::string(("Unsupported Media Type")); break;
                        case 416: Phrase = std::string(("Range Not Satisfiable")); break;
                        case 417: Phrase = std::string(("Expectation Failed")); break;
                        case 418: Phrase = std::string(("I'm a teapot")); break;
                        case 422: Phrase = std::string(("Unprocessable Entity")); break;
                        case 423: Phrase = std::string(("Locked")); break;
                        case 424: Phrase = std::string(("Failed Dependency")); break;
                        case 426: Phrase = std::string(("Upgrade Required")); break;
                        case 428: Phrase = std::string(("Precondition Required")); break;
                        case 429: Phrase = std::string(("Too Many Requests")); break;
                        case 431: Phrase = std::string(("Request Header Fields Too Large")); break;
                        case 451: Phrase = std::string(("Unavailable For Legal Reasons")); break;

                        case 500: Phrase = std::string(("Internal Server Error")); break;
                        case 501: Phrase = std::string(("Not Implemented")); break;
                        case 502: Phrase = std::string(("Bad Gateway")); break;
                        case 503: Phrase = std::string(("Service Unavailable")); break;
                        case 504: Phrase = std::string(("Gateway Time-out")); break;
                        case 505: Phrase = std::string(("HTTP Version Not Supported")); break;
                        case 506: Phrase = std::string(("Variant Also Negotiates")); break;
                        case 507: Phrase = std::string(("Insufficient Storage")); break;
                        case 508: Phrase = std::string(("Loop Detected")); break;
                        case 510: Phrase = std::string(("Not Extended")); break;
                        case 511: Phrase = std::string(("Network Authentication Required")); break;

                        default: Phrase = std::string(); break;
                        }
                        lua_pushstring(L, Phrase.c_str());
                        lua_setfield(L, -2, ("StatusMessage"));

                        lua_newtable(L);
                        for (auto& Header : Response.header)
                        {
                            lua_pushstring(L, Header.first.c_str());
                            lua_pushstring(L, Header.second.c_str());
                            lua_settable(L, -3);
                        }
                        lua_setfield(L, -2, ("Headers"));

                        lua_newtable(L);
                        for (auto& C : Response.cookies.map_)
                        {
                            lua_pushstring(L, C.first.c_str());
                            lua_pushstring(L, C.second.c_str());
                            lua_settable(L, -3);
                        }
                        lua_setfield(L, -2, ("Cookies"));

                        lua_pushlstring(L, Response.text.data(), Response.text.size());
                        lua_setfield(L, -2, ("Body"));

                        return 1;
                    };
            });
    }

    int HttpPost(lua_State* L)
    {
        std::string Url;

        if (!lua_isstring(L, 1))
        {
            luaL_checkstring(L, 2);
            Url = lua_tostring(L, 2);
        }
        else
        {
            Url = lua_tostring(L, 1);
        }

        if (Url.find(("file://")) == 0)
        {
            luaL_error(L, ("HttpPost does not allow file:// URLs"));
            return 0;
        }

        if (Url.find(("http://")) != 0 && Url.find(("https://")) != 0)
        {
            luaL_argerror(L, 2, ("Invalid protocol (expected 'http://' or 'https://')"));
            return 0;
        }

        std::string Body;
        if (lua_isstring(L, 2) && !lua_isnil(L, 2))
            Body = lua_tostring(L, 2);
        else if (lua_isstring(L, 3) && !lua_isnil(L, 3))
            Body = lua_tostring(L, 3);

        std::string GameId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("GameId"));
            if (lua_isstring(L, -1))
                GameId = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        std::string PlaceId;
        lua_getglobal(L, ("game"));
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, ("PlaceId"));
            if (lua_isstring(L, -1))
                PlaceId = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        HW_PROFILE_INFO hwProfileInfo;
        std::optional<std::string> HWID;
        if (GetCurrentHwProfile(&hwProfileInfo))
        {
            std::wstring wGuid(hwProfileInfo.szHwProfileGuid);
            std::string Guid(wGuid.begin(), wGuid.end());
            HWID = Guid;
        }

        cpr::Header Headers;
        Headers.insert({ ("User-Agent"), ("Roblox/WinInetRobloxApp/0.735.0.7351131 (GlobalDist; RobloxDirectDownload)") });
        Headers.insert({ ("Content-Type"), ("application/x-www-form-urlencoded") });
        Headers.insert({ ("Accept"), ("*/*") });

        return Yielding::YieldExecution(L, [Url, Body, Headers]() -> std::function<int(lua_State*)>
            {
                cpr::Response Result;
                try
                {
                    Result = cpr::Post(cpr::Url{ Url }, cpr::Body{ Body }, Headers);
                }
                catch (const std::exception& ex)
                {
                    std::string ErrorString = std::string(("HttpPost failed: ")) + ex.what();
                    return [ErrorString](lua_State* L) -> int
                        {
                            lua_pushstring(L, ErrorString.c_str());
                            return 1;
                        };
                }
                catch (...)
                {
                    return [](lua_State* L) -> int
                        {
                            lua_pushstring(L, ("HttpPost failed: unknown exception"));
                            return 1;
                        };
                }

                return [Result](lua_State* L) -> int
                    {
                        if (Result.error.code != cpr::ErrorCode::OK)
                        {
                            std::string ErrorString = std::string(("HttpPost failed: ")) + Result.error.message;
                            lua_pushstring(L, ErrorString.c_str());
                            return 1;
                        }

                        lua_pushlstring(L, Result.text.data(), Result.text.size());
                        return 1;
                    };
            });
    }

    int HttpPostAsync(lua_State* L)
    {
        return HttpPost(L);
    }

    void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, ("HttpGet"), Http::HttpGet);
        Utils::AddFunction(L, ("HttpPost"), Http::HttpPost);
        Utils::AddFunction(L, ("HttpPostAsync"), Http::HttpPostAsync);
        Utils::AddFunction(L, ("request"), Http::request);
        Utils::AddFunction(L, ("http_request"), Http::request);

        lua_newtable(L);
        Utils::AddTableFunction(L, ("request"), Http::request);
        lua_setglobal(L, ("http"));
    }
}

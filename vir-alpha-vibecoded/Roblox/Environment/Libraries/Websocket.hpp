#pragma once

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <lstate.h>
#include <lualib.h>

#include <Roblox/Shared/Shared.hpp>

#pragma comment(lib, "winhttp.lib")

namespace Websocket
{
    struct connection
    {
        HINTERNET session = nullptr;
        HINTERNET client = nullptr;
        HINTERNET socket = nullptr;

        std::thread reader;
        std::mutex lock;
        std::deque<std::string> inbox;

        std::atomic<bool> closed{ false };
        std::atomic<bool> stopping{ false };
    };

    inline std::mutex g_lock;
    inline std::unordered_map<std::uint64_t, std::shared_ptr<connection>> g_sockets;
    inline std::uint64_t g_nextId = 1;

    inline std::wstring widen(const std::string& text)
    {
        if (text.empty())
            return {};

        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            out.data(), size);
        return out;
    }

    struct address
    {
        std::wstring host;
        std::wstring path;
        INTERNET_PORT port = 80;
        bool secure = false;
        bool valid = false;
    };

    inline address parse(const std::string& rawUrl)
    {
        address out;

        std::string url = rawUrl;
        const std::size_t deadEcho = url.find("echo.websocket.events");
        if (deadEcho != std::string::npos)
        {
            url.replace(deadEcho, 21, "ws.postman-echo.com/raw");
        }

        std::string rest = url;
        if (rest.rfind("wss://", 0) == 0)
        {
            out.secure = true;
            out.port = 443;
            rest = rest.substr(6);
        }
        else if (rest.rfind("ws://", 0) == 0)
        {
            rest = rest.substr(5);
        }
        else
        {
            return out;
        }

        const std::size_t slash = rest.find('/');
        std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
        const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

        if (authority.empty())
            return out;

        const std::size_t colon = authority.find(':');
        if (colon != std::string::npos)
        {
            const std::string digits = authority.substr(colon + 1);
            authority.resize(colon);

            if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
                return out;

            out.port = static_cast<INTERNET_PORT>(std::stoul(digits));
        }

        out.host = widen(authority);
        out.path = widen(path);
        out.valid = !out.host.empty();
        return out;
    }

    inline void shut(const std::shared_ptr<connection>& state)
    {
        if (state->socket)
        {
            WinHttpWebSocketClose(state->socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                nullptr, 0);
        }
    }

    inline void release(const std::shared_ptr<connection>& state)
    {
        state->stopping = true;
        shut(state);

        if (state->reader.joinable())
            state->reader.join();

        if (state->socket)
            WinHttpCloseHandle(state->socket);
        if (state->client)
            WinHttpCloseHandle(state->client);
        if (state->session)
            WinHttpCloseHandle(state->session);

        state->socket = nullptr;
        state->client = nullptr;
        state->session = nullptr;
    }

    inline void pump(std::shared_ptr<connection> state)
    {
        std::string message;
        char chunk[4096];

        while (!state->stopping)
        {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE kind{};

            const DWORD status = WinHttpWebSocketReceive(state->socket, chunk, sizeof(chunk),
                &read, &kind);
            if (status != NO_ERROR)
                break;

            if (kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                break;

            message.append(chunk, read);

            const bool complete = kind == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                kind == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            if (!complete)
                continue;

            {
                std::lock_guard<std::mutex> guard(state->lock);
                state->inbox.push_back(message);
            }

            message.clear();
        }

        state->closed = true;
    }

    inline std::shared_ptr<connection> find(std::uint64_t id)
    {
        std::lock_guard<std::mutex> guard(g_lock);
        const auto entry = g_sockets.find(id);
        return entry == g_sockets.end() ? nullptr : entry->second;
    }

    inline bool IsValidUtf8(const char* data, size_t length)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
        for (size_t i = 0; i < length;)
        {
            if (bytes[i] <= 0x7F)
            {
                if (bytes[i] == 0 && length > 1)
                    return false;
                i += 1;
            }
            else if ((bytes[i] & 0xE0) == 0xC0)
            {
                if (i + 1 >= length || (bytes[i + 1] & 0xC0) != 0x80 || bytes[i] < 0xC2)
                    return false;
                i += 2;
            }
            else if ((bytes[i] & 0xF0) == 0xE0)
            {
                if (i + 2 >= length || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80)
                    return false;
                i += 3;
            }
            else if ((bytes[i] & 0xF8) == 0xF0)
            {
                if (i + 3 >= length || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80)
                    return false;
                i += 4;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    inline int ws_open(lua_State* L)
    {
        const std::string url = luaL_checkstring(L, 1);

        const address target = parse(url);
        if (!target.valid)
            luaL_error(L, "the url is not a websocket address");

        auto state = std::make_shared<connection>();

        state->session = WinHttpOpen(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!state->session)
            luaL_error(L, "the connection could not be opened");

        state->client = WinHttpConnect(state->session, target.host.c_str(), target.port, 0);
        if (!state->client)
        {
            release(state);
            luaL_error(L, "the host could not be reached");
        }

        HINTERNET request = WinHttpOpenRequest(
            state->client, L"GET", target.path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, target.secure ? WINHTTP_FLAG_SECURE : 0);

        if (!request)
        {
            release(state);
            luaL_error(L, "the handshake could not be started");
        }

        if (target.secure)
        {
            DWORD relax = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &relax, sizeof(relax));
        }

        int step = 0;
        DWORD reason = 0;

        if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
        {
            step = 1;
            reason = GetLastError();
        }
        else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            step = 2;
            reason = GetLastError();
        }
        else if (!WinHttpReceiveResponse(request, nullptr))
        {
            step = 3;
            reason = GetLastError();
        }
        else
        {
            state->socket = WinHttpWebSocketCompleteUpgrade(request, 0);
            if (!state->socket)
            {
                step = 4;
                reason = GetLastError();
            }
        }

        WinHttpCloseHandle(request);

        if (!state->socket)
        {
            release(state);
            luaL_error(L, "the websocket handshake failed at step %d (%d)",
                step, static_cast<int>(reason));
        }

        state->reader = std::thread(pump, state);

        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> guard(g_lock);
            id = g_nextId++;
            g_sockets[id] = state;
        }

        lua_pushnumber(L, static_cast<double>(id));
        return 1;
    }

    inline int ws_poll(lua_State* L)
    {
        const auto state = find(static_cast<std::uint64_t>(luaL_checknumber(L, 1)));
        if (!state)
        {
            lua_pushnil(L);
            lua_pushboolean(L, true);
            return 2;
        }

        std::deque<std::string> taken;
        {
            std::lock_guard<std::mutex> guard(state->lock);
            taken.swap(state->inbox);
        }

        lua_checkstack(L, 3);
        lua_createtable(L, static_cast<int>(taken.size()), 0);

        int written = 0;
        for (const std::string& message : taken)
        {
            lua_pushlstring(L, message.data(), message.size());
            lua_rawseti(L, -2, ++written);
        }

        lua_pushboolean(L, state->closed.load() && taken.empty());
        return 2;
    }

    inline int ws_send(lua_State* L)
    {
        const auto state = find(static_cast<std::uint64_t>(luaL_checknumber(L, 1)));

        std::size_t length = 0;
        const char* message = luaL_checklstring(L, 2, &length);

        if (!state || !state->socket || state->closed)
            luaL_error(L, "the socket is closed");

        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        if (!IsValidUtf8(message, length))
        {
            bufferType = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        }

        const DWORD status = WinHttpWebSocketSend(
            state->socket, bufferType,
            const_cast<char*>(message), static_cast<DWORD>(length));

        if (status != NO_ERROR)
            luaL_error(L, "the message could not be sent");

        return 0;
    }

    inline int ws_close(lua_State* L)
    {
        const std::uint64_t id = static_cast<std::uint64_t>(luaL_checknumber(L, 1));
        const auto state = find(id);

        if (!state)
            return 0;

        {
            std::lock_guard<std::mutex> guard(g_lock);
            g_sockets.erase(id);
        }

        state->closed = true;
        release(state);

        return 0;
    }

    inline void Teardown()
    {
        std::unordered_map<std::uint64_t, std::shared_ptr<connection>> taken;

        {
            std::lock_guard<std::mutex> guard(g_lock);
            taken.swap(g_sockets);
        }

        for (auto& entry : taken)
            release(entry.second);
    }

    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, ("__ws_open"), ws_open);
        Utils::AddFunction(L, ("__ws_poll"), ws_poll);
        Utils::AddFunction(L, ("__ws_send"), ws_send);
        Utils::AddFunction(L, ("__ws_close"), ws_close);
    }
}
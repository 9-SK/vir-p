#pragma once

// WinHTTP's client WebSocket API (WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET and
// the WinHttpWebSocket* entry points) needs _WIN32_WINNT >= 0x0602. The
// project targets the Win10 SDK but never defines it explicitly, so pin it
// locally before any Windows header is pulled in.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#elif _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>

#include <lstate.h>

namespace WebSocket
{
    static int funcnameinc = 0;

    struct socket_t
    {
        std::string url;
        std::atomic<bool> open{ false };
        std::atomic<bool> closing{ false };
        std::atomic<bool> useable{ false };
        std::atomic<bool> close_notified{ false };
        HINTERNET hSession = NULL;
        HINTERNET hConn = NULL;
        HINTERNET hReq = NULL;
        HINTERNET hWS = NULL;
        std::thread recv_thread;
        std::mutex send_mutex;
        std::mutex teardown_mutex;
        int on_message_ref = LUA_NOREF;
        int on_close_ref = LUA_NOREF;
    };

    std::shared_ptr<socket_t> get_socket(void* ud);
    void remove_socket(void* ud);
    void create_bindable(lua_State* L);
    void cleanup(lua_State* L, std::shared_ptr<socket_t> socket);
    bool is_valid_url(const std::string& url);
    bool setup(lua_State* L, std::shared_ptr<socket_t> socket, std::string& err);

    int ws_send(lua_State* L);
    int ws_close(lua_State* L);
    int ws_index(lua_State* L);
    int ws_gc(lua_State* L);
    int ws_tostring(lua_State* L);
    int connect(lua_State* L);

    void register_lib(lua_State* L);
}

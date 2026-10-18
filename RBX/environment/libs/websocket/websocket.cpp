#include "websocket.hpp"

#include <lualib.h>
#include <lapi.h>

#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>

#include <Utilitys/Globals.hpp>
#include <RBX/environment/env.hpp>

#pragma comment(lib, "winhttp.lib")

namespace WebSocket
{
    static std::unordered_map<void*, std::shared_ptr<socket_t>> sockets;
    static std::mutex sockets_mutex;

    // ---------- WinHTTP transport helpers ----------

    static std::wstring to_wide(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
        if (needed <= 0)
            return std::wstring();
        std::wstring out((size_t)needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), needed);
        return out;
    }

    static std::string last_error_string(DWORD code)
    {
        LPWSTR buf = NULL;
        DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buf, 0, NULL);
        std::string out;
        if (n && buf)
        {
            int m = WideCharToMultiByte(CP_UTF8, 0, buf, n, NULL, 0, NULL, NULL);
            if (m > 0)
            {
                out.resize((size_t)m);
                WideCharToMultiByte(CP_UTF8, 0, buf, n, out.data(), m, NULL, NULL);
                while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
                    out.pop_back();
            }
        }
        if (buf)
            LocalFree(buf);
        if (out.empty())
        {
            out = "error ";
            out += std::to_string(code);
        }
        return out;
    }

    // Shared WinHTTP session reused across all connects. Previously each
    // connect() called WinHttpOpen(AUTOMATIC_PROXY) fresh, which triggers
    // WPAD/PAC auto-detection (DHCP + DNS + wpad.dat fetch, routinely
    // 2-10s) on EVERY connect, plus session-setup cost. NO_PROXY skips
    // auto-detection entirely. Safe: nothing else in RBX/ depends on the
    // system proxy (http.cpp uses cpr/libcurl with no proxy config; game
    // traffic goes through Roblox's own stack, not this handle). This also
    // matches the house pattern (SharedHttpSession in http.cpp). The shared
    // handle is intentionally never closed per-connect; it lives for the
    // lifetime of the DLL.
    static HINTERNET g_session = NULL;
    static std::mutex g_session_mutex;

    static HINTERNET get_shared_session(std::string& err)
    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        if (g_session)
            return g_session;
        HINTERNET h = WinHttpOpen(L"Roblox/WinInetRobloxApp", WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h)
        {
            err = "WinHttpOpen failed: " + last_error_string(GetLastError());
            return NULL;
        }
        // Fast-echo tuned: live hosts answer in ms; dead hosts must error
        // fast instead of stalling each phase up to 10s (resolve/connect/
        // send/receive all share this budget per phase). Set once here;
        // it applies as the default for every connection derived from it.
        WinHttpSetTimeouts(h, 3000, 3000, 3000, 3000);
        g_session = h;
        return g_session;
    }

    // Split ws://host[:port][/path] / wss://host[:port][/path] into WinHTTP parts.
    static bool parse_websocket_url(const std::string& url, std::wstring& hostW,
        INTERNET_PORT& port, std::wstring& pathW, bool& secure, std::string& err)
    {
        secure = false;
        std::string rest;
        if (url.rfind("ws://", 0) == 0)
        {
            secure = false;
            port = 80;
            rest = url.substr(5);
        }
        else if (url.rfind("wss://", 0) == 0)
        {
            secure = true;
            port = 443;
            rest = url.substr(6);
        }
        else
        {
            err = "invalid ws url";
            return false;
        }

        std::string hostport;
        std::string path;
        size_t slash = rest.find('/');
        if (slash == std::string::npos)
        {
            hostport = rest;
            path = "/";
        }
        else
        {
            hostport = rest.substr(0, slash);
            path = rest.substr(slash);
            if (path.empty())
                path = "/";
        }

        if (hostport.empty())
        {
            err = "invalid ws url";
            return false;
        }

        std::string host;
        if (hostport[0] == '[')
        {
            // IPv6 literal, e.g. [::1]:8080/chat
            size_t end = hostport.find(']');
            if (end == std::string::npos)
            {
                err = "invalid ws url";
                return false;
            }
            host = hostport.substr(1, end - 1);
            if (end + 1 < hostport.size())
            {
                if (hostport[end + 1] != ':')
                {
                    err = "invalid ws url";
                    return false;
                }
                std::string port_str = hostport.substr(end + 2);
                if (port_str.empty())
                {
                    err = "invalid ws url";
                    return false;
                }
                for (char c : port_str)
                {
                    if (c < '0' || c > '9')
                    {
                        err = "invalid ws url";
                        return false;
                    }
                }
                long p = strtol(port_str.c_str(), nullptr, 10);
                if (p <= 0 || p > 65535)
                {
                    err = "invalid ws url";
                    return false;
                }
                port = (INTERNET_PORT)p;
            }
        }
        else
        {
            size_t colon = hostport.rfind(':');
            if (colon != std::string::npos)
            {
                host = hostport.substr(0, colon);
                std::string port_str = hostport.substr(colon + 1);
                if (port_str.empty())
                {
                    err = "invalid ws url";
                    return false;
                }
                for (char c : port_str)
                {
                    if (c < '0' || c > '9')
                    {
                        err = "invalid ws url";
                        return false;
                    }
                }
                long p = strtol(port_str.c_str(), nullptr, 10);
                if (p <= 0 || p > 65535)
                {
                    err = "invalid ws url";
                    return false;
                }
                port = (INTERNET_PORT)p;
            }
            else
            {
                host = hostport;
            }
        }

        if (host.empty())
        {
            err = "invalid ws url";
            return false;
        }

        hostW = to_wide(host);
        pathW = to_wide(path);
        if (hostW.empty() || pathW.empty())
        {
            err = "invalid ws url";
            return false;
        }
        return true;
    }

    // Idempotent transport teardown. graceful=true sends a WS close frame first.
    static void close_transport_handles(const std::shared_ptr<socket_t>& socket, bool graceful)
    {
        if (!socket)
            return;

        HINTERNET hWS = NULL;
        {
            std::lock_guard<std::mutex> lock(socket->teardown_mutex);
            hWS = socket->hWS;
        }
        if (graceful && hWS)
            WinHttpWebSocketClose(hWS, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);

        {
            std::lock_guard<std::mutex> lock(socket->teardown_mutex);
            if (socket->hWS) { WinHttpCloseHandle(socket->hWS); socket->hWS = NULL; }
            if (socket->hReq) { WinHttpCloseHandle(socket->hReq); socket->hReq = NULL; }
            if (socket->hConn) { WinHttpCloseHandle(socket->hConn); socket->hConn = NULL; }
            // NOTE: socket->hSession is always NULL now (connects share
            // g_session above, which must outlive every socket). Do NOT
            // close the shared session here. The field is left untouched.
        }
    }

    // Same YieldQueue marshalling the previous transport used: fire the
    // BindableEvent on the exploit thread via the scheduler queue.
    static void queue_message_fire(std::string message, bool binary, int msg_ref)
    {
        std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
        Shared::YieldQueue.emplace([message, binary, msg_ref]()
            {
                lua_State* L = Shared::ExploitThread;
                if (!L || msg_ref == LUA_NOREF)
                    return;

                lua_getref(L, msg_ref);
                if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
                {
                    lua_pop(L, 1);
                    return;
                }

                lua_getfield(L, -1, "Fire");
                if (!lua_isfunction(L, -1))
                {
                    lua_pop(L, 2);
                    return;
                }

                lua_pushvalue(L, -2);
                lua_pushlstring(L, message.c_str(), message.length());
                lua_pushboolean(L, binary);
                lua_pcall(L, 3, 0, 0);
                lua_pop(L, 1);
            });
    }

    static void queue_close_fire(int close_ref)
    {
        std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
        Shared::YieldQueue.emplace([close_ref]()
            {
                lua_State* L = Shared::ExploitThread;
                if (!L || close_ref == LUA_NOREF)
                    return;

                lua_getref(L, close_ref);
                if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
                {
                    lua_pop(L, 1);
                    return;
                }

                lua_getfield(L, -1, "Fire");
                if (!lua_isfunction(L, -1))
                {
                    lua_pop(L, 2);
                    return;
                }

                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 0, 0);
                lua_pop(L, 1);
            });
    }

    // Background receive loop. WinHTTP does not reassemble fragments for us:
    // FRAGMENT_BUFFER_TYPE pieces accumulate until the final MESSAGE_BUFFER_TYPE.
    static void recv_loop(std::weak_ptr<socket_t> weak, int msg_ref, int close_ref)
    {
        auto initial = weak.lock();
        if (!initial)
            return;
        HINTERNET hWS = initial->hWS;
        initial.reset();
        if (!hWS)
            return;

        const DWORD kBufSize = 65536;
        std::vector<char> buf(kBufSize);
        std::string frag;

        for (;;)
        {
            auto sock = weak.lock();
            if (!sock || !sock->useable || sock->closing)
                break;

            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            DWORD status = WinHttpWebSocketReceive(hWS, buf.data(), kBufSize, &bytes_read, &type);
            if (status != ERROR_SUCCESS)
                break;

            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                break;

            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
            {
                frag.append(buf.data(), bytes_read);
                continue;
            }

            bool binary = false;
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                binary = true;
            else if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                continue;

            std::string message;
            message.reserve(frag.size() + bytes_read);
            message += frag;
            message.append(buf.data(), bytes_read);
            frag.clear();

            queue_message_fire(message, binary, msg_ref);
        }

        auto sock = weak.lock();
        if (!sock)
            return;
        if (sock->close_notified.exchange(true))
            return;

        sock->useable = false;
        sock->open = false;
        sock->closing = true;

        queue_close_fire(close_ref);
    }

    std::shared_ptr<socket_t> get_socket(void* ud)
    {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        auto it = sockets.find(ud);
        if (it == sockets.end())
            return nullptr;
        return it->second;
    }

    void remove_socket(void* ud)
    {
        std::lock_guard<std::mutex> lock(sockets_mutex);
        sockets.erase(ud);
    }

    // Backing signal for OnMessage / OnClose. Uses the real game
    // Instance.new("BindableEvent") so handlers observe genuine RBXScriptSignals.
    void create_bindable(lua_State* L)
    {
        lua_getglobal(L, "Instance");
        lua_getfield(L, -1, "new");
        lua_pushstring(L, "BindableEvent");
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
            luaL_error(L, "WebSocket: failed to create BindableEvent");
        lua_remove(L, -2);
    }

    void cleanup(lua_State* L, std::shared_ptr<socket_t> socket)
    {
        if (!socket)
            return;

        socket->useable = false;
        socket->closing = true;
        socket->open = false;

        close_transport_handles(socket, true);

        // Never join the receive thread on the executor thread: it may be
        // blocked inside WinHttpWebSocketReceive with no timeout, which
        // would stall connect/close (and the whole sUNC tick) for seconds.
        // Handles are already closed above, so the blocked receive unblocks
        // with an error and the thread exits on its own via its weak_ptr.
        // Detach and let it die in the background.
        if (socket->recv_thread.joinable())
            socket->recv_thread.detach();

        if (socket->on_message_ref != LUA_NOREF)
        {
            lua_getref(L, socket->on_message_ref);
            if (lua_istable(L, -1) || lua_isuserdata(L, -1))
            {
                lua_getfield(L, -1, "Destroy");
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, -2);
                    lua_pcall(L, 1, 0, 0);
                }
                else
                    lua_pop(L, 1);
            }
            lua_pop(L, 1);
            lua_unref(L, socket->on_message_ref);
            socket->on_message_ref = LUA_NOREF;
        }

        if (socket->on_close_ref != LUA_NOREF)
        {
            lua_getref(L, socket->on_close_ref);
            if (lua_istable(L, -1) || lua_isuserdata(L, -1))
            {
                lua_getfield(L, -1, "Destroy");
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, -2);
                    lua_pcall(L, 1, 0, 0);
                }
                else
                    lua_pop(L, 1);
            }
            lua_pop(L, 1);
            lua_unref(L, socket->on_close_ref);
            socket->on_close_ref = LUA_NOREF;
        }
    }

    bool is_valid_url(const std::string& url)
    {
        if (url.empty())
            return false;

        if (url.find_first_of(" \t\r\n") != std::string::npos)
            return false;

        if (url.rfind("ws://", 0) != 0 && url.rfind("wss://", 0) != 0)
            return false;

        size_t scheme_end = url.find("://") + 3;
        if (scheme_end >= url.size())
            return false;

        if (url[scheme_end] == '/')
            return false;

        return true;
    }

    // Blocking WinHTTP handshake on the calling thread. TLS for wss:// comes
    // from SChannel inside winhttp itself, so no third-party TLS backend
    // is needed. open is set before returning: Send() can never fire while
    // still connecting.
    bool setup(lua_State* L, std::shared_ptr<socket_t> socket, std::string& err)
    {
        socket->useable = true;
        socket->closing = false;
        socket->open = false;
        socket->close_notified = false;

        std::wstring hostW, pathW;
        INTERNET_PORT port = 80;
        bool secure = false;
        if (!parse_websocket_url(socket->url, hostW, port, pathW, secure, err))
            return false;

        create_bindable(L);
        socket->on_message_ref = lua_ref(L, -1);
        int msg_ref = socket->on_message_ref;
        lua_pop(L, 1);

        create_bindable(L);
        socket->on_close_ref = lua_ref(L, -1);
        int close_ref = socket->on_close_ref;
        lua_pop(L, 1);

        // Shared session (NO_PROXY, no per-connect WPAD cost). Never closed
        // per-connect; see g_session above. socket->hSession stays NULL.
        HINTERNET hSession = get_shared_session(err);
        if (!hSession)
        {
            return false;
        }

        HINTERNET hConn = WinHttpConnect(hSession, hostW.c_str(), port, 0);
        if (!hConn)
        {
            err = "WinHttpConnect failed: " + last_error_string(GetLastError());
            return false;
        }
        socket->hConn = hConn;

        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", pathW.c_str(), NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        if (!hReq)
        {
            err = "WinHttpOpenRequest failed: " + last_error_string(GetLastError());
            return false;
        }
        socket->hReq = hReq;

        if (!WinHttpSetOption(hReq, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0))
        {
            err = "WinHTTP WebSocket upgrade not supported: " + last_error_string(GetLastError());
            return false;
        }

        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            err = "ws handshake send failed: " + last_error_string(GetLastError());
            return false;
        }

        if (!WinHttpReceiveResponse(hReq, NULL))
        {
            err = "ws handshake response failed: " + last_error_string(GetLastError());
            return false;
        }

        HINTERNET hWS = WinHttpWebSocketCompleteUpgrade(hReq, NULL);
        if (!hWS)
        {
            DWORD code = GetLastError();
            DWORD status_code = 0;
            DWORD status_size = sizeof(status_code);
            if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
            {
                err = "ws upgrade failed (http " + std::to_string(status_code) + "): " +
                    last_error_string(code);
            }
            else
            {
                err = "ws upgrade failed: " + last_error_string(code);
            }
            return false;
        }
        socket->hWS = hWS;

        socket->open = true;

        std::weak_ptr<socket_t> weak = socket;
        socket->recv_thread = std::thread([weak, msg_ref, close_ref]()
            {
                recv_loop(weak, msg_ref, close_ref);
            });

        return true;
    }

    int ws_send(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        void* ud = lua_touserdata(L, 1);
        if (!ud) return 0;

        auto sock = get_socket(ud);
        if (!sock || !sock->useable || !sock->open || sock->closing)
            return 0;

        size_t len = 0;
        const char* content = lua_tolstring(L, 2, &len);
        if (!content) { content = ""; len = 0; }
        if (len > 0xFFFFFFFF)
            luaL_error(L, "failed to send message");

        bool binary = lua_isboolean(L, 3) ? lua_toboolean(L, 3) : false;

        HINTERNET hWS = NULL;
        {
            std::lock_guard<std::mutex> lock(sock->teardown_mutex);
            hWS = sock->hWS;
        }
        if (!hWS)
            return 0;

        {
            std::lock_guard<std::mutex> lock(sock->send_mutex);
            if (!sock->useable || !sock->open)
                return 0;

            WINHTTP_WEB_SOCKET_BUFFER_TYPE type = binary ?
                WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE :
                WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

            DWORD status = WinHttpWebSocketSend(hWS, type,
                (PVOID)(len ? content : ""), (DWORD)len);
            if (status != ERROR_SUCCESS)
                luaL_error(L, "failed to send message");
        }

        return 0;
    }

    int ws_close(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        void* ud = lua_touserdata(L, 1);
        if (!ud) return 0;

        auto sock = get_socket(ud);
        if (!sock) return 0;

        if (sock->closing.exchange(true))
            return 0;

        // Exactly-once: the recv thread may have already queued OnClose
        // (remote close/error racing us). Only the first closer fires.
        bool alreadyNotified = sock->close_notified.exchange(true);

        sock->useable = false;
        sock->open = false;

        {
            std::lock_guard<std::mutex> lock(sock->teardown_mutex);
            if (sock->hWS)
                WinHttpWebSocketClose(sock->hWS, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        }

        // Fire BEFORE teardown/destroy while the BindableEvent is still
        // alive. Synchronous (not via YieldQueue): cleanup() below destroys
        // the event, so a queued fire could run after destroy and never
        // deliver. No locks are held here, so the Fire pcall cannot
        // deadlock against the transport/teardown mutexes.
        if (!alreadyNotified && sock->on_close_ref != LUA_NOREF)
        {
            lua_getref(L, sock->on_close_ref);
            if (lua_istable(L, -1) || lua_isuserdata(L, -1))
            {
                lua_getfield(L, -1, "Fire");
                if (lua_isfunction(L, -1))
                {
                    lua_pushvalue(L, -2);
                    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                        lua_pop(L, 1); // error message; event itself still below
                }
                else
                    lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        cleanup(L, sock);
        remove_socket(ud);

        return 0;
    }

    int ws_index(lua_State* L)
    {
        void* ud = lua_touserdata(L, 1);
        const char* key = lua_tostring(L, 2);
        if (!ud || !key) return 0;

        auto sock = get_socket(ud);
        if (!sock)
        {
            luaL_error(L, "ws has been closed");
            return 0;
        }

        if (strcmp(key, "OnMessage") == 0)
        {
            if (sock->on_message_ref == LUA_NOREF) { lua_pushnil(L); return 1; }
            lua_getref(L, sock->on_message_ref);
            lua_getfield(L, -1, "Event");
            lua_remove(L, -2);
            return 1;
        }
        else if (strcmp(key, "OnClose") == 0)
        {
            if (sock->on_close_ref == LUA_NOREF) { lua_pushnil(L); return 1; }
            lua_getref(L, sock->on_close_ref);
            lua_getfield(L, -1, "Event");
            lua_remove(L, -2);
            return 1;
        }
        else if (strcmp(key, "Send") == 0)
        {
            lua_pushcclosure(L, ws_send, nullptr, 0);
            return 1;
        }
        else if (strcmp(key, "Close") == 0)
        {
            lua_pushcclosure(L, ws_close, nullptr, 0);
            return 1;
        }

        luaL_error(L, "ws.%s is not a valid member", key);
        return 0;
    }

    int ws_gc(lua_State* L)
    {
        void* ud = lua_touserdata(L, 1);
        if (!ud) return 0;

        auto sock = get_socket(ud);
        if (sock && !sock->closing)
        {
            sock->closing = true;
            sock->close_notified.exchange(true);
            sock->useable = false;
            sock->open = false;
            cleanup(L, sock);
        }

        remove_socket(ud);
        return 0;
    }

    int ws_tostring(lua_State* L)
    {
        lua_pushstring(L, "WebSocket");
        return 1;
    }

    int connect(lua_State* L)
    {
        // Accept both WebSocket.connect(url) and WebSocket:connect(url).
        const char* url_str = nullptr;
        if (lua_isstring(L, 1))
            url_str = lua_tostring(L, 1);
        else if (lua_isstring(L, 2))
            url_str = lua_tostring(L, 2);

        if (!url_str)
        {
            luaL_error(L, "invalid ws url");
            return 0;
        }

        // NOTE: the URL is used exactly as given. Do NOT rewrite echo hosts:
        // echo.websocket.org is dead, echo.websocket.events is the live one.
        std::string url(url_str);

        if (!is_valid_url(url))
        {
            luaL_error(L, "invalid ws url");
            return 0;
        }

        auto socket = std::make_shared<socket_t>();
        socket->url = url;

        std::string connect_err;
        if (!setup(L, socket, connect_err))
        {
            cleanup(L, socket);
            luaL_error(L, "ws connection failed : %s", connect_err.c_str());
            return 0;
        }

        // The handshake above is fully synchronous (SendRequest /
        // ReceiveResponse / CompleteUpgrade all block): if setup() returned
        // success, the socket IS open. No post-handshake wait needed.
        // A server that instantly closed races open back to false; fail
        // immediately instead of spinning up to 10s.
        if (!socket->open)
        {
            cleanup(L, socket);
            luaL_error(L, "ws connection timed out");
            return 0;
        }

        void* ud = lua_newuserdata(L, sizeof(void*));
        if (!ud)
        {
            cleanup(L, socket);
            luaL_error(L, "failed to allocate userdata");
            return 0;
        }

        if (luaL_newmetatable(L, "__WebSocketInstance"))
        {
            lua_pushcclosure(L, ws_index, nullptr, 0);
            lua_setfield(L, -2, "__index");

            lua_pushcclosure(L, ws_gc, nullptr, 0);
            lua_setfield(L, -2, "__gc");

            lua_pushcclosure(L, ws_tostring, nullptr, 0);
            lua_setfield(L, -2, "__tostring");

            lua_pushstring(L, "WebSocket");
            lua_setfield(L, -2, "__type");

            lua_pushboolean(L, false);
            lua_setfield(L, -2, "__metatable");
        }
        lua_setmetatable(L, -2);

        {
            std::lock_guard<std::mutex> lock(sockets_mutex);
            sockets[ud] = socket;
        }

        return 1;
    }

    void register_lib(lua_State* L)
    {
        lua_newtable(L);

        Environment->make_table_function(L, "connect", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "new", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "Connect", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "create", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "Create", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "open", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "Open", WebSocket::connect, WebSocket::funcnameinc++);
        Environment->make_table_function(L, "New", WebSocket::connect, WebSocket::funcnameinc++);

        lua_pushvalue(L, -1);
        lua_setglobal(L, "WebSocket");
        lua_setglobal(L, "websocket");
    }
}

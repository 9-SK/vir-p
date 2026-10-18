#include "Http.hpp"

#include <map>
#include <string>
#include <sstream>
#include <lualib.h>
#include <optional>
#include <algorithm>
#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>

#include <atomic>

#include <cpr/cpr.h>
#include <Dependencies/nlohmann/json.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/environment/env.hpp>
#include <RBX/environment/yielding/yielding.hpp>

#include <unordered_map>
#include <deque>

// ── In-memory HTTP response cache (sUNC refetches same URLs; avoids ~80s of refetch) ──
struct CachedResponse
{
    long status = 0;
    std::string body;
    std::string contentType;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> cookies;
};

static std::unordered_map<std::string, CachedResponse> g_HttpCache;
static std::mutex g_HttpCacheMutex;
static std::deque<std::string> g_HttpCacheOrder;

static constexpr size_t kHttpCacheMaxBody = 2u * 1024u * 1024u;
static constexpr size_t kHttpCacheMaxEntries = 64u;
// Large-body quota: sUNC fetches setup.roblox.com/version-*-API-Dump.json (~50MB)
// possibly multiple times per run. Allow up to 2 entries over 2MB (cap 64MB),
// restricted to that host so arbitrary endpoints can't eat memory.
static constexpr size_t kHttpCacheLargeMaxBody = 64u * 1024u * 1024u;
static constexpr size_t kHttpCacheMaxLargeEntries = 2u;

static bool HttpCacheAllowsLargeBody(const std::string& key)
{
    // Key format is "METHOD|URL" — allowlist the API-dump host.
    return key.find("setup.roblox.com") != std::string::npos;
}

// ── Disk cache (persists setup.roblox.com API dump across injects) ──
// Only version-pinned immutable content (setup.roblox.com) is persisted.
// Everything else stays memory-only (test scripts update upstream).
static std::mutex g_HttpDiskCacheMutex;

static std::string HttpDiskCacheHex(const std::string& key)
{
    size_t h = std::hash<std::string>{}(key);
    char buf[32];
    snprintf(buf, sizeof(buf), "%llx", (unsigned long long)h);
    return std::string(buf);
}

static bool HttpDiskCachePaths(const std::string& key, std::string* outBin, std::string* outMeta, bool ensureDir)
{
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, (DWORD)sizeof(base));
    if (n == 0 || n >= sizeof(base))
        return false;
    std::string root(base);
    std::string viridianDir = root + "\\Viridian";
    std::string cacheDir = viridianDir + "\\httpcache\\";
    if (ensureDir)
    {
        // Best-effort: create parent then leaf, ignore failures (fall back to memory-only).
        CreateDirectoryA(viridianDir.c_str(), nullptr);
        CreateDirectoryA(cacheDir.c_str(), nullptr);
    }
    std::string stem = HttpDiskCacheHex(key);
    if (outBin)
        *outBin = cacheDir + stem + ".bin";
    if (outMeta)
        *outMeta = cacheDir + stem + ".meta";
    return true;
}

static bool HttpDiskCacheLookup(const std::string& key, CachedResponse* out)
{
    if (!HttpCacheAllowsLargeBody(key))
        return false;
    std::string binPath, metaPath;
    if (!HttpDiskCachePaths(key, &binPath, &metaPath, false))
        return false;
    std::lock_guard<std::mutex> lock(g_HttpDiskCacheMutex);
    // Read sidecar first (cheap reject if missing).
    std::ifstream metaFile(metaPath, std::ios::binary);
    if (!metaFile.is_open())
        return false;
    std::string metaText((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());
    metaFile.close();
    long status = 0;
    try { status = std::stol(metaText); }
    catch (...) { return false; }
    if (status == 0)
        return false;
    std::ifstream binFile(binPath, std::ios::binary | std::ios::ate);
    if (!binFile.is_open())
        return false;
    std::streamsize size = binFile.tellg();
    if (size <= 0)
        return false;
    binFile.seekg(0, std::ios::beg);
    std::string body;
    body.resize((size_t)size);
    if (!binFile.read(body.data(), size))
        return false;
    binFile.close();
    if (out)
    {
        out->status = status;
        out->body = std::move(body);
        out->contentType.clear();
        out->headers.clear();
        out->cookies.clear();
    }
    return true;
}

static void HttpDiskCacheStore(const std::string& key, const CachedResponse& entry)
{
    if (!HttpCacheAllowsLargeBody(key))
        return;
    if (entry.status != 200 || entry.body.empty())
        return;
    std::string binPath, metaPath;
    if (!HttpDiskCachePaths(key, &binPath, &metaPath, true))
        return;
    std::lock_guard<std::mutex> lock(g_HttpDiskCacheMutex);
    // Best-effort: any I/O failure is silently ignored.
    std::ofstream binFile(binPath, std::ios::binary | std::ios::trunc);
    if (!binFile.is_open())
        return;
    binFile.write(entry.body.data(), (std::streamsize)entry.body.size());
    binFile.close();
    if (!binFile)
        return; // write failed — leave (or leave partial); next lookup validates size>0
    std::ofstream metaFile(metaPath, std::ios::binary | std::ios::trunc);
    if (!metaFile.is_open())
        return;
    std::string statusText = std::to_string(entry.status);
    metaFile.write(statusText.data(), (std::streamsize)statusText.size());
    metaFile.close();
}

static std::string HttpCacheKey(const std::string& method, const std::string& url)
{
    return method + "|" + url;
}

static bool HttpCacheLookup(const std::string& key, CachedResponse* out)
{
    std::lock_guard<std::mutex> lock(g_HttpCacheMutex);
    auto it = g_HttpCache.find(key);
    if (it == g_HttpCache.end())
        return false;
    if (out)
        *out = it->second;
    return true;
}

static void HttpCacheStore(const std::string& key, CachedResponse entry)
{
    const size_t bodySize = entry.body.size();
    const bool isLarge = bodySize > kHttpCacheMaxBody;
    if (isLarge)
    {
        if (bodySize > kHttpCacheLargeMaxBody)
            return;
        if (!HttpCacheAllowsLargeBody(key))
            return;
        std::lock_guard<std::mutex> lock(g_HttpCacheMutex);
        auto it = g_HttpCache.find(key);
        if (it != g_HttpCache.end())
        {
            it->second = std::move(entry);
            return;
        }
        // Enforce dedicated small quota: max 2 entries over 2MB.
        // FIFO-evict oldest large entry to make room.
        size_t largeCount = 0;
        for (auto& kv : g_HttpCache)
        {
            if (kv.second.body.size() > kHttpCacheMaxBody)
                ++largeCount;
        }
        while (largeCount >= kHttpCacheMaxLargeEntries && !g_HttpCacheOrder.empty())
        {
            bool evicted = false;
            for (auto orderIt = g_HttpCacheOrder.begin(); orderIt != g_HttpCacheOrder.end(); ++orderIt)
            {
                auto entryIt = g_HttpCache.find(*orderIt);
                if (entryIt != g_HttpCache.end() && entryIt->second.body.size() > kHttpCacheMaxBody)
                {
                    g_HttpCache.erase(entryIt);
                    g_HttpCacheOrder.erase(orderIt);
                    --largeCount;
                    evicted = true;
                    break;
                }
            }
            if (!evicted)
                break;
        }
        if (g_HttpCache.size() >= kHttpCacheMaxEntries && !g_HttpCacheOrder.empty())
        {
            std::string oldest = std::move(g_HttpCacheOrder.front());
            g_HttpCacheOrder.pop_front();
            g_HttpCache.erase(oldest);
        }
        g_HttpCacheOrder.push_back(key);
        g_HttpCache.emplace(key, std::move(entry));
        return;
    }
    std::lock_guard<std::mutex> lock(g_HttpCacheMutex);
    auto it = g_HttpCache.find(key);
    if (it != g_HttpCache.end())
    {
        it->second = std::move(entry);
        return;
    }
    if (g_HttpCache.size() >= kHttpCacheMaxEntries && !g_HttpCacheOrder.empty())
    {
        std::string oldest = std::move(g_HttpCacheOrder.front());
        g_HttpCacheOrder.pop_front();
        g_HttpCache.erase(oldest);
    }
    g_HttpCacheOrder.push_back(key);
    g_HttpCache.emplace(key, std::move(entry));
}

static std::string HttpStatusPhrase(long code)
{
    switch (code)
    {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 103: return "Early Hints";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    default: return std::string();
    }
}

enum RequestMethods
{
    H_GET,
    H_HEAD,
    H_POST,
    H_PUT,
    H_DELETE,
    H_OPTIONS
};

static std::map<std::string, RequestMethods> RequestMethodMap = {
   { "get", H_GET },
   { "head", H_HEAD },
   { "post", H_POST },
   { "put", H_PUT },
   { "delete", H_DELETE },
   { "options", H_OPTIONS }
};

// ── Detect key-system / auth domains that need Seliware branding ──
static bool isAuthDomain(const std::string& url)
{
    return url.find("api.luarmor.net") != std::string::npos ||
        url.find("api.eclipsehub.xyz") != std::string::npos;
}

// ── In-flight bound: every network HttpGet/request/HttpPost occupies one
// slot for the duration of its background I/O. Past the cap the call sheds
// load with a clear error (still delivered on the next scheduler tick, so
// flood loops stay scheduler-paced) instead of spawning unbounded detached
// threads and piling unbounded completions into the yield queue.
static std::atomic<int> g_HttpInFlight{ 0 };
static constexpr int kHttpMaxInFlight = 16;

static bool HttpTryAcquireInFlight()
{
    int cur = g_HttpInFlight.load(std::memory_order_relaxed);
    while (cur < kHttpMaxInFlight)
    {
        if (g_HttpInFlight.compare_exchange_weak(cur, cur + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed))
            return true;
    }
    return false;
}

static void HttpReleaseInFlight()
{
    g_HttpInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

// RAII slot holder: lives in the background closure, so the slot is released
// on every path out (success, error, exception, teleport-abort).
struct HttpInFlightRelease
{
    ~HttpInFlightRelease() { HttpReleaseInFlight(); }
};

// ── Per-host keepalive session pool (reuses TCP/TLS per host) ──
// A single shared Session broke requests because curl handle state persists:
// explicit Cookie header (CURLOPT_COOKIE), POSTFIELDS body, and headers
// survive across requests. A POST to host A would leak its body/cookies
// into a later GET to host B. Per-host pooling confines any residual state
// to the same origin (where reuse is safe), and every call site below
// overwrites Url/Header/Timeout/Cookies (+Body) on each use so no stale
// state survives.
//
// Concurrency discipline: each host entry carries its OWN mutex. The global
// map mutex is held ONLY for lookup/creation/eviction — never across network
// I/O — so a slow host (or stale in-flight requests during a teleport) can
// never wedge other hosts or the teleport path. Callers hold the ENTRY mutex
// across configure+execute (cpr::Session is not thread-safe); eviction only
// erases the map entry while in-use callers keep it alive via shared_ptr.
struct PooledHostSession
{
    std::mutex Mutex;
    std::shared_ptr<cpr::Session> Session = std::make_shared<cpr::Session>();
};
static std::mutex g_SessionPoolMutex;
static std::map<std::string, std::shared_ptr<PooledHostSession>> g_SessionPool;
static constexpr size_t kSessionPoolMaxHosts = 8;

static std::string HttpSessionHostKey(const std::string& url)
{
    std::string scheme, rest;
    size_t schemePos = url.find("://");
    if (schemePos != std::string::npos)
    {
        scheme = url.substr(0, schemePos);
        rest = url.substr(schemePos + 3);
    }
    else
    {
        scheme = "https";
        rest = url;
    }
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    size_t pathPos = rest.find_first_of("/?#");
    std::string hostport = (pathPos == std::string::npos) ? rest : rest.substr(0, pathPos);
    std::string host, port;
    if (!hostport.empty() && hostport[0] == '[')
    {
        // IPv6 literal [::1] with optional :port
        size_t close = hostport.find(']');
        if (close != std::string::npos)
        {
            host = hostport.substr(0, close + 1);
            if (close + 1 < hostport.size() && hostport[close + 1] == ':')
                port = hostport.substr(close + 2);
        }
        else
        {
            host = hostport;
        }
    }
    else
    {
        size_t colon = hostport.find(':');
        if (colon != std::string::npos)
        {
            host = hostport.substr(0, colon);
            port = hostport.substr(colon + 1);
        }
        else
        {
            host = hostport;
        }
    }
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);
    if (port.empty())
        port = (scheme == "http") ? "80" : "443";
    return scheme + "://" + host + ":" + port;
}

// REQUIRES: nothing. Returns the pooled entry for the URL's origin,
// creating it on first use. Evicts an arbitrary entry (map begin) when over
// capacity; entries in use stay alive through the caller's shared_ptr.
static std::shared_ptr<PooledHostSession> SessionHostFor(const std::string& url)
{
    std::string key = HttpSessionHostKey(url);
    std::lock_guard<std::mutex> lock(g_SessionPoolMutex);
    auto it = g_SessionPool.find(key);
    if (it != g_SessionPool.end())
        return it->second;
    if (g_SessionPool.size() >= kSessionPoolMaxHosts && !g_SessionPool.empty())
        g_SessionPool.erase(g_SessionPool.begin());
    auto entry = std::make_shared<PooledHostSession>();
    g_SessionPool.emplace(std::move(key), entry);
    return entry;
}

// ── Get HWID for fingerprint header ──
static std::string GetHWID()
{
    HW_PROFILE_INFO hwProfileInfo;
    if (GetCurrentHwProfile(&hwProfileInfo))
    {
        std::wstring wGuid(hwProfileInfo.szHwProfileGuid);
        return std::string(wGuid.begin(), wGuid.end());
    }
    return "Unknown";
}

int Http::HttpGet(lua_State* L)
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

    if (Url.find("http://") != 0 && Url.find("https://") != 0)
    {
        luaL_argerror(L, 2, "Invalid protocol (expected 'http://' or 'https://')");
        return 0;
    }

    std::string GameId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "GameId");
        if (lua_isstring(L, -1))
            GameId = lua_tostring(L, -1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    std::string PlaceId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "PlaceId");
        if (lua_isstring(L, -1))
            PlaceId = lua_tostring(L, -1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    using Json = nlohmann::json;
    cpr::Header Headers;
    Json SessionIdJson;

    SessionIdJson["GameId"] = GameId;
    SessionIdJson["PlaceId"] = PlaceId;

    // Neutral identifiers only — harm reduction against naive signature
    // correlation, not invisibility.
    Headers.insert({ "User-Agent", "Roblox/WinInetRobloxApp" });

    Headers.insert({ "Roblox-Session-Id", SessionIdJson.dump() });
    Headers.insert({ "Roblox-Place-Id", PlaceId });
    Headers.insert({ "Roblox-Game-Id", GameId });
    Headers.insert({ "Accept", "*/*" });

    // ── Cache fast path: byte-identical body, resumed on the next scheduler
    // tick (no network, no session-pool lock). Every HttpGet yields — even on
    // a cache hit — so `while true do game:HttpGet(url) end` can never become
    // a non-yielding spin that pins scheduler workers and blocks teleport.
    std::string CacheKey = HttpCacheKey("GET", Url);
    {
        CachedResponse Hit;
        bool HaveHit = false;
        bool FromDisk = false;
        if (HttpCacheLookup(CacheKey, &Hit) && Hit.status == 200)
        {
            HaveHit = true;
        }
        // Disk fallback (setup.roblox.com only): warm memory cache, no network.
        else if (HttpDiskCacheLookup(CacheKey, &Hit) && Hit.status == 200)
        {
            HaveHit = true;
            FromDisk = true;
        }
        if (HaveHit)
        {
            if (FromDisk)
                HttpCacheStore(CacheKey, Hit);
            const uint64_t GenAtYield = Shared::StateGeneration.load(std::memory_order_acquire);
            return Yielding::YieldExecution(L, [Body = std::move(Hit.body), GenAtYield]() mutable -> Yielded
                {
                    if (GenAtYield != Shared::StateGeneration.load(std::memory_order_acquire))
                    {
                        return [](lua_State* L) -> int
                            {
                                lua_pushstring(L, "HttpGet failed: teleport aborted request");
                                return 1;
                            };
                    }
                    return [Body = std::move(Body)](lua_State* L) -> int
                        {
                            lua_pushlstring(L, Body.data(), Body.size());
                            return 1;
                        };
                });
        }
    }

    if (!HttpTryAcquireInFlight())
    {
        // Shed load with a clear error — still delivered on the next tick so
        // flood loops stay scheduler-paced — instead of wedging.
        return Yielding::YieldExecution(L, []() -> Yielded
            {
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "HttpGet failed: too many concurrent requests, shedding load");
                        return 1;
                    };
            });
    }

    const uint64_t GenAtYield = Shared::StateGeneration.load(std::memory_order_acquire);
    return Yielding::YieldExecution(L, [Url = std::move(Url), Headers = std::move(Headers), CacheKey = std::move(CacheKey), GenAtYield]() -> Yielded
        {
            HttpInFlightRelease InFlightGuard;
            if (GenAtYield != Shared::StateGeneration.load(std::memory_order_acquire))
            {
                // DataModel switched while queued: skip the network I/O; the
                // resume is dropped by the generation check on wake anyway.
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "HttpGet failed: teleport aborted request");
                        return 1;
                    };
            }
            cpr::Response Result;
            try
            {
                // Entry mutex only: the global map lock is NOT held across I/O.
                std::shared_ptr<PooledHostSession> Host = SessionHostFor(Url);
                std::lock_guard<std::mutex> HostLock(Host->Mutex);
                cpr::Session& sess = *Host->Session;
                sess.SetUrl(cpr::Url{ Url });
                sess.SetHeader(cpr::Header(Headers));
                sess.SetTimeout(cpr::Timeout{ 8000 });
                // Explicit redirect policy: release URLs are a 302 chain
                // (github.com -> release-asset host). Pooled handles inherit
                // the cpr default today; pin it per-request so a redirect is
                // always followed (bounded), never surfaced as a bare 302.
                sess.SetRedirect(true);
                sess.SetMaxRedirects(cpr::MaxRedirects{ 5 });
                sess.SetCookies(cpr::Cookies{});
                sess.SetBody(cpr::Body{});
                Result = sess.Get();
            }
            catch (const std::exception& ex)
            {
                std::stringstream Error;
                Error << "HttpGet failed: " << ex.what();
                std::string ErrorString = Error.str();
                return [ErrorString = std::move(ErrorString)](lua_State* L) -> int
                    {
                        // Transport failure is a graceful nil + message (UNC),
                        // never a truthy string and never a throw.
                        lua_pushnil(L);
                        lua_pushstring(L, ErrorString.c_str());
                        return 2;
                    };
            }
            catch (...)
            {
                return [](lua_State* L) -> int
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, "HttpGet failed: unknown exception");
                        return 2;
                    };
            }

            // ── Cache store: only successful (200) responses ──
            // Best-effort: a cache-path fault must never escape the background
            // thread (that would terminate the process with no log). The body
            // is still resumed to Lua below either way.
            try
            {
                if (Result.error.code == cpr::ErrorCode::OK && Result.status_code == 200)
                {
                    CachedResponse Entry;
                    Entry.status = Result.status_code;
                    Entry.body = Result.text;
                    for (auto& Kv : Result.header)
                    {
                        Entry.headers[Kv.first] = Kv.second;
                        if (_stricmp(Kv.first.c_str(), "Content-Type") == 0)
                            Entry.contentType = Kv.second;
                    }
                    for (auto& Kv : Result.cookies.map_)
                        Entry.cookies[Kv.first] = Kv.second;
                    // Disk persist runs in this background completion (never blocks executor thread).
                    HttpDiskCacheStore(CacheKey, Entry);
                    HttpCacheStore(CacheKey, std::move(Entry));
                }
            }
            catch (...) {}

            return [Result = std::move(Result)](lua_State* L) -> int
                {
                    if (Result.error.code != cpr::ErrorCode::OK)
                    {
                        std::stringstream Error;
                        Error << "HttpGet failed: " << Result.error.message << ", Code: " << Result.status_code;
                        std::string ErrorString = Error.str();
                        lua_pushnil(L);
                        lua_pushstring(L, ErrorString.c_str());
                        return 2;
                    }

                    if (Result.status_code != 200)
                    {
                        std::stringstream Error;
                        Error << "HttpGet returned status: " << Result.status_code << ", Error: " << Result.error.message;
                        std::string ErrorString = Error.str();
                        lua_pushnil(L);
                        lua_pushstring(L, ErrorString.c_str());
                        return 2;
                    }

                    lua_pushlstring(L, Result.text.data(), Result.text.size());
                    return 1;
                };
        });
}

int Http::request(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "Url");
    if (!lua_isstring(L, -1))
        luaL_error(L, "Missing or invalid 'Url'");

    std::string Url = lua_tostring(L, -1);
    lua_pop(L, 1);

    if (Url.find("http://") != 0 && Url.find("https://") != 0)
    {
        luaL_error(L, "Invalid protocol (expected 'http://' or 'https://')");
        return 0;
    }

    auto Method = H_GET;
    lua_getfield(L, 1, "Method");
    if (lua_isstring(L, -1))
    {
        std::string MethodStr = lua_tostring(L, -1);
        std::transform(MethodStr.begin(), MethodStr.end(), MethodStr.begin(), ::tolower);
        if (!RequestMethodMap.count(MethodStr))
            luaL_error(L, "Invalid request method: '%s'", MethodStr.c_str());

        Method = RequestMethodMap[MethodStr];
    }
    lua_pop(L, 1);

    cpr::Header Headers;
    lua_getfield(L, 1, "Headers");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
            if (!lua_isstring(L, -2) || !lua_isstring(L, -1))
                luaL_error(L, "'Headers' must have string keys/values");

            std::string Key = lua_tostring(L, -2);
            if (_stricmp(Key.c_str(), "Content-Length") == 0)
                luaL_error(L, "'Content-Length' cannot be overwritten");

            Headers[Key] = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    cpr::Cookies Cookies;
    lua_getfield(L, 1, "Cookies");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
            if (!lua_isstring(L, -2) || !lua_isstring(L, -1))
                luaL_error(L, "'Cookies' must have string keys/values");

            Cookies[lua_tostring(L, -2)] = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    std::string Body;
    lua_getfield(L, 1, "Body");
    if (lua_isstring(L, -1))
    {
        if (Method == H_GET || Method == H_HEAD)
            luaL_error(L, "'Body' is not allowed for GET/HEAD requests");

        Body = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    std::string GameId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "GameId");
        if (lua_isstring(L, -1))
            GameId = lua_tostring(L, -1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    std::string PlaceId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "PlaceId");
        if (lua_isstring(L, -1))
            PlaceId = lua_tostring(L, -1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    nlohmann::json SessionIdJson;
    SessionIdJson["GameId"] = GameId;
    SessionIdJson["PlaceId"] = PlaceId;

    // Neutral identifiers only — harm reduction against naive signature
    // correlation, not invisibility. A user-supplied User-Agent wins;
    // auth domains get no forced branding.
    if (Headers.find("User-Agent") == Headers.end())
        Headers.insert({ "User-Agent", "Roblox/WinInetRobloxApp" });

    Headers.insert({ "Roblox-Session-Id", SessionIdJson.dump() });

    // ── Cache fast path: GET/HEAD only, keyed METHOD|Url. Served on the next
    // scheduler tick (no network, no session-pool lock) so cached request
    // floods stay scheduler-paced and can never spin the scheduler workers.
    const bool Cacheable = (Method == H_GET || Method == H_HEAD);
    std::string ReqCacheKey = Cacheable
        ? HttpCacheKey((Method == H_HEAD) ? "HEAD" : "GET", Url)
        : std::string();
    if (Cacheable)
    {
        CachedResponse Hit;
        bool HaveHit = false;
        bool FromDisk = false;
        if (HttpCacheLookup(ReqCacheKey, &Hit) && Hit.status == 200)
        {
            HaveHit = true;
        }
        else if (HttpDiskCacheLookup(ReqCacheKey, &Hit) && Hit.status == 200)
        {
            // Disk fallback (setup.roblox.com only): warm memory cache, no network.
            HaveHit = true;
            FromDisk = true;
        }
        if (HaveHit)
        {
            if (FromDisk)
                HttpCacheStore(ReqCacheKey, Hit);
            const uint64_t GenAtYield = Shared::StateGeneration.load(std::memory_order_acquire);
            return Yielding::YieldExecution(L, [Resp = std::move(Hit), GenAtYield]() mutable -> Yielded
                {
                    if (GenAtYield != Shared::StateGeneration.load(std::memory_order_acquire))
                    {
                        return [](lua_State* L) -> int
                            {
                                lua_pushstring(L, "Request failed: teleport aborted request");
                                return 1;
                            };
                    }
                    return [Resp = std::move(Resp)](lua_State* L) -> int
                        {
                            lua_newtable(L);
                            lua_pushboolean(L, Resp.status >= 200 && Resp.status < 300);
                            lua_setfield(L, -2, "Success");

                            lua_pushinteger(L, Resp.status);
                            lua_setfield(L, -2, "StatusCode");

                            std::string HitPhrase = HttpStatusPhrase(Resp.status);
                            lua_pushstring(L, HitPhrase.c_str());
                            lua_setfield(L, -2, "StatusMessage");

                            lua_newtable(L);
                            for (auto& Kv : Resp.headers)
                            {
                                lua_pushstring(L, Kv.first.c_str());
                                lua_pushstring(L, Kv.second.c_str());
                                lua_settable(L, -3);
                            }
                            lua_setfield(L, -2, "Headers");

                            lua_newtable(L);
                            for (auto& Kv : Resp.cookies)
                            {
                                lua_pushstring(L, Kv.first.c_str());
                                lua_pushstring(L, Kv.second.c_str());
                                lua_settable(L, -3);
                            }
                            lua_setfield(L, -2, "Cookies");

                            lua_pushlstring(L, Resp.body.data(), Resp.body.size());
                            lua_setfield(L, -2, "Body");

                            return 1;
                        };
                });
        }
    }

    if (!HttpTryAcquireInFlight())
    {
        // Shed load with a clear error — still delivered on the next tick so
        // flood loops stay scheduler-paced — instead of wedging.
        return Yielding::YieldExecution(L, []() -> Yielded
            {
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "Request failed: too many concurrent requests, shedding load");
                        return 1;
                    };
            });
    }

    const uint64_t ReqGenAtYield = Shared::StateGeneration.load(std::memory_order_acquire);
    return Yielding::YieldExecution(L, [Url = std::move(Url), Headers = std::move(Headers), Cookies = std::move(Cookies), Body = std::move(Body), Method, ReqCacheKey = std::move(ReqCacheKey), Cacheable, ReqGenAtYield]() -> Yielded
        {
            HttpInFlightRelease InFlightGuard;
            if (ReqGenAtYield != Shared::StateGeneration.load(std::memory_order_acquire))
            {
                // DataModel switched while queued: skip the network I/O; the
                // resume is dropped by the generation check on wake anyway.
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "Request failed: teleport aborted request");
                        return 1;
                    };
            }
            cpr::Response Response;
            try
            {
                // Entry mutex only: the global map lock is NOT held across I/O.
                std::shared_ptr<PooledHostSession> Host = SessionHostFor(Url);
                std::lock_guard<std::mutex> HostLock(Host->Mutex);
                cpr::Session& sess = *Host->Session;
                sess.SetUrl(cpr::Url{ Url });
                sess.SetHeader(Headers);
                sess.SetTimeout(cpr::Timeout{ 8000 });
                // Pin redirect policy per-request (see HttpGet): never rely on
                // the pooled handle's inherited default.
                sess.SetRedirect(true);
                sess.SetMaxRedirects(cpr::MaxRedirects{ 5 });
                sess.SetCookies(Cookies);
                switch (Method)
                {
                case H_GET: sess.SetBody(cpr::Body{}); Response = sess.Get(); break;
                case H_HEAD: sess.SetBody(cpr::Body{}); Response = sess.Head(); break;
                case H_POST: sess.SetBody(cpr::Body{ Body }); Response = sess.Post(); break;
                case H_PUT: sess.SetBody(cpr::Body{ Body }); Response = sess.Put(); break;
                case H_DELETE: sess.SetBody(cpr::Body{ Body }); Response = sess.Delete(); break;
                case H_OPTIONS: sess.SetBody(cpr::Body{ Body }); Response = sess.Options(); break;
                default: throw std::runtime_error("Invalid request method"); break;
                }
            }
            catch (const std::exception& ex)
            {
                std::string Err = std::string("Request failed: ") + ex.what();
                return [Err = std::move(Err)](lua_State* L) -> int
                    {
                        lua_pushstring(L, Err.c_str());
                        return 1;
                    };
            }
            catch (...)
            {
                // /EHa: SEH faults arrive here too. Never let them escape the
                // background thread (silent process death); surface nil-side
                // Lua error like every other failure.
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "Request failed: unknown exception");
                        return 1;
                    };
            }

            // ── Cache store: GET/HEAD only, successful (200) responses only ──
            // Best-effort (see HttpGet): never let a cache fault escape the
            // background thread. The response is still resumed below.
            try
            {
                if (Cacheable && !ReqCacheKey.empty() &&
                    Response.error.code == cpr::ErrorCode::OK && Response.status_code == 200)
                {
                    CachedResponse Entry;
                    Entry.status = Response.status_code;
                    Entry.body = Response.text;
                    for (auto& Kv : Response.header)
                    {
                        Entry.headers[Kv.first] = Kv.second;
                        if (_stricmp(Kv.first.c_str(), "Content-Type") == 0)
                            Entry.contentType = Kv.second;
                    }
                    for (auto& Kv : Response.cookies.map_)
                        Entry.cookies[Kv.first] = Kv.second;
                    // Disk persist runs in this background completion (never blocks executor thread).
                    HttpDiskCacheStore(ReqCacheKey, Entry);
                    HttpCacheStore(ReqCacheKey, std::move(Entry));
                }
            }
            catch (...) {}

            return [Response = std::move(Response)](lua_State* L) -> int
                {
                    if (Response.error.code != cpr::ErrorCode::OK)
                    {
                        std::string Err = "Request failed: " + Response.error.message;
                        lua_pushstring(L, Err.c_str());
                        return 1;
                    }

                    lua_newtable(L);
                    lua_pushboolean(L, Response.status_code >= 200 && Response.status_code < 300);
                    lua_setfield(L, -2, "Success");

                    lua_pushinteger(L, Response.status_code);
                    lua_setfield(L, -2, "StatusCode");

                    std::string Phrase;
                    switch (Response.status_code)
                    {
                    case 100: Phrase = std::string("Continue"); break;
                    case 101: Phrase = std::string("Switching Protocols"); break;
                    case 102: Phrase = std::string("Processing"); break;
                    case 103: Phrase = std::string("Early Hints"); break;

                    case 200: Phrase = std::string("OK"); break;
                    case 201: Phrase = std::string("Created"); break;
                    case 202: Phrase = std::string("Accepted"); break;
                    case 203: Phrase = std::string("Non-Authoritative Information"); break;
                    case 204: Phrase = std::string("No Content"); break;
                    case 205: Phrase = std::string("Reset Content"); break;
                    case 206: Phrase = std::string("Partial Content"); break;
                    case 207: Phrase = std::string("Multi-Status"); break;
                    case 208: Phrase = std::string("Already Reported"); break;
                    case 226: Phrase = std::string("IM Used"); break;

                    case 300: Phrase = std::string("Multiple Choices"); break;
                    case 301: Phrase = std::string("Moved Permanently"); break;
                    case 302: Phrase = std::string("Found"); break;
                    case 303: Phrase = std::string("See Other"); break;
                    case 304: Phrase = std::string("Not Modified"); break;
                    case 305: Phrase = std::string("Use Proxy"); break;
                    case 307: Phrase = std::string("Temporary Redirect"); break;
                    case 308: Phrase = std::string("Permanent Redirect"); break;

                    case 400: Phrase = std::string("Bad Request"); break;
                    case 401: Phrase = std::string("Unauthorized"); break;
                    case 402: Phrase = std::string("Payment Required"); break;
                    case 403: Phrase = std::string("Forbidden"); break;
                    case 404: Phrase = std::string("Not Found"); break;
                    case 405: Phrase = std::string("Method Not Allowed"); break;
                    case 406: Phrase = std::string("Not Acceptable"); break;
                    case 407: Phrase = std::string("Proxy Authentication Required"); break;
                    case 408: Phrase = std::string("Request Timeout"); break;
                    case 409: Phrase = std::string("Conflict"); break;
                    case 410: Phrase = std::string("Gone"); break;
                    case 411: Phrase = std::string("Length Required"); break;
                    case 412: Phrase = std::string("Precondition Failed"); break;
                    case 413: Phrase = std::string("Payload Too Large"); break;
                    case 414: Phrase = std::string("URI Too Long"); break;
                    case 415: Phrase = std::string("Unsupported Media Type"); break;
                    case 416: Phrase = std::string("Range Not Satisfiable"); break;
                    case 417: Phrase = std::string("Expectation Failed"); break;
                    case 418: Phrase = std::string("I'm a teapot"); break;
                    case 422: Phrase = std::string("Unprocessable Entity"); break;
                    case 423: Phrase = std::string("Locked"); break;
                    case 424: Phrase = std::string("Failed Dependency"); break;
                    case 426: Phrase = std::string("Upgrade Required"); break;
                    case 428: Phrase = std::string("Precondition Required"); break;
                    case 429: Phrase = std::string("Too Many Requests"); break;
                    case 431: Phrase = std::string("Request Header Fields Too Large"); break;
                    case 451: Phrase = std::string("Unavailable For Legal Reasons"); break;

                    case 500: Phrase = std::string("Internal Server Error"); break;
                    case 501: Phrase = std::string("Not Implemented"); break;
                    case 502: Phrase = std::string("Bad Gateway"); break;
                    case 503: Phrase = std::string("Service Unavailable"); break;
                    case 504: Phrase = std::string("Gateway Time-out"); break;
                    case 505: Phrase = std::string("HTTP Version Not Supported"); break;
                    case 506: Phrase = std::string("Variant Also Negotiates"); break;
                    case 507: Phrase = std::string("Insufficient Storage"); break;
                    case 508: Phrase = std::string("Loop Detected"); break;
                    case 510: Phrase = std::string("Not Extended"); break;
                    case 511: Phrase = std::string("Network Authentication Required"); break;

                    default: Phrase = std::string(); break;
                    }
                    lua_pushstring(L, Phrase.c_str());
                    lua_setfield(L, -2, "StatusMessage");

                    lua_newtable(L);
                    for (auto& Header : Response.header)
                    {
                        lua_pushstring(L, Header.first.c_str());
                        lua_pushstring(L, Header.second.c_str());
                        lua_settable(L, -3);
                    }
                    lua_setfield(L, -2, "Headers");

                    lua_newtable(L);
                    for (auto& C : Response.cookies.map_)
                    {
                        lua_pushstring(L, C.first.c_str());
                        lua_pushstring(L, C.second.c_str());
                        lua_settable(L, -3);
                    }
                    lua_setfield(L, -2, "Cookies");

                    lua_pushlstring(L, Response.text.data(), Response.text.size());
                    lua_setfield(L, -2, "Body");

                    return 1;
                };
        });
}

int Http::HttpPost(lua_State* L)
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

    if (Url.find("file://") == 0)
    {
        luaL_error(L, "HttpPost does not allow file:// URLs");
        return 0;
    }

    if (Url.find("http://") != 0 && Url.find("https://") != 0)
    {
        luaL_argerror(L, 2, "Invalid protocol (expected 'http://' or 'https://')");
        return 0;
    }

    std::string Body;
    if (lua_isstring(L, 2) && !lua_isnil(L, 2))
        Body = lua_tostring(L, 2);
    else if (lua_isstring(L, 3) && !lua_isnil(L, 3))
        Body = lua_tostring(L, 3);

    std::string GameId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "GameId");
        if (lua_isstring(L, -1))
            GameId = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    std::string PlaceId;
    lua_getglobal(L, "game");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "PlaceId");
        if (lua_isstring(L, -1))
            PlaceId = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    cpr::Header Headers;

    // Neutral identifiers only — harm reduction against naive signature
    // correlation, not invisibility.
    Headers.insert({ "User-Agent", "Roblox/WinInetRobloxApp" });

    Headers.insert({ "Content-Type", "application/x-www-form-urlencoded" });
    Headers.insert({ "Accept", "*/*" });

    if (!HttpTryAcquireInFlight())
    {
        // Shed load with a clear error — still delivered on the next tick so
        // flood loops stay scheduler-paced — instead of wedging.
        return Yielding::YieldExecution(L, []() -> Yielded
            {
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "HttpPost failed: too many concurrent requests, shedding load");
                        return 1;
                    };
            });
    }

    const uint64_t PostGenAtYield = Shared::StateGeneration.load(std::memory_order_acquire);
    return Yielding::YieldExecution(L, [Url = std::move(Url), Body = std::move(Body), Headers = std::move(Headers), PostGenAtYield]() -> Yielded
        {
            HttpInFlightRelease InFlightGuard;
            if (PostGenAtYield != Shared::StateGeneration.load(std::memory_order_acquire))
            {
                // DataModel switched while queued: skip the network I/O; the
                // resume is dropped by the generation check on wake anyway.
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "HttpPost failed: teleport aborted request");
                        return 1;
                    };
            }
            cpr::Response Result;
            try
            {
                // Entry mutex only: the global map lock is NOT held across I/O.
                std::shared_ptr<PooledHostSession> Host = SessionHostFor(Url);
                std::lock_guard<std::mutex> HostLock(Host->Mutex);
                cpr::Session& sess = *Host->Session;
                sess.SetUrl(cpr::Url{ Url });
                sess.SetHeader(Headers);
                sess.SetTimeout(cpr::Timeout{ 8000 });
                // Pin redirect policy per-request (see HttpGet).
                sess.SetRedirect(true);
                sess.SetMaxRedirects(cpr::MaxRedirects{ 5 });
                sess.SetCookies(cpr::Cookies{});
                sess.SetBody(cpr::Body{ Body });
                Result = sess.Post();
            }
            catch (const std::exception& ex)
            {
                std::string ErrorString = std::string("HttpPost failed: ") + ex.what();
                return [ErrorString = std::move(ErrorString)](lua_State* L) -> int
                    {
                        lua_pushstring(L, ErrorString.c_str());
                        return 1;
                    };
            }
            catch (...)
            {
                return [](lua_State* L) -> int
                    {
                        lua_pushstring(L, "HttpPost failed: unknown exception");
                        return 1;
                    };
            }

            return [Result = std::move(Result)](lua_State* L) -> int
                {
                    if (Result.error.code != cpr::ErrorCode::OK)
                    {
                        std::string ErrorString = std::string("HttpPost failed: ") + Result.error.message;
                        lua_pushstring(L, ErrorString.c_str());
                        return 1;
                    }

                    lua_pushlstring(L, Result.text.data(), Result.text.size());
                    return 1;
                };
        });
}

int Http::HttpPostAsync(lua_State* L)
{
    return HttpPost(L);
}

void Http::register_lib(lua_State* L)
{
    Environment->make_global(L, "HttpGet", Http::HttpGet, Http::funcnameinc++);
    Environment->make_global(L, "HttpPost", Http::HttpPost, Http::funcnameinc++);
    Environment->make_global(L, "HttpPostAsync", Http::HttpPostAsync, Http::funcnameinc++);
    Environment->make_global(L, "request", Http::request, Http::funcnameinc++);
    Environment->make_global(L, "http_request", Http::request, Http::funcnameinc++);

    lua_newtable(L);
    Environment->make_table_function(L, "request", Http::request, Http::funcnameinc++);
    lua_setglobal(L, "http");
}

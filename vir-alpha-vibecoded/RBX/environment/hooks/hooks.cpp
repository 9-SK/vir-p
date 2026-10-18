#include "hooks.hpp"

#include <lapi.h>
#include <lgc.h>
#include <ldebug.h>
#include <Utilitys/Globals.hpp>

#include <RBX/environment/libs/http/http.hpp>
#include <RBX/environment/libs/miscellaneous/miscellaneous.hpp>
#include <RBX/environment/libs/oth/oth.hpp>
#include <RBX/environment/libs/closures/closures.hpp>

#include <cstring>
#include <string>
#include <vector>
#include <unordered_set>

// ───────────────────────────────────────────────────────────────
//  Original function pointers
// ───────────────────────────────────────────────────────────────
lua_CFunction original_index;
lua_CFunction original_namecall;
lua_CFunction original_newindex;

// ───────────────────────────────────────────────────────────────
//  Blocked functions — namecall and __index
// ───────────────────────────────────────────────────────────────
static std::unordered_set<std::string> blocked_funcs = {
    "Load", "OpenScreenshotsFolder", "OpenVideosFolder", "AddCoreScriptLocal",
    "SaveScriptProfilingData", "GetRobuxBalance", "PerformPurchase", "PerformPurchaseV2",
    "PerformBulkPurchase", "PerformSubscriptionPurchase", "PerformSubscriptionPurchaseV2",
    "PerformCancelSubscription", "PromptPurchase", "PromptProductPurchase",
    "PromptGamePassPurchase", "PromptBundlePurchase", "PromptBulkPurchase",
    "PromptRobloxPurchase", "PromptThirdPartyPurchase", "PromptPremiumPurchase",
    "PromptSubscriptionPurchase", "PromptCancelSubscription", "PromptNativePurchase",
    "PromptNativePurchaseWithLocalPlayer", "PromptCollectiblesPurchase",
    "PrepareCollectiblesPurchase", "GetUserSubscriptionPaymentHistoryAsync",
    "GetUserSubscriptionDetailsInternalAsync", "GetUserSubscriptionStatusAsync",
    "ReportAbuse", "ReportAbuseV3", "TakeScreenshot", "ToggleRecording",
    "OpenBrowserWindow", "OpenNativeOverlay", "ExecuteJavaScript", "ReturnToJavaScript",
    "SendCommand", "EmitHybridEvent", "OpenWeChatAuthWindow", "BroadcastNotification",
    "OpenUrl", "DetectUrl", "RegisterLuaUrl", "StartLuaUrlDelivery", "StopLuaUrlDelivery",
    "SwitchToSettingsApp", "SupportsSwitchToSettingsApp", "GetLastLuaUrl",
    "GetAndClearLastPendingUrl", "IsUrlRegistered", "RequestInternal", "requestInternal",
    "RequestAsync", "GetAsync", "PostAsync", "GetAsyncFullUrl", "PostAsyncFullUrl",
    "RequestLimitedAsync", "HttpRequestAsync", "GetApiV1", "InvokeAsync",
    "RegisterOpenCloud", "CaptureScreenshot", "CreatePostAsync", "DeleteCapture",
    "DeleteCapturesAsync", "GetCaptureFilePathAsync", "GetCaptureUploadDataAsync",
    "GetCaptureSizeAsync", "GetCaptureStorageSizeAsync", "RetrieveCaptures",
    "SaveCaptureToExternalStorage", "SaveCapturesToExternalStorageAsync",
    "SaveScreenshotCapture", "PromptSaveCapturesToGallery", "PromptShareCapture",
    "PostToFeedAsync", "Call", "GetLast", "GetMessageId",
    "GetProtocolMethodRequestMessageId", "GetProtocolMethodResponseMessageId",
    "MakeRequest", "Publish", "PublishProtocolMethodRequest",
    "PublishProtocolMethodResponse", "Subscribe", "SubscribeToProtocolMethodRequest",
    "SubscribeToProtocolMethodResponse", "GetCredentialsHeaders", "GetDeviceAccessToken",
    "GetDeviceIntegrityToken", "GetDeviceIntegrityTokenYield", "NoPromptCreateOutfit",
    "NoPromptDeleteOutfit", "NoPromptRenameOutfit", "NoPromptSaveAvatar",
    "NoPromptSaveAvatarThumbnailCustomization", "NoPromptSetFavorite",
    "NoPromptUpdateOutfit", "PerformCreateOutfitWithDescription", "PerformDeleteOutfit",
    "PerformRenameOutfit", "PerformSaveAvatarWithDescription", "PerformSetFavorite",
    "PerformUpdateOutfit", "PromptCreateOutfit", "PromptDeleteOutfit",
    "PromptRenameOutfit", "PromptSaveAvatar", "PromptSetFavorite", "PromptUpdateOutfit",
    "SetAllowInventoryReadAccess", "PromptAllowInventoryReadAccess",
    "SignalCreateOutfitFailed", "SignalCreateOutfitPermissionDenied",
    "SignalDeleteOutfitFailed", "SignalDeleteOutfitPermissionDenied",
    "SignalRenameOutfitFailed", "SignalRenameOutfitPermissionDenied",
    "SignalSaveAvatarPermissionDenied", "SignalSetFavoriteFailed",
    "SignalSetFavoritePermissionDenied", "SignalUpdateOutfitFailed",
    "SignalUpdateOutfitPermissionDenied", "GetLocalFileContents", "SetBaseUrl",
    "FetchAssetWithFormat", "RegisterUGCValidationFunction", "PerformManagedUpdate",
    "SetPurchasePromptIsShown", "DumpToFileAsync", "Run", "OpenInBrowser_DONOTUSE",
    "TryInstallPlugin", "PromptImportFile", "PromptImportFiles", "UninstallPlugin",
};

// ───────────────────────────────────────────────────────────────
//  Blocked services — namecall only
// ───────────────────────────────────────────────────────────────
static std::unordered_set<std::string> blocked_services = {
    "BrowserService", "ScriptContext", "ScriptProfilerService", "MicroProfilerService",
    "TestService", "StudioService", "AccountService", "MessageBusService",
    "OpenCloudService", "HttpRbxApiService", "AvatarEditorService", "CaptureService",
    "LinkingService",
};

// ───────────────────────────────────────────────────────────────
//  sanitize_key — strip null/control bytes from key
// ───────────────────────────────────────────────────────────────
static std::string sanitize_key(const char* str, size_t len)
{
    std::string key;
    key.reserve(len);
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] != '\0' && str[i] != '\1')
            key += str[i];
    }
    return key;
}

// ───────────────────────────────────────────────────────────────
//  is_executor_state — detect executor-owned lua_State
// ───────────────────────────────────────────────────────────────
static bool is_executor_state(lua_State* L)
{
    if (!L || !L->userdata)
        return false;

    auto* data = static_cast<RobloxExtraSpace*>(L->userdata);

    if (data->capabilities == MaxCapabilities)
        return true;

    if (data->script.expired())
        return true;

    return false;
}

// ───────────────────────────────────────────────────────────────
//  IndexHook — intercepts __index on game userdata
// ───────────────────────────────────────────────────────────────
int index_hook(lua_State* L)
{
    std::string key;
    if (lua_isstring(L, 2))
    {
        size_t len = 0;
        const char* str = lua_tolstring(L, 2, &len);
        if (str)
            key = sanitize_key(str, len);
    }

    if (key.empty())
        return original_index(L);

    if (blocked_funcs.count(key) > 0)
    {
        luaL_error(L, "attempt to access blocked function '%s'", key.c_str());
        return 0;
    }

    if (is_executor_state(L))
    {
        if (key == "HttpGet" || key == "HttpGetAsync")
        {
            lua_pushcclosure(L, Http::HttpGet, nullptr, NULL);
            return 1;
        }
        else if (key == "GetObjects" || key == "GetObjectsAsync")
        {
            lua_pushcclosure(L, Miscellaneous::GetObjects, nullptr, NULL);
            return 1;
        }
    }

    return original_index(L);
}

// ───────────────────────────────────────────────────────────────
//  NamecallHook — intercepts __namecall on game userdata
// ───────────────────────────────────────────────────────────────
int namecall_hook(lua_State* L)
{
    std::string key;
    if (L->namecall)
    {
        const char* str = L->namecall->data;
        size_t len = L->namecall->len;
        if (str)
            key = sanitize_key(str, len);
    }

    if (key.empty())
        return original_namecall(L);

    if (blocked_services.count(key) > 0)
    {
        luaL_error(L, "attempt to access blocked service '%s'", key.c_str());
        return 0;
    }

    if (blocked_funcs.count(key) > 0)
    {
        luaL_error(L, "attempt to access blocked function '%s'", key.c_str());
        return 0;
    }

    if (is_executor_state(L))
    {
        if (key == "HttpGet" || key == "HttpGetAsync")
            return Http::HttpGet(L);
        else if (key == "GetObjects" || key == "GetObjectsAsync")
            return Miscellaneous::GetObjects(L);
    }

    return original_namecall(L);
}

// ───────────────────────────────────────────────────────────────
//  NewindexHook — intercepts __newindex on game userdata
// ───────────────────────────────────────────────────────────────
int newindex_hook(lua_State* L)
{
    if (lua_type(L, 1) == LUA_TUSERDATA && lua_type(L, 2) == LUA_TSTRING)
    {
        const char* prop = lua_tostring(L, 2);
        if (strcmp(prop, ("OnInvoke")) == 0 || strcmp(prop, ("OnClientInvoke")) == 0 || strcmp(prop, ("OnServerInvoke")) == 0)
        {
            int top = lua_gettop(L);
            lua_pushstring(L, ("__callbacks"));
            lua_rawget(L, LUA_REGISTRYINDEX);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                lua_newtable(L);

                lua_newtable(L);
                lua_pushstring(L, ("k"));
                lua_setfield(L, -2, ("__mode"));
                lua_setmetatable(L, -2);

                lua_pushstring(L, ("__callbacks"));
                lua_pushvalue(L, -2);
                lua_rawset(L, LUA_REGISTRYINDEX);
            }

            lua_pushvalue(L, 1);
            lua_rawget(L, -2);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                lua_newtable(L);

                lua_pushvalue(L, 1);
                lua_pushvalue(L, -2);
                lua_rawset(L, -4);
            }

            lua_pushstring(L, prop);
            lua_pushvalue(L, 3);
            lua_rawset(L, -3);

            lua_settop(L, top);
        }
    }

    return original_newindex(L);
}

// ───────────────────────────────────────────────────────────────
//  InitializeHooks — patches game metatable fields
// ───────────────────────────────────────────────────────────────
void InitializeHooks(lua_State* L)
{
    int original_top = lua_gettop(L);

    lua_getglobal(L, "game");

    luaL_getmetafield(L, -1, "__index");
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* index_closure = clvalue(luaA_toobject(L, -1));
        if (original_index == nullptr)
            original_index = index_closure->c.f;
        index_closure->c.f = index_hook;
    }
    lua_pop(L, 1);

    luaL_getmetafield(L, -1, "__newindex");
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* newindex_closure = clvalue(luaA_toobject(L, -1));
        if (original_newindex == nullptr)
            original_newindex = newindex_closure->c.f;
        newindex_closure->c.f = newindex_hook;
    }
    lua_pop(L, 1);

    luaL_getmetafield(L, -1, "__namecall");
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* namecall_closure = clvalue(luaA_toobject(L, -1));
        if (original_namecall == nullptr)
            original_namecall = namecall_closure->c.f;
        namecall_closure->c.f = namecall_hook;
    }
    lua_pop(L, 1);

    lua_settop(L, original_top);
}

// ═══════════════════════════════════════════════════════════════
//  ANTI-DETECTION — debug.info / getfenv hooks
// ═══════════════════════════════════════════════════════════════

static lua_CFunction anti_debug_info_original = nullptr;
static lua_CFunction anti_getfenv_original   = nullptr;

// ─── getthread helper (same as ldblib.cpp) ────────────────────
static lua_State* anti_getthread(lua_State* L, int* arg)
{
    if (lua_isthread(L, 1))
    {
        *arg = 1;
        return lua_tothread(L, 1);
    }
    *arg = 0;
    return L;
}

// ─── Determine original c.f for a closure hooked by oth or hookfunction ──
// NOTE: only valid for C closures. Lua hookfunction targets swap Proto,
// not c.f — reading c.f from a Lua closure is UB and corrupts l.p.
static lua_CFunction resolve_original_cf(lua_State* L, Closure* c)
{
    if (!c || !c->isC) return nullptr;

    // oth.hook stores original c.f inside the clone closure
    lua_CFunction oth_orig = Oth::GetOriginalCF(c);
    if (oth_orig) return oth_orig;

    // hookfunction stores a ref to the original clone.
    // Copy-out: map op under the leaf lock, lua_getref below runs unlocked.
    int origRefCopy = LUA_NOREF;
    {
        std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
        auto it = Closures::HookRecords.find(c);
        if (it != Closures::HookRecords.end())
            origRefCopy = it->second.OriginalRef;
    }
    if (origRefCopy != LUA_NOREF)
    {
        lua_getref(L, origRefCopy);
        if (lua_isfunction(L, -1))
        {
            Closure* orig = clvalue(index2addr(L, -1));
            lua_CFunction result = nullptr;
            if (orig && orig->isC)
                result = orig->c.f;
            lua_pop(L, 1);
            return result;
        }
        else
            lua_pop(L, 1);
    }

    return nullptr;
}

// ─── Is this closure owned by the executor? ───────────────────
static bool is_executor_owned(Closure* c)
{
    if (!c) return false;
    if (Shared::ExecutorClosures.count(c))    return true;
    if (Closures::WrappedClosures.count(c))   return true;
    if (Closures::NewCClosureRefs.count(c))   return true;
    bool isHookRecorded = false;
    {
        std::lock_guard<std::mutex> lock(Closures::HookRecordsMutex);
        isHookRecorded = Closures::HookRecords.count(c) > 0;
    }
    if (isHookRecorded)       return true;
    if (Environment && Environment->function_array.size())
    {
        for (Closure* reg : Environment->function_array)
            if (reg == c) return true;
    }
    return false;
}

// ─── Hooked debug.info ────────────────────────────────────────
// If the target closure is one we've hooked, temporarily swap c.f
// back to the original, call real debug.info, then restore.
// This makes debug.info return the original function's info.
// CLOSURES ONLY: Lua hookfunction targets swap Proto (l.p), never c.f.
// Touching c.f on a Lua closure corrupts it and breaks the Lua
// hook dispatcher (which itself calls debug.info(1, "f") to fetch
// the hooked closure). So Lua closures always pass through.
static int anti_debug_info(lua_State* L)
{
    int arg = 0;
    lua_State* L1 = anti_getthread(L, &arg);

    // ── Function-based call: debug.info(func, "flags") ──
    if (!lua_isnumber(L, arg + 1) && lua_isfunction(L, arg + 1))
    {
        Closure* cl = clvalue(index2addr(L, arg + 1));
        if (!cl || !cl->isC)
            return anti_debug_info_original(L);
        lua_CFunction orig = resolve_original_cf(L, cl);
        if (orig && orig != cl->c.f)
        {
            lua_CFunction saved = cl->c.f;
            cl->c.f = orig;
            int result = anti_debug_info_original(L);
            cl->c.f = saved;
            return result;
        }
    }
    // ── Level-based call: debug.info(level, "flags") ──
    else if (lua_isnumber(L, arg + 1))
    {
        int level = (int)lua_tointeger(L, arg + 1);
        // Hostile levels (NaN/inf/huge → INT_MIN via C cast) would wild-read
        // in lua_getinfo — pass through to the stock-guarded original instead.
        if (level < 0)
            return anti_debug_info_original(L);
        lua_Debug ar;

        // Peek at the function at this level
        if (lua_getinfo(L1, level, "f", &ar))
        {
            // 'f' pushes the function onto L1's stack
            if (lua_gettop(L1) > 0 && lua_isfunction(L1, -1))
            {
                Closure* cl = clvalue(index2addr(L1, -1));
                // Lua closures (incl. the __lunarium hook dispatcher proto
                // with source "=hookfunction") must NOT be touched.
                if (!cl || !cl->isC)
                {
                    lua_pop(L1, 1);
                    return anti_debug_info_original(L);
                }
                lua_CFunction orig = resolve_original_cf(L, cl);
                if (orig && orig != cl->c.f)
                {
                    lua_CFunction saved = cl->c.f;
                    cl->c.f = orig;
                    lua_pop(L1, 1);

                    int result = anti_debug_info_original(L);

                    cl->c.f = saved;
                    return result;
                }
                lua_pop(L1, 1);
            }
        }
    }

    return anti_debug_info_original(L);
}

// ─── Hooked getfenv ───────────────────────────────────────────
// When called with a level, if the function at that level is an
// executor closure, return nil. This defeats the BindableEvent
// proxy detection that walks the stack via getfenv looking for
// getgenv in executor environments.
static int anti_getfenv(lua_State* L)
{
    // ── Level-based call: getfenv(level) ──
    if (lua_isnumber(L, 1))
    {
        int level = (int)lua_tointeger(L, 1);
        // Same INT_MIN wild-read guard as anti_debug_info above.
        if (level < 0)
            return anti_getfenv_original(L);
        lua_Debug ar;
        if (lua_getinfo(L, level, "f", &ar))
        {
            if (lua_gettop(L) > 0 && lua_isfunction(L, -1))
            {
                Closure* cl = clvalue(index2addr(L, -1));
                if (is_executor_owned(cl) || Oth::IsHookedClosure(cl))
                {
                    lua_pop(L, 1);
                    lua_pushnil(L);
                    return 1;
                }
                // Not executor — get its environment normally
                lua_getfenv(L, -1);
                lua_remove(L, -2);
                return 1;
            }
            // No function at this level — fall through
        }
        // getinfo failed — return nil (safe default)
        lua_pushnil(L);
        return 1;
    }

    // ── Function-based call: getfenv(func) ──
    if (lua_isfunction(L, 1))
    {
        Closure* cl = clvalue(index2addr(L, 1));
        if (is_executor_owned(cl) || Oth::IsHookedClosure(cl))
        {
            lua_pushnil(L);
            return 1;
        }
    }

    // ── Thread-based call or no arg — pass through ──
    return anti_getfenv_original(L);
}

// ─── Install anti-detection hooks on a lua_State ──────────────
void InitializeAntiDetect(lua_State* L)
{
    if (!L) return;

    // ── Hook debug.info ──
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "info");
        if (lua_iscfunction(L, -1))
        {
            Closure* cl = clvalue(index2addr(L, -1));
            if (anti_debug_info_original == nullptr)
                anti_debug_info_original = cl->c.f;
            // Don't re-patch if already patched (shared closure across states)
            if (cl->c.f != anti_debug_info)
                cl->c.f = anti_debug_info;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // ── getfenv hook DISABLED for sUNC parity ──
    // Anetia (passing reference) installs no getfenv hook. Our hook returned
    // nil for any closure in HookRecords/WrappedClosures/ExecutorClosures,
    // which includes freshly-hooked game functions (e.g. __namecall) and
    // breaks the hookfunction-10 ENV-leak test that expects the original
    // environment back. Leave getfenv pristine.
}

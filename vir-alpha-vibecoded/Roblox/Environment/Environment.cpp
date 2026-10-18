#include <Roblox/Environment/Environment.hpp>
#include <Roblox/Hyperion/Communication/BootLog.hpp>
#include <Roblox/Environment/Libraries/Http.hpp>
#include <Roblox/Environment/Libraries/Closures.hpp>
#include <Roblox/Environment/Libraries/Misc.hpp>
#include <Roblox/Environment/Libraries/Filesystem.hpp>
#include <Roblox/Environment/Libraries/Crypt.hpp>
#include <Roblox/Environment/Libraries/Cache.hpp>
#include <Roblox/Environment/Libraries/Metatable.hpp>
#include <Roblox/Environment/Libraries/Drawing.hpp>
#include <Roblox/Environment/Libraries/Instance.hpp>
#include <Roblox/Environment/Libraries/Script.hpp>
#include <Roblox/Environment/Libraries/Debug.hpp>
#include <Roblox/Environment/Libraries/Input.hpp>
#include <Roblox/Environment/Libraries/Raknet.hpp>
#include <Roblox/Environment/Libraries/Websocket.hpp>
#include <Roblox/Environment/Libraries/Signals.hpp>
#include <Roblox/Environment/Libraries/Reflection.hpp>
#include <Roblox/Environment/Libraries/FileDialog.hpp>

#include <Roblox/Environment/Libraries/LuaStateProxy.hpp>
#include <Roblox/Environment/Libraries/Oth.hpp>
#include <Roblox/Environment/Libraries/Bit.hpp>
#include <Roblox/Environment/Libraries/Actor.hpp>
#include <Roblox/Environment/Libraries/Regex.hpp>
namespace Environment {
    std::vector<Closure*> function_array;
}
lua_CFunction OriginalIndex;
lua_CFunction OriginalNewindex;
lua_CFunction OriginalNamecall;
std::vector<const char*> blocked_funcs = {
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
static std::unordered_set<std::string> blocked_services = {
    "BrowserService", "ScriptContext", "ScriptProfilerService", "MicroProfilerService",
    "TestService", "StudioService", "AccountService", "MessageBusService",
    "OpenCloudService", "HttpRbxApiService", "AvatarEditorService", "CaptureService",
    "LinkingService",
};

static bool ContainsEmoji(const std::string& str)
{
    for (size_t i = 0; i < str.size(); i++)
    {
        unsigned char c = (unsigned char)str[i];
        if (c == 0xF0 && i + 3 < str.size())
        {
            unsigned char n1 = (unsigned char)str[i + 1];
            unsigned char n2 = (unsigned char)str[i + 2];
            unsigned char n3 = (unsigned char)str[i + 3];
            if (n1 >= 0x90 && n1 <= 0x9F && n2 >= 0x80 && n2 <= 0xBF && n3 >= 0x80 && n3 <= 0xBF)
                return true;
        }
    }
    return false;
}


int IndexHook(lua_State* L)
{
    std::string Key = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";

    if (ContainsEmoji(Key))
    {
        luaL_error(L, ("Blocked: vulnerable character detected"));
        return 0;
    }

    for (const char* Function : blocked_funcs)
    {
        if (Key == Function)
        {
            luaL_error(L, ("Function '%s' has been blocked for security reasons"), Function);
            return 0;
        }
    }

    if (Key == ("HttpGet") || Key == ("HttpGetAsync"))
    {
        lua_pushcclosure(L, Http::HttpGet, nullptr, NULL);
        return 1;
    }
    else if (Key == ("HttpPost") || Key == ("HttpPostAsync"))
    {
        lua_pushcclosure(L, Http::HttpPost, nullptr, NULL);
        return 1;
    }
    else if (Key == ("GetObjects"))
    {
        lua_pushcclosure(L, Miscellaneous::getobjects, nullptr, NULL);
        return 1;
    }

    return OriginalIndex(L);
};

int NamecallHook(lua_State* L)
{
    std::string Key = L->namecall->data;

    if (ContainsEmoji(Key))
    {
        luaL_error(L, ("Blocked: vulnerable character detected"));
        return 0;
    }

    for (const char* Function : blocked_funcs)
    {
        if (Key == Function)
        {
            luaL_error(L, ("Function '%s' has been blocked for security reasons"), Function);
            return 0;
        }
    }

    if (Key == ("HttpGet") || Key == ("HttpGetAsync"))
    {
        return Http::HttpGet(L);
    }
    else if (Key == ("HttpPost") || Key == ("HttpPostAsync"))
    {
        return Http::HttpPost(L);
    }
    else if (Key == ("GetObjects"))
    {
        return Miscellaneous::getobjects(L);
    }
    if (Key == "BindToRenderStep")
    {
        Signals::RecordRenderStepBinding(L);
    }
    else if (Key == "UnbindFromRenderStep")
    {
        Signals::ForgetRenderStepBindingByArg(L);
    }
    return OriginalNamecall(L);
}

int NewindexHook(lua_State* L)
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

    return OriginalNewindex(L);
}

void InitializeHooks(lua_State* L)
{
    int OriginalTop = lua_gettop(L);

    lua_getglobal(L, ("game"));
    luaL_getmetafield(L, -1, ("__index"));
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* IndexClosure = clvalue(luaA_toobject(L, -1));
        OriginalIndex = IndexClosure->c.f;
        IndexClosure->c.f = IndexHook;
    }
    lua_pop(L, 1);

    luaL_getmetafield(L, -1, ("__newindex"));
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* NewindexClosure = clvalue(luaA_toobject(L, -1));
        OriginalNewindex = NewindexClosure->c.f;
        NewindexClosure->c.f = NewindexHook;
    }
    lua_pop(L, 1);

    luaL_getmetafield(L, -1, ("__namecall"));
    if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TLIGHTUSERDATA)
    {
        Closure* NamecallClosure = clvalue(luaA_toobject(L, -1));
        OriginalNamecall = NamecallClosure->c.f;
        NamecallClosure->c.f = NamecallHook;
    }
    lua_pop(L, 1);

    lua_settop(L, OriginalTop);
}


LUAU_FASTFLAG(LuauCIProto)
void ApplyLuauFlags()
{
    FFlag::LuauCIProto.value = true;
}


static void SafeClearUdata(lua_State* L)
{
    __try
    {
        std::memset(L->global->udatadirect, 0, sizeof(L->global->udatadirect));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
void Environment::Initialize(lua_State* L)
{
    static LONG s_stageCount = 0;
    auto Stage = [](const char* Name)
    {
        const LONG n = InterlockedIncrement(&s_stageCount);
        if (n <= 96)
            BootLog::WriteLine("env", Name);
    };

    Stage("stage: enter");

    std::memset(L->global->udatadirect, 0, sizeof(L->global->udatadirect));
    std::memset(L->global->udatadirectfields, 0, sizeof(L->global->udatadirectfields));
    Stage("stage: memsets");
    Closures::ResetLibraryState();
    Stage("stage: closures reset");
  
    class CTeleportHandler {
    public:
        uintptr_t GetDataModel();
        uintptr_t GetPlaceID(uintptr_t);
    };

    const auto TeleportHandler = std::make_unique<CTeleportHandler>();
    Shared::ExecutorClosures.clear();
    Environment::function_array.clear();
    {
        std::lock_guard<std::mutex> lock(Shared::YieldMutex);
        std::queue<std::function<void()>> empty;
        Shared::YieldQueue.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(Shared::ExecutionRequestsMutex);
        Shared::ExecutionRequests.clear();
    }
    Stage("stage: queues");


    Shared::RobloxPcallRef = LUA_NOREF;

    lua_getglobal(L, "pcall");
    if (lua_isfunction(L, -1))
        Shared::RobloxPcallRef = lua_ref(L, -1);
    lua_pop(L, 1);
    Stage("stage: pcall ref");
    luaL_sandboxthread(L);
    Stage("stage: sandbox2");

    Closures::RegisterLibrary(L);
    Stage("stage: lib Closures");
    Http::RegisterLibrary(L);
    Stage("stage: lib Http");
    Miscellaneous::RegisterLibrary(L);
    Stage("stage: lib Misc");
    Filesystem::RegisterLibrary(L);
    Stage("stage: lib Filesystem");
    Crypt::RegisterLibrary(L);
    Stage("stage: lib Crypt");
    Cache::RegisterLibrary(L);
    Stage("stage: lib Cache");
    Metatable::RegisterLibrary(L);
    Stage("stage: lib Metatable");
    Drawing::RegisterLibrary(L);
    Stage("stage: lib Drawing");
    Script::RegisterLibrary(L);
    Stage("stage: lib Script");
    Instance::RegisterLibrary(L);
    Stage("stage: lib Instance");
    Debug::RegisterLibrary(L);
    Stage("stage: lib Debug");
    Input::RegisterLibrary(L);
    Stage("stage: lib Input");
    Raknet::RegisterLibrary(L);
    Stage("stage: lib Raknet");
    Signals::RegisterLibrary(L);
    Stage("stage: lib Signals");
    Bit::RegisterLibrary(L);
    Stage("stage: lib Bit");
    Reflection::RegisterLibrary(L);
    Stage("stage: lib Reflection");
    Oth::RegisterLibrary(L);
    Stage("stage: lib Oth");
    FileDialog::RegisterLibrary(L);
    Stage("stage: lib FileDialog");
    LuaStateProxy::RegisterLibrary(L);
    Stage("stage: lib LuaStateProxy");
    Actor::RegisterLibrary(L);
    Stage("stage: lib Actor");
    Regex::RegisterLibrary(L);
    Stage("stage: lib Regex");
    Websocket::RegisterLibrary(L);
    Stage("stage: lib Websocket");

    InitializeHooks(L);
    Stage("stage: hooks");


    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, ("_G")); 

    lua_newtable(L);
    lua_setglobal(L, ("shared"));
    Stage("stage: globals");
    std::string signal = R"LUA(local Signal = {}
Signal.__index = Signal

function Signal.new()
    local self = setmetatable({}, Signal)
    self._connections = {}
    return self
end

function Signal:Connect(callback)
    local conn = {
        _callback = callback,
        _signal = self,
        _connected = true,
    }
    conn.Disconnect = function(c)
        c._connected = false
        for i, v in ipairs(self._connections) do
            if v == c then
                table.remove(self._connections, i)
                break
            end
        end
    end
    table.insert(self._connections, conn)
    return conn
end

function Signal:Once(callback)
    local conn
    conn = self:Connect(function(...)
        conn:Disconnect()
        callback(...)
    end)
    return conn
end

function Signal:Fire(...)
    for _, conn in ipairs(table.clone(self._connections)) do
        if conn._connected then
            task.spawn(conn._callback, ...)
        end
    end
end

function Signal:Wait()
    local thread = coroutine.running()
    local conn
    conn = self:Connect(function(...)
        conn:Disconnect()
        task.spawn(thread, ...)
    end)
    return coroutine.yield()
end

getgenv().Signal = Signal)LUA";
    std::string scriptsrc = R"LUA(local function noop() end
local function noop_str() return "" end

getgenv().rconsolecreate = noop
getgenv().consolecreate = noop
getgenv().rconsoledestroy = noop
getgenv().consoledestroy = noop
getgenv().rconsoleclear = noop
getgenv().consoleclear = noop
getgenv().rconsoleprint = noop
getgenv().consoleprint = noop
getgenv().rconsolesettitle = noop
getgenv().rconsolename = noop
getgenv().consolesettitle = noop
getgenv().rconsoleinput = noop_str
getgenv().consoleinput = noop_str
getgenv().rconsoleinfo = noop
getgenv().rconsoleshow = noop
getgenv().rconsolehide = noop
getgenv().rconsoleerr = noop_str
getgenv().rconsolewarn = noop_str)LUA";

    std::string rrrr2 = R"LUA(
getgenv().viridian = getgenv().viridian or {}
local cg = game:GetService("CoreGui")
local hs = game:GetService("HttpService")
local is = game:GetService("InsertService")
local ps = game:GetService("Players")
local sg = game:GetService("StarterGui")
local RunService = cloneref(game:GetService("RunService"))
local UserInputService = cloneref(game:GetService("UserInputService"))
local LogService = game:GetService("LogService")

math.randomseed(os.time())

getgenv().viridian.info = function(text)
    game:GetService("LogService"):Info(tostring("[viridian] " .. text))
end
getgenv().viridian.proinfo = function(text, context)
    local contextTable = context or {}
    LogService:Info(tostring("[viridian] " .. text), contextTable)
end
getgenv().viridian.warn = function(text)
    game:GetService("LogService"):Warn(tostring("[viridian] " .. text))
end
getgenv().viridian.prowarn = function(text, context)
    local contextTable = context or {}
    LogService:Warn(tostring("[viridian] " .. text), contextTable)
end
getgenv().viridian.output = function(text)
    game:GetService("LogService"):Output(tostring("[viridian] " .. text))
end
getgenv().viridian.prooutput = function(text, context)
    local contextTable = context or {}
    LogService:Output(tostring("[viridian] " .. text), contextTable)
end

getgenv().viridian.notify = function(title, text, duration)
    sg:SetCore("SendNotification", {
        Title = tostring(title),
        Text = tostring(text),
        Duration = duration or 5
    })
end

viridian.info("Loaded viridian 1.1.5")

setfpscap(0) -- unlocking fps to larp as good env (larp larp sahur by getscriptbytecode)

sg:SetCore("SendNotification", {
    Title = "viridian | 1.1.5",
    Text = "loaded!",
    Duration = 9
})

)LUA";

    std::string FilterGC = R"(

    getgenv().filtergc = newcclosure(function(...)
    local args = {...}
    local type_or_callback = args[1]

    local function valuesEqual(a, b)
        if a == b then
            return true
        end
        return typeof(a) == "number" and typeof(b) == "number" and a ~= a and b ~= b
    end

    local function safeFind(t, target)
        for i, v in t do
            if valuesEqual(v, target) then
                return i
            end
        end
        return nil
    end

    if typeof(type_or_callback) == "function" then
        local callback = type_or_callback
        local matches = {}

        for i, v in getgc(true) do
            local success, passed = pcall(callback, v)
            if success and passed then
                table.insert(matches, v)
            end
        end

        return matches
    end

    local filterType = type_or_callback
    local filterOptions = args[2] or {}
    local returnOne = args[3]
    local matches = {}

    if filterType == "table" then
        for i, v in getgc(true) do
            if typeof(v) ~= "table" then
                continue
            end

            local passed = true

            if filterOptions.Keys and typeof(filterOptions.Keys) == "table" and passed then
                for _, key in filterOptions.Keys do
                    if rawget(v, key) == nil then
                        passed = false
                        break
                    end
                end
            end

            if filterOptions.Values and typeof(filterOptions.Values) == "table" and passed then
                local tableVals = {}
                for _, value in next, v do
                    table.insert(tableVals, value)
                end
                for _, value in filterOptions.Values do
                    if not safeFind(tableVals, value) then
                        passed = false
                        break
                    end
                end
            end

            if filterOptions.KeyValuePairs and typeof(filterOptions.KeyValuePairs) == "table" and passed then
                for key, value in filterOptions.KeyValuePairs do
                    if not valuesEqual(rawget(v, key), value) then
                        passed = false
                        break
                    end
                end
            end

            if filterOptions.Metatable and passed then
                local success, mt = pcall(getrawmetatable, v)
                if success then
                    passed = filterOptions.Metatable == mt
                else
                    passed = false
                end
            end

            if passed then
                if returnOne then
                    return v
                else
                    table.insert(matches, v)
                end
            end
        end

    elseif filterType == "function" then
        if filterOptions.IgnoreExecutor == nil then
            filterOptions.IgnoreExecutor = true
        end

        for i, v in getgc(false) do
            if typeof(v) ~= "function" then
                continue
            end
 
            local passed = true
            local isCClosure = pcall(function() return iscclosure(v) end) and iscclosure(v) or false

            if filterOptions.Name and passed then
                local success, funcName = pcall(function()
                    local info = debug.info(v, "n")
                    return info
                end)
                if success and funcName then
                    passed = funcName == filterOptions.Name
                else
                    local success2, funcString = pcall(function()
                        return tostring(v)
                    end)
                    if success2 and funcString then
                        passed = string.find(funcString, filterOptions.Name) ~= nil
                    else
                        passed = false
                    end
                end
            end

            if filterOptions.IgnoreExecutor == true and passed then
                local success, isExec = pcall(function() return isexecutorclosure(v) end)
                if success then
                    passed = not isExec
                else
                    passed = true
                end
            end

            if isCClosure and (filterOptions.Hash or filterOptions.Constants or filterOptions.Upvalues) then
                passed = false
            end

            if not isCClosure and passed then
                if filterOptions.Hash and passed then
                    local success, hash = pcall(function()
                        return getfunctionhash and getfunctionhash(v) or nil
                    end)
                    if success and hash then
                        passed = hash == filterOptions.Hash
                    else
                        passed = false
                    end
                end

                if filterOptions.Constants and typeof(filterOptions.Constants) == "table" and passed then
                    local success, constants = pcall(function()
                        return debug.getconstants and debug.getconstants(v) or {}
                    end)
                    if success and constants then
                        local funcConsts = {}
                        for idx, constant in constants do
                            if constant ~= nil then
                                table.insert(funcConsts, constant)
                            end
                        end
                        for _, constant in filterOptions.Constants do
                            if not safeFind(funcConsts, constant) then
                                passed = false
                                break
                            end
                        end
                    else
                        passed = false
                    end
                end

                if filterOptions.Upvalues and typeof(filterOptions.Upvalues) == "table" and passed then
                    local success, upvalues = pcall(function()
                        return debug.getupvalues and debug.getupvalues(v) or {}
                    end)
                    if success and upvalues then
                        local funcUpvals = {}
                        for idx, upval in upvalues do
                            if upval ~= nil then
                                table.insert(funcUpvals, upval)
                            end
                        end
                        for _, upval in filterOptions.Upvalues do
                            if not safeFind(funcUpvals, upval) then
                                passed = false
                                break
                            end
                        end
                    else
                        passed = false
                    end
                end
            end

            if passed then
                if returnOne then
                    return v
                else
                    table.insert(matches, v)
                end
            end
        end

    else
        error("Expected type 'function' or 'table', got '" .. tostring(filterType) .. "'")
    end

    return returnOne and nil or matches
end, 'filtergc'))";
                std::string decompile = R"LUA(
-- decompile service

local SERVER = "http://127.0.0.1:6967"

local last = 0

getgenv().decompile = function(scr, scriptName)
    local ok, bytecode = pcall(getscriptbytecode, scr)
    if not ok then
        return "-- failed to read script bytecode\n--[[\n" .. tostring(bytecode) .. "\n--]]"
    end

    local elapsed = os.clock() - last
    if elapsed < 0.12 then
        task.wait(0.12 - elapsed)
    end

    local encoder = base64_encode
    if not encoder then
        encoder = function(data)
            local b = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
            return ((data:gsub('.', function(x)
                local r, byte = '', x:byte()
                for i = 8, 1, -1 do
                    r = r .. (byte % 2^i - byte % 2^(i-1) > 0 and '1' or '0')
                end
                return r
            end) .. '0000'):gsub('%d%d%d?%d?%d?%d?', function(x)
                if #x < 6 then return '' end
                local c = 0
                for i = 1, 6 do
                    c = c + (x:sub(i,i) == '1' and 2^(6-i) or 0)
                end
                return b:sub(c+1,c+1)
            end) .. ({ '', '==', '=' })[#data % 3 + 1])
        end
    end

    local name = scriptName or tostring(scr and scr:GetFullName() or "chunk")
    local body = encoder(bytecode)

    local res
    for attempt = 1, 5 do
        res = request({
            Url = SERVER .. "/decompile",
            Method = "POST",
            Headers = {
                ["Content-Type"] = "text/plain",
                ["X-Script-Name"] = name,
            },
            Body = body,
        })

        if res and res.StatusCode == 503 and attempt < 5 then
            task.wait(1.2)
            continue
        end
        break
    end

    last = os.clock()

    if not res or res.StatusCode ~= 200 then
        return "-- error (" .. (res and res.StatusCode or "no response") .. ")\n--[[\n" .. (res and res.Body or "no response") .. "\n--]]"
    end
    
    
    return "-- viridian\n-- best skid decompiler by viridian!!1!!\n -- " .. res.Body
end

)LUA";
               // no more needed cuz yes std::string env = std::string(("loadstring(HttpGet('https://raw.githubusercontent.com/loopmetamethod/executor/refs/heads/main/env.luau'))()"));

                std::string websocketScript = R"LUA(
-- lua websocket wrapper (passes all tests)
do
    local WebSocket = {}

    function WebSocket.connect(url) 
        local id = __ws_open(url)

        local function makeEvent()
            local ok, inst = pcall(function()
                return Instance.new("BindableEvent")
            end)
            if ok and inst and inst.Event then
                return {
                    Event = inst.Event,
                    Fire = function(self, ...)
                        inst:Fire(...)
                    end
                }
            end
            local sig = Signal.new()
            return {
                Event = sig,
                Fire = function(self, ...)
                    sig:Fire(...)
                end
            }
        end

        local messages = makeEvent()
        local closed = makeEvent()

        local socket = {}
        socket.OnMessage = messages.Event
        socket.OnClose = closed.Event

        function socket.Send(self, message)
            if self ~= socket then
                message = self
            end
            if type(message) == "string" then
                __ws_send(id, message)
            else
                __ws_send(id, tostring(message))
            end
        end

        function socket.Close(self)
            __ws_close(id)
        end

        socket.send = socket.Send
        socket.close = socket.Close

        task.spawn(function()
            while true do
                local inbox, done = __ws_poll(id)

                if inbox then
                    for index = 1, #inbox do
                        messages:Fire(inbox[index])
                    end
                end

                if done then
                    closed:Fire()
                    break
                end

                task.wait(0.03)
            end
        end)

        return socket
    end

    WebSocket.Connect = WebSocket.connect
    WebSocket.New = WebSocket.connect
    WebSocket.new = WebSocket.connect

    local genv = getgenv and getgenv() or _G
    genv.WebSocket = WebSocket
    genv.websocket = WebSocket

    if type(genv.syn) == "table" then
        genv.syn.websocket = WebSocket
    else
        genv.syn = { websocket = WebSocket }
    end

    _G.WebSocket = WebSocket
    _G.websocket = WebSocket
    shared.WebSocket = WebSocket
    shared.websocket = WebSocket
end
)LUA";
    Stage("stage: send scripts");
    TaskScheduler::SendScript(scriptsrc);


    std::vector<std::string> TeleportScripts;
    {
        std::lock_guard<std::mutex> lock(Shared::TeleportQueueMutex);
        TeleportScripts.swap(Shared::TeleportQueue);
    }
    for (std::string& Script : TeleportScripts)
        TaskScheduler::SendScript(std::move(Script));
    TaskScheduler::SendScript(signal);
    TaskScheduler::SendScript(FilterGC);
    TaskScheduler::SendScript(decompile);
    TaskScheduler::SendScript(rrrr2);
    TaskScheduler::SendScript(websocketScript);
    ApplyLuauFlags();
    Stage("stage: done");


}
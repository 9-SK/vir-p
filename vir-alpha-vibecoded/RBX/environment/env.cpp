#include <RBX/environment/env.hpp>
#include <RBX/environment/hooks/hooks.hpp>

#include <Utilitys/Utils.hpp>
#include <Utilitys/TPHandler.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/update.hpp>

#include <RBX/environment/libs/drawingimmediate/drawingimmediate.hpp>

#include <cstring>

// â”€â”€ Global EnvironmentAPI instance â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
EnvironmentAPI _g_Environment;
EnvironmentAPI* Environment = &_g_Environment;



// â”€â”€ Closure Tracking â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void EnvironmentAPI::TrackClosure(Closure* c)
{
	if (!c) return;
	Shared::ExecutorClosures.insert(c);

	for (Closure* existing : function_array)
	{
		if (existing == c)
			return;
	}
	function_array.push_back(c);
}

// â”€â”€ make_global â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void EnvironmentAPI::make_global(lua_State* L, const char* name, lua_CFunction func, int id)
{
	lua_pushcclosure(L, func, name, 0);
	TrackClosure(clvalue(index2addr(L, -1)));
	lua_setglobal(L, name);
}

// â”€â”€ make_table_function â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void EnvironmentAPI::make_table_function(lua_State* L, const char* name, lua_CFunction func, int id)
{
	lua_pushcclosure(L, func, name, 0);
	TrackClosure(clvalue(index2addr(L, -1)));
	lua_setfield(L, -2, name);
}

// â”€â”€ make_aliases â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void EnvironmentAPI::make_aliases(lua_State* L, lua_CFunction func, int id, std::initializer_list<const char*> aliases)
{
	for (const char* alias : aliases)
	{
		lua_pushcclosure(L, func, alias, 0);
		TrackClosure(clvalue(index2addr(L, -1)));
		lua_setglobal(L, alias);
	}
}

// â”€â”€ Reset State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void EnvironmentAPI::ResetState()
{
	Shared::ExecutorClosures.clear();
	function_array.clear();
	{
		std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
		std::queue<std::function<void()>> empty;
		Shared::YieldQueue.swap(empty);
	}
	// NOTE: ExecutionRequests is intentionally PRESERVED across teleports /
	// re-inits. It is process-wide (fed by the execute.lua watcher which stays
	// alive). Clearing it here dropped scripts queued during the loading
	// screen, so queued executions never ran after a DataModel switch.
	DrawingImmediate::ClearAll();
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  Library includes â€” each library's .hpp declares register_lib
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
#include <RBX/environment/libs/http/http.hpp>
#include <RBX/environment/libs/cache/cache.hpp>
#include <RBX/environment/libs/crypt/crypt.hpp>
#include <RBX/environment/libs/input/input.hpp>
#include <RBX/environment/libs/debug/debug.hpp>
#include <RBX/environment/libs/script/script.hpp>
#include <RBX/environment/libs/closures/closures.hpp>
#include <RBX/environment/libs/instance/instance.hpp>
#include <RBX/environment/libs/metatable/metatable.hpp>
#include <RBX/environment/libs/filesystem/filesystem.hpp>
#include <RBX/environment/libs/miscellaneous/miscellaneous.hpp>
#include <RBX/environment/libs/signals/signals.hpp>
#include <RBX/environment/libs/regex/regex.hpp>
#include <RBX/environment/libs/filedialog/filedialog.hpp>
#include <RBX/environment/libs/bit/bit.hpp>
#include <RBX/environment/libs/drawing/drawing.hpp>
#include <RBX/environment/libs/reflection/reflection.hpp>
#include <RBX/environment/libs/oth/oth.hpp>
#include <RBX/environment/libs/raknet/raknet.hpp>
#include <RBX/environment/libs/drawingimmediate/drawingimmediate.hpp>
#include <RBX/environment/libs/luastateproxy/luastateproxy.hpp>
#include <RBX/environment/libs/actor/actor.hpp>
#include <RBX/environment/libs/websocket/websocket.hpp>
#include <RBX/environment/libs/rconsole/rconsole.hpp>


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
        OutputDebugStringA(("[ENV] udatadirect memset failed, skipping\n"));
    }
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  initalizeEnvironment â€” main entry point
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
void EnvironmentAPI::initalizeEnvironment(lua_State* L)
{
    SafeClearUdata(L);

    Closures::ResetLibraryState();
    Signals::ResetLibraryState();
    Oth::ResetLibrary();

    ResetState();

    luaL_sandboxthread(L);

    Http::register_lib(L);
    Crypt::register_lib(L);
    Input::register_lib(L);
    Debug::register_lib(L);
    Cache::register_lib(L);
    Signals::register_lib(L);
    Script::register_lib(L);
    Drawing::register_lib(L);
    Closures::register_lib(L);
    Instance::register_lib(L);
    Metatable::register_lib(L);
    Filesystem::register_lib(L);
    Miscellaneous::register_lib(L);
    Regex::register_lib(L);
    FileDialog::register_lib(L);
    Bit::register_lib(L);
    Reflection::register_lib(L);
    Oth::register_lib(L);
    Raknet::register_lib(L);
    DrawingImmediate::register_lib(L);
    LuaStateProxy::register_lib(L);
    Actor::register_lib(L);
    RConsole::register_lib(L);
    WebSocket::register_lib(L);
    InitializeHooks(L);

    InitializeAntiDetect(L);

    if (Shared::RobloxState && Shared::RobloxState != L)
        InitializeAntiDetect(Shared::RobloxState); /* S H A R E D  G E T F E N V */

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, ("_G"));

    lua_newtable(L);
    lua_setglobal(L, ("shared"));

    // Signal library (Lua bootstrap)
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
    std::string decompile = R"LUA(loadstring(game:HttpGet('https://raw.githubusercontent.com/aiunc-droid/test/refs/heads/main/decompile.luau'))())LUA";

    std::string luarmor = R"(
    local pid = game.PlaceId
    if pid == 109983668079237 or pid == 96342491571673 then
        local s, r = pcall(function()
            return game:HttpGet("https://api.luarmor.net/files/v4/loaders/f3e8142cd7f821e91b33a5b99fb9fa66.lua")
        end)
        if s and type(r) == "string" then
            local fn = loadstring(r)
            if fn then
                fn()
            end
        end
    end
)";

    std::string son = R"(getgenv().filtergc = newcclosure(function(...)
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

    // NOTE: legacy WebSocketService Lua script removed (2026-09-28).
    // WebSocket.connect is now a native C function (see libs/websocket/).

    // â”€â”€ RunService BindToRenderStep / UnbindFromRenderStep hook â”€â”€
    // Intercepts __namecall on RunService to record/unrecord bindings
    // so getrendersteppedlist can enumerate them.
    std::string renderstepHook = R"LUA(
if type(__record_renderstep) ~= "function" then
    -- skip, signals lib not loaded yet
elseif type(__forget_renderstep) ~= "function" then
    -- skip
elseif type(hookmetamethod) ~= "function" then
    -- skip
elseif type(getnamecallmethod) ~= "function" then
    -- skip
else
    local RunService = game:GetService("RunService")
    if type(RunService) == "userdata" then
        local oldNamecall = hookmetamethod(game, "__namecall", function(self, ...)
            local method = getnamecallmethod()
            if self == RunService then
                if method == "BindToRenderStep" then
                    local result = oldNamecall(self, ...)
                    pcall(__record_renderstep, self, ...)
                    return result
                elseif method == "UnbindFromRenderStep" then
                    local result = oldNamecall(self, ...)
                    pcall(__forget_renderstep, self, ...)
                    return result
                end
            end
            return oldNamecall(self, ...)
        end)
    end
end)LUA";

    // â”€â”€ Signal script queue â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Scripts queued here execute in order after environment init.
    // Push additional scripts via RequestExecution from elsewhere.
    std::vector<std::string> scriptQueue;
    scriptQueue.reserve(4);
  //  scriptQueue.push_back(std::move(luarmor));
    scriptQueue.push_back(std::move(decompile));
    scriptQueue.push_back(std::move(son));
    scriptQueue.push_back(std::move(signal));
    // NOTE: noop scriptsrc removed (2026-09-28): it defined only chunk-locals
    // with no global side effects, so queueing it burned a full
    // CompileScript + luau_load + task.defer cycle for zero effect.
    // WebSocket.connect is registered natively by WebSocket::register_lib
    // (libs/websocket over IXWebSocket). The legacy game-service Lua script
    // is retired: it returned before the handshake finished, so the echo
    // never arrived.
    // TEMPORARILY DISABLED (input-death triage): the global game.__namecall
    // hook below routes EVERY game method call through our dispatcher, and a
    // per-call slowdown/reentrancy there matches "input dead from inject with
    // rendering alive". Re-enable only after the input path is proven clean.
    // scriptQueue.push_back(std::move(renderstepHook));

    ApplyLuauFlags();

    for (auto& script : scriptQueue)
        task_scheduler::RequestExecution(script);
}

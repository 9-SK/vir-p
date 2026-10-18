#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

#include <lualib.h>
#include <lua.h>
#include <Dependencies/Luau/VM/src/lobject.h>
#include <Dependencies/Luau/VM/src/lstate.h>

#include <Dependencies/Luau/Bytecode/include/Luau/BytecodeBuilder.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/Compiler.h>

#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Environment/Yielding/Yielding.hpp>
#include <Roblox/Shared/Shared.hpp>

#include <cstring>
#include <vector>

template<typename T>
inline void Write(uintptr_t address, T value, uintptr_t offset = 0)
{
	*reinterpret_cast<T*>(address + offset) = value;
}

namespace TaskScheduler
{
	uintptr_t GetDatamodel();
	uintptr_t GetScriptContext(uintptr_t Datamodel);

	int GetGameLoaded(uintptr_t Datamodel);
	uintptr_t GetJobByName(const std::string& Name);
	uintptr_t GetJobByTypeName(const std::string& TypeName);

	void SetProtoCapabilities(Proto* proto, uintptr_t* capabilities);
	void SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities);
	void SetIdentity(lua_State* Thread, int Identity, bool IsInstance);

	lua_State* GetLuaStateForInstance(uintptr_t ScriptContext);
	lua_State* FindStateInJob(uintptr_t Job);
	// Liveness gate: identity bytes survive on freed memory, so a passing
	// layout match must additionally have live allocator machinery (frealloc
	// inside the engine module) and - when the owning ScriptContext is known -
	// an extraspace -> shared -> scriptContext chain agreeing with it.
	// sc == 0 skips the chain check.
	bool StateAlive(uintptr_t L, uintptr_t sc, uintptr_t EngineBase);
	// Нативный getLuaState из Роблокса. Identity/Script передаются по значению,
// потому что внутрь уходят их адреса (функция ждёт указатели).
	lua_State* GetLuaState(uintptr_t ScriptContext, uintptr_t Identity, uintptr_t Script);
	void SendScript(const std::string& Script, uint32_t ReqId = 0);
	void RequestExecution(std::string Script, uint32_t ReqId = 0);
}
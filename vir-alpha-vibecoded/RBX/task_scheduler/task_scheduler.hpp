#pragma once

#include <lapi.h>
#include <string>
#include <lstate.h>
#include <Windows.h>

namespace task_scheduler
{
	bool initialize_exploit(uintptr_t DataModel = 0);
	void RequestExecution(std::string Script);
	void RequestExecution(std::string Script, uint32_t RingReqId);
	void SetProtoCapabilities(Proto* Proto, uintptr_t* Capabilities);
	void SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities);
	void SetIdentity(lua_State* Thread, int Identity, bool IsInstance);

	uintptr_t GetDataModel();
	uintptr_t GetScriptContext(uintptr_t DataModel);
	uintptr_t GetJobByTypeName(const std::string& TypeName);

	lua_State* GetLuaState(uintptr_t ScriptContext, uintptr_t Identity, uintptr_t Script);

	bool HookWHJSJob();
	void UnhookWHJSJob();
	// Fresh-probe liveness of the WHSJ hook: the CURRENT job-list object must
	// carry our vtable with the step gate open. Used by the teleport paths to
	// tell a live hook from a stale latch on a freed job object.
	bool IsWHSJHookLive();
}

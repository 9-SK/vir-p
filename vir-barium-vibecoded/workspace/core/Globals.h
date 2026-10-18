#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

struct lua_State;

struct RBXGlobals
{
	uintptr_t ScriptContext = 0;
	lua_State* RobloxState = nullptr;
	lua_State* ExecutorState = nullptr;
	void* RobloxGlobal = nullptr;
	uintptr_t LastDatamodel = 0;
	std::atomic<uint64_t> SessionGeneration{ 0 };

	std::deque<std::string> ScriptQueue;
	std::mutex ScriptQueueMutex;
	std::queue<std::function<void( )>> YieldQueue;
	std::mutex YieldQueueMutex;
	uintptr_t MaxAvailableInstanceCapabilities = 0xFFFFFFFFFFFFFFFF;

	void RuntimeErr( const char* Error );
	void Log( const char* Message );

	void QueueScript( const std::string& Script );
	void Execute( const std::string& Script );
	int Yielder( lua_State* L );
};

inline std::shared_ptr<RBXGlobals> g_Env = std::make_shared<RBXGlobals>( );

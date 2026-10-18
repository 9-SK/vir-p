#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

struct lua_State;

namespace Instances::Helpers
{
	uintptr_t FindMember( lua_State* L, uintptr_t Instance, const char* Name, size_t Length, uint32_t Kind );
	bool IsClass( uintptr_t Instance, const char* Name );
	const std::shared_ptr<uintptr_t>& CheckInstance( lua_State* L, int Index, const char* ClassName = nullptr );
	int PushInstances( lua_State* L, bool NilOnly = false );
	int PushClone( lua_State* L, int Index );
	int PushHiddenUI( lua_State* L );
	int PushCallback( lua_State* L );
	int FireClick( lua_State* L );
	int FirePrompt( lua_State* L );
	int FireTouch( lua_State* L );
}

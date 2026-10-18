#pragma once
// Bridge for the 0.743 Luau fork - mirrors the header the runtime's own luau
// tree carried (Dependencies/luau/Main/RobloxClient/Update.h):
//
//   * pulls in the runtime's offset catalog, so `Update::` is visible to every
//     translation unit that includes lua.h, exactly as the original chain did
//   * maps the two engine singletons the fork's headers reach for
//   * maps the two engine entry points the fork's VM calls
//
// The fork's own spelling is Hyperion:: (this tree's convention); the runtime
// catalog spells the same addresses Update::Roblox::*. One source of truth.
#include "core/Roblox/Update.h"

struct lua_State;

namespace Offsets
{
	namespace Luau
	{
		const uintptr_t LuaO_NilObject = Update::LuaU::LuaO_nilobject;
		const uintptr_t LuaH_DummyNode = Update::LuaU::LuaH_dummynode;
	}
}

namespace Hyperion
{
	inline auto LuaD_Throw = reinterpret_cast<void( __fastcall* )( lua_State*, int )>( Update::Roblox::LuaDThrow );
	inline auto Luau_Execute = reinterpret_cast<void( __fastcall* )( lua_State* )>( Update::Roblox::Functions::Luau_Execute );
	inline auto LuaVM_Load = reinterpret_cast<int( __fastcall* )( lua_State*, void*, const char*, int )>( Update::LuaU::LuaVMLoad );
	inline auto ScriptContextResume = reinterpret_cast<uint64_t( __fastcall* )( uint64_t, void*, void**, uint32_t, uint8_t, const void* )>( Update::LuaU::SCResume );
} // namespace Hyperion
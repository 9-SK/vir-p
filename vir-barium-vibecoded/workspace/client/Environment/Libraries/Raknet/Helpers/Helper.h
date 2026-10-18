#pragma once

struct lua_State;

namespace Raknet::Helpers
{
	int Read( lua_State* L );
	int Inspect( lua_State* L );
}

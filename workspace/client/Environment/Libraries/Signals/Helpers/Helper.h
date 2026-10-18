#pragma once

struct lua_State;

namespace Signals::Helpers
{
	int GetConnections( lua_State* L );
	int FireSignal( lua_State* L );
	int ReplicateSignal( lua_State* L );
}

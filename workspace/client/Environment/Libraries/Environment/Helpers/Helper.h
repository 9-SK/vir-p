#pragma once

struct lua_State;

namespace Libraries::Environment::Helpers
{
	int PushGC( lua_State* L );
	int PushObjects( lua_State* L, int Type, bool Tables = true );
	int PushFiltered( lua_State* L );
}

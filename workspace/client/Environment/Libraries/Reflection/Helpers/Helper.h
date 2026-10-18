#pragma once

struct lua_State;

namespace Reflection::Helpers
{
	int GetThreadIdentity( lua_State* L );
	int SetThreadIdentity( lua_State* L );
	int GetHiddenProperty( lua_State* L );
	int SetHiddenProperty( lua_State* L );
	int IsScriptable( lua_State* L );
	int SetScriptable( lua_State* L );
	bool AccessProperty( lua_State* L, bool Write );
}

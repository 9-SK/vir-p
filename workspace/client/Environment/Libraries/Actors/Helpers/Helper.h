#pragma once

struct lua_State;

namespace Actors::Helpers
{
	void Register( lua_State* L );
	void Shutdown( );
}

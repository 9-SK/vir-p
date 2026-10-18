#pragma once

struct lua_State;

namespace WebSocket::Helpers
{
	int Connected( lua_State* L, int Status );
	int Connect( lua_State* L );
	void Shutdown( );
}

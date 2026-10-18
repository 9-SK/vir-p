#include "Websocket.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include <lua.h>

namespace WebSocket
{
	void Initialize( )
	{
		lua_State* L = g_Alpen->State( );
		lua_newtable( L );
		lua_pushcclosurek( L, Helpers::Connect, "WebSocket.connect", 0, Helpers::Connected );
		lua_setfield( L, -2, "connect" );
		lua_setreadonly( L, -1, true );
		lua_setglobal( L, "WebSocket" );
	}

	void Shutdown( )
	{
		Helpers::Shutdown( );
	}
}

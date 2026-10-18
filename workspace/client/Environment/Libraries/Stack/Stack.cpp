#include "Stack.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <lualib.h>

namespace Stack
{
	void Initialize( )
	{
		Env::RegisterGlobal( "sethiddenstack", Helpers::SetHidden );
		Env::RegisterGlobal( "setstackhidden", Helpers::SetHidden );
		Env::RegisterGlobal( "ishiddenstack", Helpers::IsHidden );
		Env::RegisterGlobal( "getfenv", Helpers::GetEnvironment );
		Env::RegisterGlobal( "error", Helpers::Error );
		lua_State* L = g_Alpen->State( );
		lua_getglobal( L, "debug" );
		lua_pushcfunction( L, Helpers::Info, "debug.info" );
		lua_setfield( L, -2, "info" );
		lua_pushcfunction( L, Helpers::Traceback, "debug.traceback" );
		lua_setfield( L, -2, "traceback" );
		lua_pop( L, 1 );
	}
}

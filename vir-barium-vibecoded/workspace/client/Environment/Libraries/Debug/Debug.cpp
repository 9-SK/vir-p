#include "Debug.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <lualib.h>

namespace Debug
{
	void Initialize( )
	{
		lua_State* L = g_Alpen->State( );
		lua_getglobal( L, "debug" );
		if ( lua_istable( L, -1 ) )
		{
			lua_clonetable( L, -1 );
			lua_remove( L, -2 );
			lua_setreadonly( L, -1, false );
		}
		else
		{
			lua_pop( L, 1 );
			lua_newtable( L );
		}
		const luaL_Reg Functions[] = {
			{ "getconstant", Helpers::GetConstant },
			{ "getinfo", Helpers::GetInfo },
			{ "getconstants", Helpers::GetConstants },
			{ "getproto", Helpers::GetProto },
			{ "getprotos", Helpers::GetProtos },
			{ "getstack", Helpers::GetStack },
			{ "getupvalue", Helpers::GetUpvalue },
			{ "getupvalues", Helpers::GetUpvalues },
			{ "setconstant", Helpers::SetConstant },
			{ "setstack", Helpers::SetStack },
			{ "setupvalue", Helpers::SetUpvalue },
			{ nullptr, nullptr } };
		for ( const auto& Function : Functions )
		{
			if ( !Function.name )
				break;
			lua_pushcfunction( L, Function.func, Function.name );
			lua_setfield( L, -2, Function.name );
		}
		lua_setglobal( L, "debug" );
	}
}

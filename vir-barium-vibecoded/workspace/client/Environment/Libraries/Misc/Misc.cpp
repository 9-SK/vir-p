#include "Misc.h"
#include "Helpers/Helpers.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <lualib.h>

namespace Misc
{
	namespace
	{
		int request( lua_State* L )
		{
			return Helpers::PushRequest( L );
		}

		int identifyexecutor( lua_State* L )
		{
			lua_pushstring( L, "Alpen" );
			lua_pushstring( L, "1.0.0" );
			return 2;
		}

		int getexecutorname( lua_State* L )
		{
			lua_pushstring( L, "Alpen" );
			return 1;
		}

		int GetObjects( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TUSERDATA );
			luaL_checktype( L, 2, LUA_TSTRING );

			lua_getglobal( L, "game" );
			lua_getfield( L, -1, "GetService" );
			lua_pushvalue( L, -2 );
			lua_pushstring( L, "InsertService" );
			lua_call( L, 2, 1 );
			lua_remove( L, -2 );

			lua_getfield( L, -1, "LoadLocalAsset" );

			lua_pushvalue( L, -2 );
			lua_pushvalue( L, 2 );
			lua_pcall( L, 2, 1, 0 );

			if ( lua_type( L, -1 ) == LUA_TSTRING )
				luaL_error( L, lua_tostring( L, -1 ) );

			lua_createtable( L, 1, 0 );
			lua_pushvalue( L, -2 );
			lua_rawseti( L, -2, 1 );

			lua_remove( L, -3 );
			lua_remove( L, -2 );

			return 1;
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "request", request );
		Env::RegisterGlobal( "http_request", request );
		Env::RegisterGlobal( "identifyexecutor", identifyexecutor );
		Env::RegisterGlobal( "getexecutorname", getexecutorname );

		Env::RegisterMethod( "GetObjects", GetObjects );
	}
}

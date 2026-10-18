#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "Metatable.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <lualib.h>

namespace Metatable
{
	namespace
	{
		int GetNamecallMethod( lua_State* L )
		{
			return Oth::Helpers::Namecall( L );
		}

		int GetRawMetatable( lua_State* L )
		{
			luaL_checkany( L, 1 );
			if ( !lua_getmetatable( L, 1 ) )
				lua_pushnil( L );
			return 1;
		}

		int IsReadonly( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TTABLE );
			lua_pushboolean( L, lua_getreadonly( L, 1 ) );
			return 1;
		}

		int SetRawMetatable( lua_State* L )
		{
			luaL_checkany( L, 1 );
			luaL_checkany( L, 2 );
			if ( !lua_isnil( L, 2 ) )
				luaL_checktype( L, 2, LUA_TTABLE );
			lua_settop( L, 2 );
			const bool Readonly = lua_istable( L, 1 ) && lua_getreadonly( L, 1 );
			if ( Readonly )
				lua_setreadonly( L, 1, false );
			lua_setmetatable( L, 1 );
			if ( Readonly )
				lua_setreadonly( L, 1, true );
			return 1;
		}

		int SetReadonly( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TTABLE );
			luaL_checktype( L, 2, LUA_TBOOLEAN );
			if ( lua_rawequal( L, 1, LUA_REGISTRYINDEX ) )
				luaL_argerror( L, 1, "cannot change registry protection" );
			lua_setreadonly( L, 1, lua_toboolean( L, 2 ) );
			lua_setsafeenv( L, 1, false );
			return 0;
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "getnamecallmethod", GetNamecallMethod );
		Env::RegisterGlobal( "setnamecallmethod", Oth::Helpers::SetNamecall );
		Env::RegisterGlobal( "getrawmetatable", GetRawMetatable );
		Env::RegisterGlobal( "isreadonly", IsReadonly );
		Env::RegisterGlobal( "setrawmetatable", SetRawMetatable );
		Env::RegisterGlobal( "setreadonly", SetReadonly );
	}
}

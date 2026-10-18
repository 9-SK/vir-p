#include "Closures.h"
#include "Helpers/Helpers.h"
#include "client/Environment/Env.h"
#include <lua.h>
#include <lualib.h>

namespace Closures
{
	namespace
	{
		int checkcaller( lua_State* L )
		{
			lua_pushboolean( L, Helpers::IsCaller( L ) );
			return 1;
		}

		int clonefunction( lua_State* L )
		{
			return Helpers::PushClone( L, 1 );
		}

		int getfunctionhash( lua_State* L )
		{
			return Helpers::PushHash( L );
		}

		int hookfunction( lua_State* L )
		{
			return Helpers::PushHook( L, 1, 2 );
		}

		int hookmetamethod( lua_State* L )
		{
			luaL_checkany( L, 1 );
			luaL_checktype( L, 2, LUA_TSTRING );
			Helpers::CheckFunction( L, 3 );
			lua_rawcheckstack( L, 4 );

			if ( !lua_getmetatable( L, 1 ) )
				luaL_argerror( L, 1, "object has no metatable" );

			lua_pushvalue( L, 2 );
			lua_rawget( L, -2 );

			if ( !lua_isfunction( L, -1 ) )
				luaL_argerror( L, 2, "metamethod is not a function" );

			return Helpers::PushHook( L, -1, 3 );
		}

		int iscclosure( lua_State* L )
		{
			Helpers::CheckFunction( L, 1 );
			lua_pushboolean( L, lua_iscfunction( L, 1 ) );
			return 1;
		}

		int isexecutorclosure( lua_State* L )
		{
			lua_pushboolean( L, Helpers::IsExecutor( Helpers::CheckFunction( L, 1 ) ) );
			return 1;
		}

		int islclosure( lua_State* L )
		{
			Helpers::CheckFunction( L, 1 );
			lua_pushboolean( L, lua_isLfunction( L, 1 ) );
			return 1;
		}

		int newcclosure( lua_State* L )
		{
			return Helpers::PushWrapper( L, 1 );
		}

		int restorefunction( lua_State* L )
		{
			return Helpers::Restore( L, 1 );
		}

		int loadstring( lua_State* L )
		{
			return Helpers::PushLoadstring( L );
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "checkcaller", checkcaller );
		Env::RegisterGlobal( "clonefunction", clonefunction );
		Env::RegisterGlobal( "getfunctionhash", getfunctionhash );
		Env::RegisterGlobal( "hookfunction", hookfunction );
		Env::RegisterGlobal( "hookmetamethod", hookmetamethod );
		Env::RegisterGlobal( "iscclosure", iscclosure );
		Env::RegisterGlobal( "isexecutorclosure", isexecutorclosure );
		Env::RegisterGlobal( "islclosure", islclosure );
		Env::RegisterGlobal( "newcclosure", newcclosure );
		Env::RegisterGlobal( "restorefunction", restorefunction );
		Env::RegisterGlobal( "isfunctionhooked", Helpers::IsHooked );
		Env::RegisterGlobal( "checkclosure", isexecutorclosure );
		Env::RegisterGlobal( "loadstring", loadstring );
	}
}

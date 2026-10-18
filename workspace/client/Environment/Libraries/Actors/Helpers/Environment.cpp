#include "Environment.h"
#include "Helper.h"
#include "Native.h"
#include <lua.h>
#include <lualib.h>
#include <stdexcept>
#include <string>

namespace Actors::Helpers
{
	namespace
	{
		char g_Environment;

		int GetEnvironment( lua_State* L )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Environment );
			return 1;
		}

		int GetRobloxEnvironment( lua_State* L )
		{
			lua_xpush( lua_mainthread( L ), L, LUA_GLOBALSINDEX );
			return 1;
		}

		int LoadString( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TSTRING );
			if ( !lua_isnoneornil( L, 2 ) )
				luaL_checktype( L, 2, LUA_TSTRING );
			try
			{
				size_t Length = 0;
				const char* Source = lua_tolstring( L, 1, &Length );
				Native::Load( L, Native::Compile( std::string( Source, Length ) ) );
				return 1;
			}
			catch ( const std::exception& Error )
			{
				lua_pushnil( L );
				lua_pushstring( L, Error.what( ) );
			}
			return 2;
		}
	}

	void PrepareEnvironment( lua_State* L )
	{
		lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Environment );
		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_xpush( lua_mainthread( L ), L, LUA_GLOBALSINDEX );
			lua_replace( L, LUA_GLOBALSINDEX );
			luaL_sandboxthread( L );
			lua_setsafeenv( L, LUA_GLOBALSINDEX, false );
			Register( L );
			const luaL_Reg Functions[] =
				{
					{ "getgenv", GetEnvironment },
					{ "getrenv", GetRobloxEnvironment },
					{ "loadstring", LoadString },
					{ nullptr, nullptr } };
			for ( const auto* Entry = Functions; Entry->name; ++Entry )
			{
				lua_pushcfunction( L, Entry->func, Entry->name );
				lua_setglobal( L, Entry->name );
			}
			lua_pushvalue( L, LUA_GLOBALSINDEX );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Environment );
		}

		lua_replace( L, LUA_GLOBALSINDEX );
		luaL_sandboxthread( L );
		lua_setsafeenv( L, LUA_GLOBALSINDEX, false );
	}
}

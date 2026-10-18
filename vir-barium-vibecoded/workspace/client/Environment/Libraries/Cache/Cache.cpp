#include "Cache.h"
#include "client/Environment/Env.h"
#include "client/Environment/Libraries/Instances/Helpers/Helpers.h"
#include "core/Roblox/Update.h"
#include <lua.h>
#include <lualib.h>

namespace Cache
{
	namespace
	{
		void Push( lua_State* L )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, reinterpret_cast<void*>( Update::Roblox::Instance::Cache ) );
			if ( !lua_istable( L, -1 ) )
				luaL_error( L, "Instance cache is unavailable" );
		}

		int Invalidate( lua_State* L )
		{
			auto* Instance = Instances::Helpers::CheckInstance( L, 1 ).get( );
			Push( L );
			lua_pushnil( L );
			lua_rawsetp( L, -2, Instance );
			return 0;
		}

		int IsCached( lua_State* L )
		{
			auto* Instance = Instances::Helpers::CheckInstance( L, 1 ).get( );
			Push( L );
			lua_rawgetp( L, -1, Instance );
			lua_pushboolean( L, !lua_isnil( L, -1 ) );
			return 1;
		}

		int Replace( lua_State* L )
		{
			auto* Instance = Instances::Helpers::CheckInstance( L, 1 ).get( );
			Instances::Helpers::CheckInstance( L, 2 );
			Push( L );
			lua_pushvalue( L, 2 );
			lua_rawsetp( L, -2, Instance );
			return 0;
		}
	}

	void Initialize( )
	{
		lua_State* L = g_Alpen->State( );
		const luaL_Reg Functions[] =
			{
				{ "invalidate", Invalidate },
				{ "iscached", IsCached },
				{ "replace", Replace },
				{ nullptr, nullptr } };
		lua_newtable( L );
		luaL_register( L, nullptr, Functions );
		lua_setglobal( L, "cache" );
	}
}

#include "Instances.h"
#include "Helpers/Helpers.h"
#include "client/Environment/Env.h"
#include <lua.h>

namespace Instances
{
	namespace
	{
		int getinstances( lua_State* L )
		{
			return Helpers::PushInstances( L );
		}

		int getnilinstances( lua_State* L )
		{
			return Helpers::PushInstances( L, true );
		}

		int gethui( lua_State* L )
		{
			return Helpers::PushHiddenUI( L );
		}

		int cloneref( lua_State* L )
		{
			return Helpers::PushClone( L, 1 );
		}

		int compareinstances( lua_State* L )
		{
			const auto& First = Helpers::CheckInstance( L, 1 );
			const auto& Second = Helpers::CheckInstance( L, 2 );
			lua_pushboolean( L, First.get( ) == Second.get( ) );
			return 1;
		}

		int fireclickdetector( lua_State* L )
		{
			return Helpers::FireClick( L );
		}

		int fireproximityprompt( lua_State* L )
		{
			return Helpers::FirePrompt( L );
		}

		int firetouchinterest( lua_State* L )
		{
			return Helpers::FireTouch( L );
		}

		int getcallbackvalue( lua_State* L )
		{
			return Helpers::PushCallback( L );
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "getinstances", getinstances );
		Env::RegisterGlobal( "getnilinstances", getnilinstances );
		Env::RegisterGlobal( "gethui", gethui );
		Env::RegisterGlobal( "cloneref", cloneref );
		Env::RegisterGlobal( "compareinstances", compareinstances );
		Env::RegisterGlobal( "fireclickdetector", fireclickdetector );
		Env::RegisterGlobal( "fireproximityprompt", fireproximityprompt );
		Env::RegisterGlobal( "firetouchinterest", firetouchinterest );
		Env::RegisterGlobal( "getcallbackvalue", getcallbackvalue );
	}
}

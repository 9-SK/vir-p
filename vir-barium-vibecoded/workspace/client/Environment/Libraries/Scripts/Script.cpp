#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "Script.h"
#include "Helpers/Helpers.h"
#include "client/Environment/Env.h"
#include "core/Roblox/Update.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <cstring>
#include <lua.h>
#include <lualib.h>

namespace Scripts
{
	namespace
	{
		int getscriptclosure( lua_State* L )
		{
			return Helpers::PushClosure( L );
		}

		int getscriptbytecode( lua_State* L )
		{
			return Helpers::PushBytecode( L );
		}

		int getscripthash( lua_State* L )
		{
			return Helpers::PushHash( L );
		}

		int getscripts( lua_State* L )
		{
			return Helpers::PushInstances( L );
		}

		int getrunningscripts( lua_State* L )
		{
			return Helpers::PushRunningScripts( L );
		}

		int getloadedmodules( lua_State* L )
		{
			return Helpers::PushLoadedModules( L );
		}

		int getcallingscript( lua_State* L )
		{
			return Helpers::PushCallingScript( L, Oth::Helpers::Original( L ) );
		}

		int getscriptfromthread( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TTHREAD );
			Helpers::PushScript( L, lua_tothread( L, 1 ) );
			return 1;
		}

		int getsenv( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TUSERDATA );

			if ( std::strcmp( luaL_typename( L, 1 ), "Instance" ) != 0 )
				luaL_argerror( L, 1, "expected a Script, LocalScript or ModuleScript" );

			lua_getfield( L, 1, "ClassName" );
			const char* ClassName = lua_tostring( L, -1 );

			if ( !Helpers::IsScript( ClassName ) )
				luaL_argerror( L, 1, "expected a Script, LocalScript or ModuleScript" );

			lua_pop( L, 1 );

			Udata* Userdata = uvalue( luaA_toobject( L, 1 ) );

			if ( Userdata->len < sizeof( uintptr_t ) )
				luaL_argerror( L, 1, "invalid script instance" );

			uintptr_t Instance = *static_cast<uintptr_t*>( lua_touserdata( L, 1 ) );

			if ( !Instance )
				luaL_argerror( L, 1, "expected a script instance" );

			lua_rawcheckstack( L, 1 );

			LuaTable* Environment = Helpers::FindEnvironment( L, Instance );

			if ( !Environment )
			{
				lua_State* RobloxState = g_Roblox ? g_Roblox->State( ) : nullptr;

				if ( RobloxState && RobloxState->global != L->global && Helpers::FindEnvironment( RobloxState, Instance ) )
				{
					lua_pushnil( L );
					return 1;
				}

				luaL_error( L, "script is not running" );
			}

			luaC_threadbarrier( L );
			sethvalue( L, L->top, Environment );
			L->top++;

			return 1;
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "getcallingscript", getcallingscript );
		Env::RegisterGlobal( "getloadedmodules", getloadedmodules );
		Env::RegisterGlobal( "getscriptbytecode", getscriptbytecode );
		Env::RegisterGlobal( "getscriptclosure", getscriptclosure );
		Env::RegisterGlobal( "getsenv", getsenv );
		Env::RegisterGlobal( "getscriptfromthread", getscriptfromthread );
		Env::RegisterGlobal( "getscripts", getscripts );
		Env::RegisterGlobal( "getscripthash", getscripthash );
	}
}

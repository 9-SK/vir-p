#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "Environment.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include "client/Environment/Libraries/Closures/Helpers/Helpers.h"
#include "client/Environment/Libraries/Stack/Helpers/Helper.h"
#include "core/Globals.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lobject.h>
#include <VM/src/ltable.h>
#include <lua.h>
#include <lualib.h>
#include <cmath>
#include <limits>

namespace Libraries::Environment
{
	namespace
	{
		LuaTable* g_RenvCache = nullptr;
		lua_State* g_RenvCacheState = nullptr;

		int getfenv( lua_State* L )
		{
			if ( lua_isfunction( L, 1 ) )
			{
				Closure* Function = clvalue( luaA_toobject( L, 1 ) );
				if ( Function->isC && Function->env == g_Env->ExecutorState->gt && Closures::Helpers::IsExecutor( Function ) )
					lua_pushvalue( L, LUA_GLOBALSINDEX );
				else
					lua_getfenv( L, 1 );
			}
			else
			{
				double Level = luaL_optnumber( L, 1, 1 );
				if ( !std::isfinite( Level ) || Level < 0 || Level > ( std::numeric_limits<int>::max )( ) || Level != std::floor( Level ) )
					luaL_argerror( L, 1, "invalid level" );
				if ( Level == 0 )
					lua_pushvalue( L, LUA_GLOBALSINDEX );
				else
				{
					lua_Debug Info{ };
					lua_State* Thread = L;
					CallInfo* Frame = Oth::Helpers::Frame( Thread, static_cast<int>( Level ) );
					if ( !Frame || !lua_getinfo( Thread, static_cast<int>( Thread->ci - Frame ), "f", &Info ) )
						luaL_argerror( L, 1, "invalid level" );
					if ( Thread != L )
						lua_xmove( Thread, L, 1 );
					lua_getfenv( L, -1 );
				}
			}
			lua_setsafeenv( L, -1, false );
			return 1;
		}

		int getgc( lua_State* L )
		{
			return Helpers::PushGC( L );
		}

		int filtergc( lua_State* L )
		{
			return Helpers::PushFiltered( L );
		}

		int getreg( lua_State* L )
		{
			lua_pushvalue( L, LUA_REGISTRYINDEX );
			return 1;
		}

		int getgenv( lua_State* L )
		{
			lua_State* ExecutorState = g_Env->ExecutorState;

			if ( !ExecutorState || ExecutorState->global != L->global )
			{
				lua_pushnil( L );
				return 1;
			}

			if ( ExecutorState == L )
			{
				lua_pushvalue( L, LUA_GLOBALSINDEX );
				return 1;
			}

			lua_rawcheckstack( L, 1 );
			luaC_threadbarrier( L );
			luaC_threadbarrier( ExecutorState );
			lua_pushvalue( ExecutorState, LUA_GLOBALSINDEX );
			lua_xmove( ExecutorState, L, 1 );

			return 1;
		}

		int getrenv( lua_State* L )
		{
			lua_State* RobloxState = L->global->mainthread;

			if ( g_RenvCacheState != RobloxState )
			{
				g_RenvCache = nullptr;
				g_RenvCacheState = RobloxState;
			}

			if ( !g_RenvCache )
			{
				LuaTable* Clone = luaH_clone( L, RobloxState->gt );

				lua_rawcheckstack( L, 2 );
				luaC_threadbarrier( L );
				luaC_threadbarrier( RobloxState );

				L->top->value.p = Clone;
				L->top->tt = LUA_TTABLE;
				L->top++;

				lua_pushvalue( L, -1 );
				lua_ref( L, -1 );
				lua_pop( L, 1 );

				lua_rawgeti( L, LUA_REGISTRYINDEX, 2 );
				lua_setfield( L, -2, "_G" );

				lua_rawgeti( L, LUA_REGISTRYINDEX, 4 );
				lua_setfield( L, -2, "shared" );

				g_RenvCache = Clone;
				lua_pop( L, 1 );
			}

			lua_rawcheckstack( L, 1 );
			luaC_threadbarrier( L );

			L->top->value.p = g_RenvCache;
			L->top->tt = LUA_TTABLE;
			L->top++;

			return 1;
		}

	}

	void Initialize( )
	{
		Env::RegisterGlobal( "getfenv", getfenv );
		Env::RegisterGlobal( "getgc", getgc );
		Env::RegisterGlobal( "filtergc", filtergc );
		Env::RegisterGlobal( "getreg", getreg );
		Env::RegisterGlobal( "getgenv", getgenv );
		Env::RegisterGlobal( "getrenv", getrenv );
	}
}

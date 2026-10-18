#include "core/Execution/Execution.h"
#include "core/Globals.h"
#include "core/Scheduler/Helper.h"
#include "Helpers.h"
#include <VM/src/lapi.h>
#include <VM/src/lobject.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <lua.h>
#include <lualib.h>
#include <string>

namespace Closures::Helpers
{
	int LoadBytecode( lua_State* L, const std::string& ByteCode, const std::string& ChunkName, const std::string& Error )
	{
		if ( !Error.empty( ) || ByteCode.empty( ) )
		{
			lua_pushnil( L );
			lua_pushstring( L, Error.empty( ) ? "compiler returned no bytecode" : Error.c_str( ) );
			return 2;
		}

		lua_setsafeenv( L, LUA_GLOBALSINDEX, false );
		int Status = luau_load( L, ChunkName.c_str( ), ByteCode.data( ), ByteCode.size( ), 0 );

		if ( Status != LUA_OK )
		{
			lua_pushnil( L );
			lua_insert( L, -2 );
			return 2;
		}

		if ( Closure* Cl = clvalue( luaA_toobject( L, -1 ) ); !Cl->isC && Cl->l.p )
			ProtoCapsEqual( Cl->l.p, &g_Env->MaxAvailableInstanceCapabilities );

		return 1;
	}

	int PushLoadstring( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TSTRING );
		if ( !lua_isnoneornil( L, 2 ) )
			luaL_checktype( L, 2, LUA_TSTRING );

		lua_rawcheckstack( L, 2 );
		lua_setsafeenv( L, LUA_GLOBALSINDEX, false );
		size_t Length = 0;
		const char* Source = lua_tolstring( L, 1, &Length );
		char Buffer[ 32 ];
		snprintf( Buffer, sizeof( Buffer ), "=%08X", static_cast<unsigned>( rand( ) ) );
		std::string ChunkName = lua_isnoneornil( L, 2 ) ? Buffer : lua_tostring( L, 2 );

		auto Compile = [ Source = std::string( Source, Length ), ChunkName = std::move( ChunkName ) ]( ) -> Yielded
		{
			std::string ByteCode;
			std::string Error;
			try
			{
				ByteCode = Alpen::Execution::Compile( Source );
			}
			catch ( const std::exception& Exception )
			{
				Error = Exception.what( );
			}

			return [ ByteCode = std::move( ByteCode ), ChunkName, Error = std::move( Error ) ]( lua_State* State )
			{
				return LoadBytecode( State, ByteCode, ChunkName, Error );
			};
		};

		if ( Length >= 32768 && lua_isyieldable( L ) )
			return Yielding::YieldExec( L, Compile );

		return Compile( )( L );
	}

}

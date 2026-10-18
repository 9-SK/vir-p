#include "Raknet.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include "core/Network/Observer.h"
#include "core/Network/Transport.h"
#include "utils/Packets/Logger.h"
#include <lualib.h>

namespace Raknet
{
	namespace
	{
		int Start( lua_State* L )
		{
			std::string Error;
			bool Started = Network::Transport::UseRaknet( Error );
			if ( Started )
			{
				Started = Network::Observer::Start( );
				if ( !Started )
				{
					Error = Network::Observer::Error( );
				}
			}
			lua_pushboolean( L, Started );
			if ( Started )
				return 1;
			lua_pushlstring( L, Error.data( ), Error.size( ) );
			return 2;
		}

		int Stop( lua_State* )
		{
			Network::Observer::Stop( );
			return 0;
		}

		int Inspect( lua_State* L )
		{
			const auto Status = Packets::g_Logger->Inspect( );
			lua_createtable( L, 0, 12 );
			lua_pushboolean( L, Status.Enabled );
			lua_setfield( L, -2, "enabled" );
			lua_pushboolean( L, Network::Observer::Installed( ) );
			lua_setfield( L, -2, "installed" );
			lua_pushnumber( L, double( Status.Generation ) );
			lua_setfield( L, -2, "generation" );
			lua_pushnumber( L, double( Status.Captured ) );
			lua_setfield( L, -2, "captured" );
			lua_pushnumber( L, double( Status.Read ) );
			lua_setfield( L, -2, "read" );
			lua_pushnumber( L, double( Status.Dropped ) );
			lua_setfield( L, -2, "dropped" );
			lua_pushnumber( L, double( Status.Rejected ) );
			lua_setfield( L, -2, "rejected" );
			lua_pushnumber( L, double( Status.Pending ) );
			lua_setfield( L, -2, "pending" );
			const auto Error = Network::Observer::Error( );
			lua_pushstring( L, Error.c_str( ) );
			lua_setfield( L, -2, "error" );
			const int Flag = Network::Transport::ReadFlag( );
			if ( Flag < 0 )
				lua_pushnil( L );
			else
				lua_pushboolean( L, Flag != 0 );
			lua_setfield( L, -2, "use_rbxtransport" );
			lua_pushstring( L, "unknown" );
			lua_setfield( L, -2, "server_status" );
			return 1;
		}
	}

	void Bootstrap( )
	{
		try
		{
			std::string Error;
			if ( !Network::Transport::UseRaknet( Error ) || !Network::Observer::Start( ) )
			{
				if ( Error.empty( ) )
					Error = Network::Observer::Error( );
				OutputDebugStringA( ( "[Alpen Packets] " + Error + "\n" ).c_str( ) );
			}
		}
		catch ( const std::exception& Error )
		{
			OutputDebugStringA( Error.what( ) );
		}
	}

	void Initialize( )
	{
		lua_State* L = g_Alpen->State( );
		const luaL_Reg Functions[] =
			{
				{ "start", Start },
				{ "stop", Stop },
				{ "status", Inspect },
				{ "inspect", Helpers::Inspect },
				{ "read", Helpers::Read },
				{ nullptr, nullptr } };
		lua_newtable( L );
		luaL_register( L, nullptr, Functions );
		lua_setglobal( L, "raknet" );
		Bootstrap( );
	}

	void Shutdown( )
	{
		Network::Observer::Stop( );
	}
}

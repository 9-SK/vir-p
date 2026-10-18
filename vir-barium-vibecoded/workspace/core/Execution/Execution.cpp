#include "Execution.h"
#include "client/Environment/Env.h"
#include "core/Roblox/Update.h"
#include "core/Scheduler/Helper.h"
#include <VM/src/lapi.h>
#include <VM/src/lobject.h>
#include <cstdio>
#include <cstdlib>
#include <lualib.h>
#include <string>

namespace Alpen
{
	namespace Execution
	{
		std::string
		Compile( std::string Source )
		{
			auto BytecodeEncoding = BytecodeEncoder( );
			static const char* CommonGlobals[] = { "Game", "Workspace", "game", "plugin", "script", "shared", "workspace", "_G", "_ENV", nullptr };

			Luau::CompileOptions Options;
			Options.debugLevel = 2;
			Options.optimizationLevel = 1;
			Options.mutableGlobals = CommonGlobals;
			Options.vectorLib = "Vector3";
			Options.vectorCtor = "new";
			Options.vectorType = "Vector3";

			return Luau::compile( Source, Options, { }, &BytecodeEncoding );
		}

		void RunBytecode( lua_State* L, const std::string& Bytecode )
		{
			if ( !L )
				return;

			const int OriginalTop = lua_gettop( L );
			lua_State* Thread = lua_newthread( L );
			g_Alpen->PushLocal( Thread );
			lua_replace( Thread, LUA_GLOBALSINDEX );
			ThreadCapsEqual( Thread, 8, g_Env->MaxAvailableInstanceCapabilities );
			char Name[ 32 ];
			snprintf( Name, sizeof( Name ), "=%08X", static_cast<unsigned>( rand( ) ) );

			if ( luau_load( Thread, Name, Bytecode.data( ), Bytecode.size( ), 0 ) == LUA_OK )
			{
				Closure* Function = clvalue( luaA_toobject( Thread, -1 ) );
				ProtoCapsEqual( Function->l.p, &g_Env->MaxAvailableInstanceCapabilities );
				lua_xpush( Thread->global->mainthread, Thread, LUA_GLOBALSINDEX );
				lua_getfield( Thread, -1, "task" );
				lua_remove( Thread, -2 );
				lua_getfield( Thread, -1, "defer" );
				lua_remove( Thread, -2 );
				lua_insert( Thread, -2 );

				if ( lua_pcall( Thread, 1, 0, 0 ) == LUA_OK )
				{
					lua_settop( L, OriginalTop );
					return;
				}
			}

			const char* Error = lua_tostring( Thread, -1 );
			Update::Roblox::Functions::Print( 2, "%s", Error ? Error : "failed to execute script" );
			lua_settop( L, OriginalTop );
		}

		void Execute( lua_State* L, std::string Script )
		{
			if ( !L )
				return;

			const uint64_t Generation = g_Env->SessionGeneration.load( );
			try
			{
				Yielding::Submit( [ Script = std::move( Script ), Generation ]
				{
					std::string Bytecode;
					std::string Error;
					try
					{
						Bytecode = Compile( Script );
					}
					catch ( const std::exception& Exception )
					{
						Error = Exception.what( );
					}

					std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );
					if ( Generation != g_Env->SessionGeneration.load( ) )
						return;

					g_Env->YieldQueue.emplace( [ Bytecode = std::move( Bytecode ), Error = std::move( Error ), Generation ]
					{
						if ( Generation != g_Env->SessionGeneration.load( ) )
							return;

						if ( Error.empty( ) )
							RunBytecode( g_Env->ExecutorState, Bytecode );
						else
							Update::Roblox::Functions::Print( 2, "%s", Error.c_str( ) );
					} );
				} );
			}
			catch ( const std::exception& Exception )
			{
				Update::Roblox::Functions::Print( 2, "%s", Exception.what( ) );
			}
		}
	}
}

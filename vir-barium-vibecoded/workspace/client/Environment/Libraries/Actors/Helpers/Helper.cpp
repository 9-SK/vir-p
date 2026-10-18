#include "Helper.h"
#include "Native.h"
#include "Transfer.h"
#include <atomic>
#include <cmath>
#include <lua.h>
#include <lualib.h>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace Actors::Helpers
{
	namespace
	{
		char g_HandleMetatable;
		char g_Handles;
		std::mutex g_ChannelMutex;
		struct Channel
		{
			uint64_t Generation;
			Native::Instance Event;
		};
		std::unordered_map<uint64_t, Channel> g_Channels;
		std::atomic<uint64_t> g_NextChannel{ 1 };
		struct Handle
		{
			uint64_t Generation;
			Native::ThreadRef Thread;
		};

		template <int ( *Callback )( lua_State* )>
		int Protected( lua_State* L )
		{
			try
			{
				return Callback( L );
			}
			catch ( const std::exception& Error )
			{
				lua_pushstring( L, Error.what( ) );
			}
			lua_error( L );
		}

		void CheckSession( lua_State* L )
		{
			if ( static_cast<uint64_t>( lua_tonumber( L, lua_upvalueindex( 1 ) ) ) != Native::Generation( ) )
				throw std::runtime_error( "Actor session has ended" );
		}

		void RequireSerial( lua_State* L )
		{
			CheckSession( L );
			if ( Native::Parallel( L ) )
				throw std::runtime_error( "Actor operation requires a serial scheduler phase" );
		}

		int IsParallel( lua_State* L )
		{
			CheckSession( L );
			lua_pushboolean( L, Native::Parallel( L ) );
			return 1;
		}

		int ListActors( lua_State* L, bool Deleted )
		{
			RequireSerial( L );
			auto Snapshot = Native::Enumerate( L );
			lua_createtable( L, static_cast<int>( Snapshot.Actors.size( ) ), 0 );
			int Count = 0;
			for ( const auto& Actor : Snapshot.Actors )
			{
				if ( Native::Deleted( Actor ) != Deleted )
					continue;
				Native::PushInstance( L, Actor );
				if ( !lua_isnil( L, -1 ) )
					lua_rawseti( L, -2, ++Count );
				else
					lua_pop( L, 1 );
			}
			return 1;
		}

		int GetActors( lua_State* L )
		{
			return ListActors( L, false );
		}

		int GetDeletedActors( lua_State* L )
		{
			return ListActors( L, true );
		}

		int GetActorThreads( lua_State* L )
		{
			RequireSerial( L );
			auto Snapshot = Native::Enumerate( L );
			lua_createtable( L, static_cast<int>( Snapshot.Threads.size( ) ), 0 );
			int Count = 0;
			for ( auto& Thread : Snapshot.Threads )
			{
				void* Storage = lua_newuserdatadtor( L, sizeof( Handle ), []( void* Data )
													 {
														 static_cast<Handle*>( Data )->~Handle( );
													 } );
				new ( Storage ) Handle{ Native::Generation( ), std::move( Thread ) };
				lua_rawgetp( L, LUA_REGISTRYINDEX, &g_HandleMetatable );
				lua_setmetatable( L, -2 );
				lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Handles );
				lua_pushvalue( L, -2 );
				lua_pushboolean( L, true );
				lua_rawset( L, -3 );
				lua_pop( L, 1 );
				lua_rawseti( L, -2, ++Count );
			}
			return 1;
		}

		int CreateChannel( lua_State* L )
		{
			RequireSerial( L );
			lua_getglobal( L, "Instance" );
			lua_getfield( L, -1, "new" );
			lua_remove( L, -2 );
			lua_pushliteral( L, "BindableEvent" );
			lua_call( L, 1, 1 );
			auto Event = Native::CheckInstance( L, -1, "BindableEvent" );
			const uint64_t Id = g_NextChannel.fetch_add( 1 );
			if ( Id >= ( uint64_t( 1 ) << 53 ) )
				throw std::runtime_error( "communication channel identifiers exhausted" );
			{
				std::lock_guard<std::mutex> Lock( g_ChannelMutex );
				g_Channels.emplace( Id, Channel{ Native::Generation( ), std::move( Event ) } );
			}
			lua_pushnumber( L, static_cast<double>( Id ) );
			lua_insert( L, -2 );
			return 2;
		}

		int GetChannel( lua_State* L )
		{
			RequireSerial( L );
			luaL_checktype( L, 1, LUA_TNUMBER );
			const double Number = lua_tonumber( L, 1 );
			if ( !std::isfinite( Number ) || Number < 1 || Number >= 9007199254740992.0 || std::floor( Number ) != Number )
				luaL_argerror( L, 1, "expected an exact positive channel identifier" );
			Native::Instance Event;
			{
				std::lock_guard<std::mutex> Lock( g_ChannelMutex );
				auto Found = g_Channels.find( static_cast<uint64_t>( Number ) );
				if ( Found != g_Channels.end( ) && Found->second.Generation == Native::Generation( ) )
					Event = Found->second.Event;
			}
			if ( !Event || Native::Deleted( Event ) )
				throw std::runtime_error( "communication channel is unknown, destroyed or from an ended session" );
			Native::PushInstance( L, Event );
			return 1;
		}

		int Run( lua_State* L, const Native::ThreadRef& Target )
		{
			luaL_checktype( L, 2, LUA_TSTRING );
			size_t Length = 0;
			const char* Source = lua_tolstring( L, 2, &Length );
			if ( Length > 16 * 1024 * 1024 )
				throw std::runtime_error( "Actor source exceeds 16 MiB" );
			Transfer Arguments( L, 3 );
			const std::string Bytecode = Native::Compile( std::string( Source, Length ) );
			if ( Bytecode.empty( ) || Bytecode[ 0 ] == 0 )
				throw std::runtime_error( Bytecode.size( ) > 1 ? Bytecode.substr( 1 ) : "Actor compilation failed" );
			const auto Locked = Target ? Target->Lock( ) : nullptr;
			if ( !Locked )
				throw std::runtime_error( "Actor thread is no longer available" );
			lua_State* Parent = Locked.get( );
			const int Top = lua_gettop( Parent );
			try
			{
				if ( !lua_checkstack( Parent, 1 ) )
					throw std::runtime_error( "Actor parent stack is full" );
				lua_State* Thread = lua_newthread( Parent );
				Native::Prepare( Thread, Target->Actor );
				Native::Load( Thread, Bytecode );
				Arguments.Push( Thread );
				Native::Enqueue( Thread, Arguments.Count( ) );
			}
			catch ( ... )
			{
				lua_settop( Parent, Top );
				throw;
			}
			lua_settop( Parent, Top );
			return 0;
		}

		int RunOnActor( lua_State* L )
		{
			RequireSerial( L );
			auto Actor = Native::CheckInstance( L, 1, "Actor" );
			auto Snapshot = Native::Enumerate( L );
			for ( const auto& Thread : Snapshot.Threads )
				if ( Thread->Actor == Actor )
					return Run( L, Thread );
			return Run( L, Native::CreateTarget( L, Actor ) );
		}

		int RunOnThread( lua_State* L )
		{
			RequireSerial( L );
			luaL_checktype( L, 1, LUA_TUSERDATA );
			bool Valid = false;
			if ( lua_getmetatable( L, 1 ) )
			{
				lua_rawgetp( L, LUA_REGISTRYINDEX, &g_HandleMetatable );
				Valid = lua_rawequal( L, -1, -2 ) && lua_objlen( L, 1 ) == sizeof( Handle ) + sizeof( void ( * )( lua_State*, void* ) );
				lua_pop( L, 2 );
			}
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Handles );
			lua_pushvalue( L, 1 );
			lua_rawget( L, -2 );
			Valid = Valid && lua_toboolean( L, -1 );
			lua_pop( L, 2 );
			if ( !Valid )
				luaL_argerror( L, 1, "expected a handle returned by getactorthreads" );
			const auto* Value = static_cast<const Handle*>( lua_touserdata( L, 1 ) );
			if ( Value->Generation != Native::Generation( ) )
				throw std::runtime_error( "Actor thread handle belongs to an ended session" );
			return Run( L, Value->Thread );
		}
			/* ngl had no idea how, so just do eng wrp */
		constexpr const char* Wrapper = R"(
local callback, parallel = ...
local synchronize, desynchronize = task.synchronize, task.desynchronize
local pack, unpack, pcall, error = table.pack, table.unpack, pcall, error
return function(...)
    local wasParallel = parallel()
    if wasParallel then synchronize() end
    local result = pack(pcall(callback, ...))
    if wasParallel then desynchronize() end
    if not result[1] then error(result[2], 2) end
    return unpack(result, 2, result.n)
end
)";

		void PushCallback( lua_State* L, const luaL_Reg& Entry )
		{
			lua_pushnumber( L, static_cast<double>( Native::Generation( ) ) );
			lua_pushcclosure( L, Entry.func, Entry.name, 1 );
		}
	}

	void Register( lua_State* L )
	{
		lua_rawcheckstack( L, 8 );
		lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Handles );
		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_newtable( L );
			lua_newtable( L );
			lua_pushliteral( L, "k" );
			lua_setfield( L, -2, "__mode" );
			lua_setmetatable( L, -2 );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Handles );
		}
		lua_pop( L, 1 );
		lua_rawgetp( L, LUA_REGISTRYINDEX, &g_HandleMetatable );
		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_newtable( L );
			lua_pushliteral( L, "ActorThread" );
			lua_setfield( L, -2, "__type" );
			lua_pushliteral( L, "ActorThread" );
			lua_setfield( L, -2, "__metatable" );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, &g_HandleMetatable );
		}
		lua_pop( L, 1 );
		const luaL_Reg Parallel{ "is_parallel", Protected<IsParallel> };
		PushCallback( L, Parallel );
		lua_setglobal( L, Parallel.name );
		const luaL_Reg Functions[] =
			{
				{ "create_comm_channel", Protected<CreateChannel> },
				{ "get_comm_channel", Protected<GetChannel> },
				{ "getactors", Protected<GetActors> },
				{ "getdeletedactors", Protected<GetDeletedActors> },
				{ "getactorthreads", Protected<GetActorThreads> },
				{ "run_on_actor", Protected<RunOnActor> },
				{ "run_on_thread", Protected<RunOnThread> } };
		const std::string Bytecode = Native::Compile( Wrapper );
		for ( const auto& Entry : Functions )
		{
			Native::Load( L, Bytecode );
			PushCallback( L, Entry );
			PushCallback( L, Parallel );
			lua_call( L, 2, 1 );
			lua_setglobal( L, Entry.name );
		}
		for ( const auto& Alias : { std::pair{ "get_actors", "getactors" }, std::pair{ "get_actor_threads", "getactorthreads" }, std::pair{ "get_deleted_actors", "getdeletedactors" } } )
		{
			lua_getglobal( L, Alias.second );
			lua_setglobal( L, Alias.first );
		}
	}

	void Shutdown( )
	{
		std::unordered_map<uint64_t, Channel> Retired;
		{
			std::lock_guard<std::mutex> Lock( g_ChannelMutex );
			Retired.swap( g_Channels );
		}
	}
}

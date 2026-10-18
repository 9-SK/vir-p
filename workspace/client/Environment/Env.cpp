#include "Env.h"
#include "utils/BootLog.hpp"
#include "Libraries/Actors/Actors.h"
#include "Libraries/Oth/Oth.h"
#include "Libraries/Stack/Stack.h"
#include "Libraries/Cache/Cache.h"
#include "Libraries/Drawing/Drawing.h"
#include "Libraries/Raknet/Raknet.h"
#include "client/Environment/Libraries/Closures/Helpers/Helpers.h"
#include "client/Environment/Libraries/Closures/Closures.h"
#include "client/Environment/Libraries/Debug/Debug.h"
#include "client/Environment/Libraries/Encoding/Encoding.h"
#include "client/Environment/Libraries/Environment/Environment.h"
#include "client/Environment/Libraries/Filesystem/Filesystem.h"
#include "client/Environment/Libraries/Http/Http.h"
#include "client/Environment/Libraries/Instances/Instances.h"
#include "client/Environment/Libraries/Metatable/Metatable.h"
#include "client/Environment/Libraries/Misc/Misc.h"
#include "client/Environment/Libraries/Reflection/Helpers/Helper.h"
#include "client/Environment/Libraries/Reflection/Reflection.h"
#include "client/Environment/Libraries/Scripts/Script.h"
#include "client/Environment/Libraries/Signals/Signals.h"
#include "client/Environment/Libraries/WebSocket/Websocket.h"
#include "core/Globals.h"
#include "core/Scheduler/Helper.h"
#include <VM/src/lapi.h>
#include <VM/src/ldo.h>
#include <VM/src/lgc.h>
#include <lualib.h>

EnvironmentRef g_Alpen;
EnvironmentRef g_Roblox;

Environment::Environment( lua_State* State )
{
	Create( State );
}

Environment::~Environment( )
{
	Destroy( );
}

bool Environment::Create( lua_State* State )
{
	if ( !State )
		return false;

	m_State = State;

	return true;
}

void Environment::Destroy( )
{
	m_State = nullptr;
}

bool Environment::Valid( ) const
{
	return m_State != nullptr;
}

lua_State* Environment::State( ) const
{
	return m_State;
}

void Environment::Push( lua_State* Target ) const
{
	if ( !Target )
		return;

	if ( !m_State || m_State->global != Target->global )
	{
		lua_pushnil( Target );
		return;
	}

	lua_xpush( m_State, Target, LUA_GLOBALSINDEX );
	lua_setsafeenv( Target, -1, false );
}

void Environment::PushLocal( lua_State* Target ) const
{
	if ( !Target )
		return;

	Push( Target );

	if ( !lua_istable( Target, -1 ) )
		luaL_error( Target, "executor environment is unavailable" );

	lua_newtable( Target );
	lua_newtable( Target );
	lua_pushvalue( Target, -3 );
	lua_setfield( Target, -2, "__index" );
	lua_setreadonly( Target, -1, true );
	lua_setmetatable( Target, -2 );
	lua_setsafeenv( Target, -1, false );
	lua_remove( Target, -2 );
}

void Environment::Register( const char* Name, lua_CFunction Function )
{
	if ( !m_State || !Name || !Function )
		return;

	lua_pushcclosure( m_State, Function, nullptr, 0 );
	lua_setglobal( m_State, Name );
}

namespace
{
	char g_Methods;
	char g_Session;

	struct Session
	{
		uint64_t Generation;
		void* Global;
	};

	void TrackSession( lua_State* L )
	{
		void* Storage = lua_newuserdatadtor( L, sizeof( Session ), []( void* Data )
											 {
												 const auto* Owner = static_cast<const Session*>( Data );
												 if ( Owner->Generation == g_Env->SessionGeneration.load( ) && Owner->Global == g_Env->RobloxGlobal )
													 Env::Shutdown( );
											 } );
		new ( Storage ) Session{ g_Env->SessionGeneration.load( ), L->global };
		lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Session );
	}

	enum MethodSlot
	{
		Receiver = 1,
		OriginalIndex,
		OriginalNamecall,
		OriginalNewIndex
	};

	void PushMethods( lua_State* L )
	{
		lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Methods );

		if ( lua_istable( L, -1 ) )
			return;

		lua_pop( L, 1 );
		lua_newtable( L );
		lua_pushvalue( L, -1 );
		lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Methods );
	}

	bool CanIntercept( lua_State* L )
	{
		if ( !Closures::Helpers::IsCaller( L ) || lua_type( L, 1 ) != LUA_TUSERDATA )
			return false;

		PushMethods( L );
		lua_rawgeti( L, -1, Receiver );
		bool Matches = lua_rawequal( L, 1, -1 );

		if ( !Matches && lua_type( L, -1 ) == LUA_TUSERDATA )
		{
			Udata* Value = uvalue( luaA_toobject( L, 1 ) );
			Udata* Reference = uvalue( luaA_toobject( L, -1 ) );

			if ( Value->tag == Reference->tag && Value->metatable == Reference->metatable && Value->len == sizeof( std::shared_ptr<uintptr_t> ) && Reference->len == Value->len )
			{
				const auto& Instance = *reinterpret_cast<const std::shared_ptr<uintptr_t>*>( Value->data );
				const auto& DataModel = *reinterpret_cast<const std::shared_ptr<uintptr_t>*>( Reference->data );
				Matches = Instance && Instance.get( ) == DataModel.get( );
			}
		}

		lua_pop( L, 2 );
		return Matches;
	}

	bool PushMethod( lua_State* L, int Key )
	{
		Key = lua_absindex( L, Key );
		PushMethods( L );
		lua_pushvalue( L, Key );
		lua_rawget( L, -2 );
		lua_remove( L, -2 );

		if ( lua_isfunction( L, -1 ) )
			return true;

		lua_pop( L, 1 );
		return false;
	}

	int ContinueMethod( lua_State* L, int Status )
	{
		if ( Status != LUA_OK )
			lua_error( L );

		return lua_gettop( L );
	}

	int InvokeMethod( lua_State* L )
	{
		lua_insert( L, 1 );
		L->ci->errfunc = 0;
		L->ci->flags |= LUA_CALLINFO_HANDLE;
		StkId Function = L->base;
		int Status = luaD_pcall( L, []( lua_State* State, void* Context )
		{
			luaD_callint( State, static_cast<StkId>( Context ), LUA_MULTRET, lua_isyieldable( State ) != 0 );
		}, Function, savestack( L, Function ), 0 );
		expandstacklimit( L, L->top );
		if ( Status == LUA_OK && isyielded( L ) )
			return C_CALL_YIELD;
		L->ci->flags &= ~LUA_CALLINFO_HANDLE;
		return ContinueMethod( L, Status );
	}

	int InvokeOriginal( lua_State* L, MethodSlot Slot )
	{
		PushMethods( L );
		lua_rawgeti( L, -1, Slot );
		lua_remove( L, -2 );
		return InvokeMethod( L );
	}

	int IndexHook( lua_State* L )
	{
		lua_rawcheckstack( L, 4 );
		if ( Reflection::Helpers::AccessProperty( L, false ) )
			return 1;
		if ( CanIntercept( L ) && lua_type( L, 2 ) == LUA_TSTRING && PushMethod( L, 2 ) )
			return 1;

		return InvokeOriginal( L, OriginalIndex );
	}

	int NewIndexHook( lua_State* L )
	{
		lua_rawcheckstack( L, 4 );
		if ( Reflection::Helpers::AccessProperty( L, true ) )
			return 0;
		return InvokeOriginal( L, OriginalNewIndex );
	}

	int NamecallHook( lua_State* L )
	{
		lua_rawcheckstack( L, 4 );
		if ( CanIntercept( L ) && L->namecall )
		{
			lua_pushlstring( L, L->namecall->data, L->namecall->len );
			if ( PushMethod( L, -1 ) )
			{
				lua_remove( L, -2 );
				return InvokeMethod( L );
			}
			lua_pop( L, 1 );
		}

		return InvokeOriginal( L, OriginalNamecall );
	}

	void InstallHooks( lua_State* L )
	{
		int OriginalTop = lua_gettop( L );
		lua_rawcheckstack( L, 8 );
		PushMethods( L );
		int Methods = lua_gettop( L );
		lua_rawgeti( L, Methods, OriginalIndex );
		bool Installed = lua_isfunction( L, -1 );
		lua_pop( L, 1 );

		if ( Installed )
		{
			lua_settop( L, OriginalTop );
			return;
		}

		lua_getglobal( L, "game" );
		int Game = lua_gettop( L );
		lua_pushvalue( L, Game );
		lua_rawseti( L, Methods, Receiver );

		if ( !luaL_getmetafield( L, Game, "__index" ) || !lua_iscfunction( L, -1 ) )
			luaL_error( L, "DataModel index is unavailable" );
		int Index = lua_gettop( L );

		if ( !luaL_getmetafield( L, Game, "__namecall" ) || !lua_iscfunction( L, -1 ) )
			luaL_error( L, "DataModel namecall is unavailable" );
		int Namecall = lua_gettop( L );

		if ( !luaL_getmetafield( L, Game, "__newindex" ) || !lua_iscfunction( L, -1 ) )
			luaL_error( L, "DataModel newindex is unavailable" );
		int NewIndex = lua_gettop( L );

		Closures::Helpers::PushClone( L, Index );
		lua_rawseti( L, Methods, OriginalIndex );
		Closures::Helpers::PushClone( L, Namecall );
		lua_rawseti( L, Methods, OriginalNamecall );
		Closures::Helpers::PushClone( L, NewIndex );
		lua_rawseti( L, Methods, OriginalNewIndex );

		Closure* IndexClosure = clvalue( luaA_toobject( L, Index ) );
		Closure* NamecallClosure = clvalue( luaA_toobject( L, Namecall ) );
		Closure* NewIndexClosure = clvalue( luaA_toobject( L, NewIndex ) );
		IndexClosure->c.f = IndexHook;
		IndexClosure->c.cont = ContinueMethod;
		NamecallClosure->c.f = NamecallHook;
		NamecallClosure->c.cont = ContinueMethod;
		NewIndexClosure->c.f = NewIndexHook;
		NewIndexClosure->c.cont = ContinueMethod;

		lua_settop( L, OriginalTop );
	}
}

void Env::RegisterGlobal( const char* Name, lua_CFunction Function )
{
	if ( !g_Alpen )
		return;

	g_Alpen->Register( Name, Function );
}

void Env::RegisterMethod( const char* Name, lua_CFunction Function )
{
	if ( !g_Alpen || !g_Alpen->State( ) || !Name || !Function )
		return;

	lua_State* L = g_Alpen->State( );
	lua_rawcheckstack( L, 3 );
	PushMethods( L );
	lua_pushcclosure( L, Function, Name, 0 );
	lua_setfield( L, -2, Name );
	lua_pop( L, 1 );
}

void Env::Initialize( lua_State* RobloxState )
{
	g_Env->RobloxState = RobloxState;
	g_Env->RobloxGlobal = RobloxState->global;
	TrackSession( g_Env->ExecutorState );

	g_Roblox = std::make_shared<Environment>( RobloxState );

	luaL_sandboxthread( g_Env->ExecutorState );
	lua_setsafeenv( g_Env->ExecutorState, LUA_GLOBALSINDEX, false );

	g_Alpen = std::make_shared<Environment>( g_Env->ExecutorState );

	// One line per library before its initializer runs: if this environment ever
	// faults during registration, the last name in boot.log is the culprit.
	const std::pair<const char*, void ( * )( )> Libraries[] = {
		{ "Http", Http::Initialize },
		{ "Closures", Closures::Initialize },
		{ "Debug", Debug::Initialize },
		{ "Stack", Stack::Initialize },
		{ "Encoding", Encoding::Initialize },
		{ "Filesystem", Filesystem::Initialize },
		{ "Environment", Libraries::Environment::Initialize },
		{ "Misc", Misc::Initialize },
		{ "Scripts", Scripts::Initialize },
		{ "Instances", Instances::Initialize },
		{ "Cache", Cache::Initialize },
		{ "Drawing", Drawing::Initialize },
		{ "Metatable", Metatable::Initialize },
		{ "Reflection", Reflection::Initialize },
		{ "Signals", Signals::Initialize },
		{ "Actors", Actors::Initialize },
		{ "WebSocket", WebSocket::Initialize },
		{ "Oth", Oth::Initialize },
		{ "Raknet", Raknet::Initialize },
	};

	for ( const auto& Entry : Libraries )
	{
		BootLog::WriteLine( "env", Entry.first );
		Entry.second( );
	}

	InstallHooks( g_Env->ExecutorState );
}

void Env::Shutdown( )
{
	std::queue<std::function<void( )>> Pending;
	{
		std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );
		g_Env->SessionGeneration.fetch_add( 1 );
		Pending.swap( g_Env->YieldQueue );
	}
	{
		std::lock_guard<std::mutex> Lock( g_Env->ScriptQueueMutex );
		g_Env->ScriptQueue.clear( );
	}
	WebSocket::Shutdown( );
	Actors::Shutdown( );
	Raknet::Shutdown( );
	Drawing::Shutdown( );

	g_Alpen.reset( );
	g_Roblox.reset( );
	g_Env->ExecutorState = nullptr;
	g_Env->RobloxState = nullptr;
	g_Env->RobloxGlobal = nullptr;
	g_Env->ScriptContext = 0;
	g_Env->LastDatamodel = 0;
}

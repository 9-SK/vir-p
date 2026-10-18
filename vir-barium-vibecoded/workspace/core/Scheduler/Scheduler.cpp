#include "Scheduler.h"
#include "Helper.h"
#include "client/Environment/Env.h"
#include "utils/BootLog.hpp"
#include <cstdio>
#include <lua.h>
#include <mutex>
#include <string>

namespace Scheduler
{
	inline std::mutex g_InitMutex;

	namespace
	{
		// Stages: every risky engine call is isolated so a fault is a logged bail
		// instead of a dead client. __try cannot live in a frame that needs C++
		// unwinding, hence these thin helpers.

		lua_State* TryCreateExecutorThread( lua_State* RobloxLs, int Top, unsigned long& Code )
		{
			__try
			{
				lua_State* State = lua_newthread( RobloxLs );
				lua_pushvalue( RobloxLs, -1 );
				lua_rawsetp( RobloxLs, LUA_REGISTRYINDEX, &g_InitMutex );
				lua_settop( RobloxLs, Top );
				return State;
			}
			__except ( Code = GetExceptionCode( ), EXCEPTION_EXECUTE_HANDLER )
			{
				return nullptr;
			}
		}

		bool TryCapsEqual( lua_State* State, unsigned long& Code )
		{
			__try
			{
				ThreadCapsEqual( State, 8, g_Env->MaxAvailableInstanceCapabilities );
				return true;
			}
			__except ( Code = GetExceptionCode( ), EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

		bool TryEnvInitialize( lua_State* RobloxLs, unsigned long& Code )
		{
			__try
			{
				Env::Initialize( RobloxLs );
				return true;
			}
			__except ( Code = GetExceptionCode( ), EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

		bool IsCurrentSession( lua_State* L )
		{
			return static_cast<uint64_t>( lua_tonumber( L, lua_upvalueindex( 1 ) ) ) == g_Env->SessionGeneration.load( ) &&
				   GetDataModel( ) == g_Env->LastDatamodel && IsGameLoaded( ) && GetScriptContext( ) == g_Env->ScriptContext &&
				   g_Env->ExecutorState && L->global == g_Env->RobloxGlobal;
		}

		int Pump( lua_State* L )
		{
			if ( IsCurrentSession( L ) )
				g_Env->Yielder( L );

			return 0;
		}

		int Connect( lua_State* L )
		{
			if ( !IsCurrentSession( L ) )
				return 0;

			lua_getglobal( L, "game" );
			lua_getfield( L, -1, "GetService" );
			lua_insert( L, -2 );
			lua_pushstring( L, "RunService" );
			lua_call( L, 2, 1 );
			lua_getfield( L, -1, "Heartbeat" );
			lua_remove( L, -2 );
			lua_getfield( L, -1, "Connect" );
			lua_insert( L, -2 );
			lua_pushvalue( L, lua_upvalueindex( 1 ) );
			lua_pushcclosure( L, Pump, nullptr, 1 );
			lua_call( L, 2, 0 );
			return 0;
		}

		bool StartPump( lua_State* L )
		{
			const int Top = lua_gettop( L );
			lua_getglobal( L, "task" );
			lua_getfield( L, -1, "defer" );
			lua_remove( L, -2 );
			lua_pushnumber( L, static_cast<lua_Number>( g_Env->SessionGeneration.load( ) ) );
			lua_pushcclosure( L, Connect, nullptr, 1 );
			const int Status = lua_pcall( L, 1, 0, 0 );

			if ( Status != LUA_OK )
			{
				const char* Error = lua_tostring( L, -1 );
				Update::Roblox::Functions::Print( 2, "%s", Error ? Error : "failed to schedule executor" );
			}

			lua_settop( L, Top );
			return Status == LUA_OK;
		}

		void Stage( const char* name )
		{
			char Line[ 128 ]{ };
			snprintf( Line, sizeof( Line ), "step: %s", name );
			BootLog::WriteLine( "init", Line );
		}

		void Fault( const char* where, unsigned long Code, uintptr_t Extra = 0 )
		{
			char Line[ 192 ]{ };
			snprintf( Line, sizeof( Line ), "FAULT %s code=%08lx extra=%llx", where, Code,
				static_cast<unsigned long long>( Extra ) );
			BootLog::WriteLine( "init", Line );
		}

		// Every early return in Initialize() names itself once. This runs on the
		// engine's step hook, so silence here is the only way to lose an hour.
		void Bail( const char* reason, uintptr_t a = 0, uintptr_t b = 0 )
		{
			static std::string Last;
			const std::string Line = [ & ]
			{
				char Buffer[ 192 ]{ };
				snprintf( Buffer, sizeof( Buffer ), "%s a=%llx b=%llx", reason,
					static_cast<unsigned long long>( a ), static_cast<unsigned long long>( b ) );
				return std::string( Buffer );
			}( );

			if ( Line == Last )
				return;

			Last = Line;
			BootLog::WriteLine( "init", Line.c_str( ) );
		}
	}

	lua_State*
	GetLuaState( )
	{
		return reinterpret_cast<lua_State*>( KiGetLuaState( GetScriptContext( ) ) );
	}

	void Retire( uintptr_t DataModel )
	{
		std::lock_guard<std::mutex> Lock( g_InitMutex );
		if ( g_Env->LastDatamodel == DataModel )
			Env::Shutdown( );
	}

	bool
	Initialize( )
	{
		std::lock_guard<std::mutex> Lock( g_InitMutex );

		const uintptr_t CurrentDataModel = GetDataModel( );
		Bail( "probe", CurrentDataModel, IsGameLoaded( ) ? 1 : 0 );

		const uintptr_t Context = GetScriptContext( );
		Bail( "context", Context, CurrentDataModel );
		if ( CurrentDataModel && CurrentDataModel == g_Env->LastDatamodel && g_Env->ExecutorState &&
			 IsGameLoaded( ) && Context && Context == g_Env->ScriptContext )
			return true;
		if ( g_Env->LastDatamodel || g_Env->ExecutorState )
			Env::Shutdown( );
		if ( !CurrentDataModel || !IsGameLoaded( ) )
		{
			Bail( "no datamodel or not loaded", CurrentDataModel, 0 );
			return false;
		}

		lua_State* RobloxLs = reinterpret_cast<lua_State*>( KiGetLuaState( Context ) );
		Bail( "ki state", reinterpret_cast<uintptr_t>( RobloxLs ), Context );
		if ( !RobloxLs || CurrentDataModel != GetDataModel( ) || Context != GetScriptContext( ) )
			return false;

		const int Top = lua_gettop( RobloxLs );

		Stage( "bypass write" );
		const BOOLEAN Enabled = TRUE;
		if ( !WriteProcessMemory( GetCurrentProcess( ), reinterpret_cast<void*>( Context + Update::Roblox::RequireBypass ), &Enabled, sizeof( Enabled ), nullptr ) )
		{
			Bail( "require bypass write failed", Context + Update::Roblox::RequireBypass, 0 );
			return false;
		}

		g_Env->LastDatamodel = CurrentDataModel;
		g_Env->ScriptContext = Context;

		unsigned long Code = 0;

		Stage( "lua_newthread" );
		g_Env->ExecutorState = TryCreateExecutorThread( RobloxLs, Top, Code );
		if ( !g_Env->ExecutorState )
		{
			Fault( "lua_newthread", Code, reinterpret_cast<uintptr_t>( RobloxLs ) );
			g_Env->LastDatamodel = 0;
			g_Env->ScriptContext = 0;
			return false;
		}

		Stage( "thread caps" );
		if ( !TryCapsEqual( g_Env->ExecutorState, Code ) )
		{
			Fault( "caps", Code, reinterpret_cast<uintptr_t>( g_Env->ExecutorState ) );
			return false;
		}

		Stage( "env initialize" );
		if ( !TryEnvInitialize( RobloxLs, Code ) )
		{
			Fault( "env", Code, reinterpret_cast<uintptr_t>( RobloxLs ) );
			return false;
		}

		Stage( "pump" );
		try
		{
			if ( !StartPump( g_Env->ExecutorState ) )
				throw std::runtime_error( "failed to start executor pump" );
		}
		catch ( const std::exception& Error )
		{
			OutputDebugStringA( Error.what( ) );
			Bail( "exception", 0, 0 );
			lua_settop( RobloxLs, Top );
			lua_pushnil( RobloxLs );
			lua_rawsetp( RobloxLs, LUA_REGISTRYINDEX, &g_InitMutex );
			Env::Shutdown( );
			return false;
		}

		char Line[ 128 ]{ };
		snprintf( Line, sizeof( Line ), "env up dm=%llx ctx=%llx", (unsigned long long)CurrentDataModel, (unsigned long long)Context );
		BootLog::WriteLine( "init", Line );

		Stage( "winning" );
		ExecuteY( "print('Winning!')" );

		return true;
	}
}
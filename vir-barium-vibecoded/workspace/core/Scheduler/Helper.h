#pragma once
#include <Windows.h>
#include <cstdint>
#undef Yield
#include "core/Globals.h"
#include "Helpers/Helper.h"
#include "utils/BootLog.hpp"
#include <VM/src/ldo.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <lua.h>
#include <lualib.h>
#include <mutex>
#include <stdexcept>
#include <thread>

[[nodiscard]] inline bool
CheckMemory( uintptr_t address )
{
	if ( address < 0x10000 || address > 0x7FFFFFFFFFFF )
		return false;

	MEMORY_BASIC_INFORMATION mbi;
	if ( VirtualQuery( reinterpret_cast<void*>( address ), &mbi, sizeof( mbi ) ) == 0 )
		return false;

	if ( mbi.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) || mbi.State != MEM_COMMIT )
		return false;

	return true;
}

[[nodiscard]] inline uintptr_t
KiGetLuaState( uintptr_t ScriptContext )
{

	if ( !ScriptContext )
		return 0;

	auto vmval1 = []( uint32_t a, uint32_t b ) -> uint32_t
	{
		return b - a;
	};
	auto vmval2 = []( uint32_t a, uint32_t b ) -> uint32_t
	{
		return a + b;
	};
	auto vmval3 = []( uint32_t a, uint32_t b ) -> uint32_t
	{
		return a ^ b;
	};
	auto vmval4 = []( uint32_t a, uint32_t b ) -> uint32_t
	{
		return a - b;
	};

	auto enc_test = [ & ]( uintptr_t address, uint32_t e0, uint32_t e1, auto&& op ) -> uintptr_t
	{
		uint32_t low = op( static_cast<uint32_t>( address ), e0 );
		uint32_t high = op( static_cast<uint32_t>( address ), e1 );

		uintptr_t ptr =
			( static_cast<uint64_t>( high ) << 32 ) | static_cast<uint64_t>( low );

		if ( !ptr )
			return 0;

		if ( !Scheduler::Helpers::ValidState( ptr, ScriptContext ) )
			return 0;

		static bool Reported = false;
		if ( !Reported )
		{
			Reported = true;
			char Line[ 128 ]{ };
			snprintf( Line, sizeof( Line ), "ki state=%llx field=+0x%llx",
				static_cast<unsigned long long>( ptr ),
				static_cast<unsigned long long>( address - ScriptContext ) );
			BootLog::WriteLine( "env", Line );
		}

		return ptr;
	};

	for ( uintptr_t offset = 0; offset <= 0x1000; offset += 0x8 )
	{
		uintptr_t address = ScriptContext + offset;

		uint32_t Encrypted[ 2 ]{ };
		if ( !Scheduler::Helpers::Read( address, Encrypted ) )
			continue;
		uint32_t e0 = Encrypted[ 0 ];
		uint32_t e1 = Encrypted[ 1 ];

		if ( e0 == 0 && e1 == 0 )
			continue;

		if ( uintptr_t p = enc_test( address, e0, e1, vmval1 ) )
			return p;
		if ( uintptr_t p = enc_test( address, e0, e1, vmval2 ) )
			return p;
		if ( uintptr_t p = enc_test( address, e0, e1, vmval3 ) )
			return p;
		if ( uintptr_t p = enc_test( address, e0, e1, vmval4 ) )
			return p;
	}

	static bool ReportedFailure = false;
	if ( !ReportedFailure )
	{
		ReportedFailure = true;
		BootLog::WriteLine( "env", "ki scan found nothing in +0x0..0x1000" );
	}

	return 0;
}

[[nodiscard]] inline uintptr_t
GetDataModel( )
{
	uintptr_t FakeClientDataModel = 0;
	if ( !Scheduler::Helpers::Read( Update::Roblox::DataModel::FakeDataModel, FakeClientDataModel ) || !FakeClientDataModel )
		return 0;

	uintptr_t ClientDataModel = 0;
	if ( !Scheduler::Helpers::Read( FakeClientDataModel + Update::Roblox::DataModel::RealDataModel, ClientDataModel ) || !ClientDataModel )
		return 0;

	return ClientDataModel;
}

[[nodiscard]] inline bool
IsGameLoaded( )
{
	const uintptr_t DataModel = GetDataModel( );
	uintptr_t State = 0;
	if ( !DataModel || !Scheduler::Helpers::Read( DataModel + Update::Roblox::GameLoaded, State ) )
		return false;

	static bool Reported = false;
	if ( !Reported )
	{
		Reported = true;
		char Line[ 128 ]{ };
		snprintf( Line, sizeof( Line ), "gameloaded sentinel=%llu", static_cast<unsigned long long>( State ) );
		BootLog::WriteLine( "env", Line );
	}

	return State == 31;
}

[[nodiscard]] inline uintptr_t
GetScriptContext( )
{
	const uintptr_t ClientDataModel = GetDataModel( );
	uintptr_t Children = 0;
	uintptr_t Begin = 0;
	uintptr_t End = 0;
	uintptr_t ScriptContext = 0;
	if ( !ClientDataModel || !Scheduler::Helpers::Read( ClientDataModel + Update::Roblox::ChildrenStart, Children ) || !Children ||
		!Scheduler::Helpers::Read( Children, Begin ) || !Scheduler::Helpers::Read( Children + sizeof( uintptr_t ), End ) ||
		!Begin || End <= Begin || End - Begin > 1048576 || End - Begin < Update::Roblox::DataModel::ScriptContext + sizeof( uintptr_t ) ||
		!Scheduler::Helpers::Read( Begin + Update::Roblox::DataModel::ScriptContext, ScriptContext ) )
	{
		static bool Reported = false;
		if ( !Reported )
		{
			Reported = true;
			char Line[ 224 ]{ };
			snprintf( Line, sizeof( Line ), "no context dm=%llx children=%llx begin=%llx end=%llx want+0x%llx",
				static_cast<unsigned long long>( ClientDataModel ),
				static_cast<unsigned long long>( Children ),
				static_cast<unsigned long long>( Begin ),
				static_cast<unsigned long long>( End ),
				static_cast<unsigned long long>( Update::Roblox::DataModel::ScriptContext ) );
			BootLog::WriteLine( "env", Line );
		}
		return 0;
	}

	static bool Announced = false;
	if ( !Announced )
	{
		Announced = true;
		char Line[ 128 ]{ };
		snprintf( Line, sizeof( Line ), "scriptcontext=%llx", static_cast<unsigned long long>( ScriptContext ) );
		BootLog::WriteLine( "env", Line );
	}

	return ScriptContext;
}

inline void
ProtoCapsEqual( Proto* Root, uintptr_t* Capabilities )
{
	Root->userdata = Capabilities;
	for ( int i = 0; i < Root->sizep; ++i )
		ProtoCapsEqual( Root->p[ i ], Capabilities );
}

inline void
ThreadCapsEqual( lua_State* L, int Level, uintptr_t Capabilities )
{
	// 0.743 fork: the identity block at extra-space +0x30 leads with a plain
	// integer (RBX::Identity assigns from int); the 0.741 shape had a nested
	// `.type` member instead.
	L->userdata->identity = Level;
	L->userdata->capabilities = Capabilities;
}

struct YieldState
{
	uintptr_t State;
	uintptr_t TimeUsed;
	uintptr_t Useless;
};

struct YieldingLuaThread
{
	char Pad0[ 0x28 ];
	lua_State* L;
};

using Yielded = std::function<int( lua_State* )>;

namespace Update
{
	namespace Roblox
	{
		constexpr uintptr_t ScriptContextToResume = Update::Roblox::SC2Resume;

		namespace Functions
		{
			inline auto ScriptContextResume = reinterpret_cast<uint64_t( __fastcall* )( uint64_t, YieldState*, YieldingLuaThread**, uint32_t, uint8_t, const std::string* )>( Update::LuaU::SCResume );
		}
	}
}

namespace Yielding
{
	inline void Submit( std::function<void( )> Function )
	{
		auto Work = std::make_unique<std::function<void( )>>( std::move( Function ) );

		if ( !TrySubmitThreadpoolCallback( []( PTP_CALLBACK_INSTANCE Callback, void* Context )
		{
			CallbackMayRunLong( Callback );
			std::unique_ptr<std::function<void( )>> Work( static_cast<std::function<void( )>*>( Context ) );
			try
			{
				( *Work )( );
			}
			catch ( const std::exception& Exception )
			{
				OutputDebugStringA( Exception.what( ) );
			}
			catch ( ... )
			{
				OutputDebugStringA( "asynchronous operation failed" );
			}
		}, Work.get( ), nullptr ) )
			throw std::runtime_error( "failed to submit asynchronous operation" );

		Work.release( );
	}

	inline void Yield( )
	{
		const auto Deadline = std::chrono::steady_clock::now( ) + std::chrono::milliseconds( 2 );
		size_t Count = 0;
		{
			std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );
			Count = ( std::min ) ( g_Env->YieldQueue.size( ), size_t( 32 ) );
		}

		while ( Count-- )
		{
			std::function<void( )> Request;
			{
				std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );
				if ( g_Env->YieldQueue.empty( ) )
					break;

				Request = std::move( g_Env->YieldQueue.front( ) );
				g_Env->YieldQueue.pop( );
			}

			if ( Request )
				Request( );

			if ( std::chrono::steady_clock::now( ) >= Deadline )
				break;
		}
	}

	inline int ResumeError( lua_State* L )
	{
		if ( static_cast<uint64_t>( lua_tonumber( L, lua_upvalueindex( 3 ) ) ) != g_Env->SessionGeneration.load( ) || GetDataModel( ) != g_Env->LastDatamodel )
			return 0;

		lua_State* Target = lua_tothread( L, lua_upvalueindex( 1 ) );

		if ( !Target || lua_status( Target ) != LUA_YIELD || !Target->userdata || !Target->userdata->shared || !Target->userdata->shared->scriptContext )
			return 0;

		std::string Error = lua_tostring( L, lua_upvalueindex( 2 ) );
		YieldingLuaThread Thread{ };
		Thread.L = Target;
		YieldingLuaThread* ThreadPointer = &Thread;
		YieldState State{ };
		uintptr_t ScriptContext = reinterpret_cast<uintptr_t>( Target->userdata->shared->scriptContext );
		Update::Roblox::Functions::ScriptContextResume( ScriptContext + Update::Roblox::ScriptContextToResume, &State, &ThreadPointer, 0, true, &Error );
		return 0;
	}

	inline int
	YieldExec( lua_State* L, const std::function<Yielded( )>& YieldingClosure )
	{
		if ( !lua_isyieldable( L ) )
			luaL_error( L, "attempt to yield across metamethod/C-call boundary" );

		if ( !L->userdata || !L->userdata->shared || !L->userdata->shared->scriptContext )
			luaL_error( L, "script context is unavailable" );

		const uint64_t Generation = g_Env->SessionGeneration.load( );
		const uintptr_t DataModel = GetDataModel( );
		const uintptr_t ScriptContext = reinterpret_cast<uintptr_t>( L->userdata->shared->scriptContext );
		lua_rawcheckstack( L, 1 );
		lua_pushthread( L );
		int ThreadRef = lua_ref( L, -1 );
		lua_pop( L, 1 );
		std::string LaunchError;

		try
		{
			Submit( [ L, Generation, DataModel, ScriptContext, ThreadRef, YieldingClosure ]
			{
				Yielded ResumeFunction;

				try
				{
					ResumeFunction = YieldingClosure( );
				}
				catch ( const std::exception& Exception )
				{
					ResumeFunction = [ Error = std::string( Exception.what( ) ) ]( lua_State* State )
					{
						lua_pushlstring( State, Error.data( ), Error.size( ) );
						return -1;
					};
				}

				std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );

				if ( Generation != g_Env->SessionGeneration.load( ) )
					return;

				g_Env->YieldQueue.emplace( [ L, Generation, DataModel, ScriptContext, ThreadRef, ResumeFunction = std::move( ResumeFunction ) ]
				{
					if ( Generation != g_Env->SessionGeneration.load( ) || DataModel != GetDataModel( ) ||
						!g_Env->ExecutorState || ScriptContext != g_Env->ScriptContext || DataModel != g_Env->LastDatamodel )
						return;

					if ( lua_status( L ) != LUA_YIELD || !L->userdata || !L->userdata->shared || reinterpret_cast<uintptr_t>( L->userdata->shared->scriptContext ) != ScriptContext )
					{
						lua_unref( L, ThreadRef );
						return;
					}

					struct Completion
					{
						const Yielded* Function;
						int Count = 0;
					};

					Completion Result{ &ResumeFunction };
					const int Top = lua_gettop( L );
					int Status = luaD_rawrunprotected( L, []( lua_State* State, void* Context )
					{
						Completion* Result = static_cast<Completion*>( Context );
						lua_rawcheckstack( State, LUA_MINSTACK );
						Result->Count = ( *Result->Function )( State );
					}, &Result );
					bool Failed = Status != LUA_OK || Result.Count < 0;
					std::string Error;

					if ( Failed )
					{
						const char* Message = lua_type( L, -1 ) == LUA_TSTRING ? lua_tostring( L, -1 ) : nullptr;
						Error = Status == LUA_ERRMEM ? "not enough memory" : Message ? Message
																					 : "asynchronous operation failed";
						lua_settop( L, Top );
					}

					lua_State* Scheduler = g_Env->ExecutorState;
					const int SchedulerTop = lua_gettop( Scheduler );
					lua_rawcheckstack( Scheduler, ( Failed ? 3 : Result.Count ) + 3 );
					lua_xpush( Scheduler->global->mainthread, Scheduler, LUA_GLOBALSINDEX );
					lua_getfield( Scheduler, -1, "task" );
					lua_remove( Scheduler, -2 );
					lua_getfield( Scheduler, -1, "defer" );
					lua_remove( Scheduler, -2 );
					lua_getref( Scheduler, ThreadRef );

					if ( Failed )
					{
						lua_pushlstring( Scheduler, Error.data( ), Error.size( ) );
						lua_pushnumber( Scheduler, static_cast<lua_Number>( Generation ) );
						lua_pushcclosure( Scheduler, ResumeError, nullptr, 3 );
					}
					else
						lua_xmove( L, Scheduler, Result.Count );

					if ( lua_pcall( Scheduler, Failed ? 1 : Result.Count + 1, 0, 0 ) != LUA_OK )
					{
						const char* Message = lua_tostring( Scheduler, -1 );
						std::string ScheduleError = Message ? Message : "failed to schedule asynchronous result";
						YieldingLuaThread Thread{ };
						Thread.L = L;
						YieldingLuaThread* ThreadPointer = &Thread;
						YieldState State{ };
						Update::Roblox::Functions::ScriptContextResume( ScriptContext + Update::Roblox::ScriptContextToResume, &State, &ThreadPointer, 0, true, &ScheduleError );
					}

					lua_settop( Scheduler, SchedulerTop );
					lua_unref( L, ThreadRef );
				} );
			} );
		}
		catch ( const std::exception& Exception )
		{
			LaunchError = Exception.what( );
		}

		if ( !LaunchError.empty( ) )
		{
			lua_unref( L, ThreadRef );
			luaL_error( L, "failed to start asynchronous operation: %s", LaunchError.c_str( ) );
		}

		return lua_yield( L, 0 );
	}

}

inline void
Sync( lua_State* L )
{
	( void ) L;
}
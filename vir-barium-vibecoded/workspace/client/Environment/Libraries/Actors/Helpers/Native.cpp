#include "Native.h"
#include "Environment.h"
#include "Helper.h"
#include "client/Environment/Libraries/Instances/Helpers/Helpers.h"
#include "core/Execution/Execution.h"
#include "core/Globals.h"
#include "core/Roblox/Update.h"
#include "core/Roblox/Variant.h"
#include "core/Scheduler/Helper.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lstate.h>
#include <array>
#include <cstring>
#include <lualib.h>
#include <stdexcept>
#include <unordered_set>

namespace Actors::Native
{
	namespace
	{
		namespace Layout = Update::Roblox::Actors;
		struct WeakThreadRef
		{
			uintptr_t Vtable = 0;
			uint64_t Reference = 0;
			uintptr_t Object = 0;
			uintptr_t Control = 0;
		};
		static_assert( sizeof( WeakThreadRef ) == 32 );
		// 0.743 fork layout (see lua.h static_asserts): actor 0x90, scriptContext 0x30.
		static_assert( offsetof( RobloxExtraSpace, actor ) == 0x90 );
		static_assert( offsetof( RobloxExtraSpace, shared ) == 0x18 );
		static_assert( offsetof( RobloxShared, scriptVmState ) == 0x10 );
		static_assert( offsetof( RobloxShared, scriptContext ) == 0x30 );

		template <typename T>
		T Read( uintptr_t Address )
		{
			T Value{ };
			if ( !Scheduler::Helpers::Read( Address, Value ) )
				throw std::runtime_error( "Actor native state is unavailable" );
			return Value;
		}

		uintptr_t Context( lua_State* L )
		{
			if ( !L || !L->userdata || !L->userdata->shared )
				throw std::runtime_error( "Actor thread has no engine context" );
			const uintptr_t Owner = reinterpret_cast<uintptr_t>( L->userdata->shared->scriptContext );
			if ( !Owner || Owner != g_Env->ScriptContext )
				throw std::runtime_error( "Actor thread belongs to an ended session" );
			return Owner;
		}

		class Reference final : public Thread
		{
		  public:
			explicit Reference( lua_State* State, Instance Owner ) : m_Generation( Generation( ) )
			{
				Actor = std::move( Owner );
				reinterpret_cast<void* ( * ) ( WeakThreadRef*, lua_State* )>( Layout::CreateThreadRef )( &m_Reference, State );
			}

			~Reference( ) override
			{
				reinterpret_cast<void ( * )( WeakThreadRef* )>( Layout::DestroyThreadRef )( &m_Reference );
			}

			std::shared_ptr<lua_State> Lock( ) const override
			{
				if ( m_Generation != Generation( ) )
					return nullptr;
				std::shared_ptr<uintptr_t> Live;
				reinterpret_cast<void* ( * ) ( const WeakThreadRef*, std::shared_ptr<uintptr_t>* )>( Layout::LockThreadRef )( &m_Reference, &Live );
				if ( !Live )
					return nullptr;
				auto* State = Read<lua_State*>( reinterpret_cast<uintptr_t>( Live.get( ) ) + Layout::LiveThread );
				if ( !State || !Scheduler::Helpers::ValidState( reinterpret_cast<uintptr_t>( State ), g_Env->ScriptContext ) )
					return nullptr;
				return std::shared_ptr<lua_State>( std::move( Live ), State );
			}

		  private:
			WeakThreadRef m_Reference;
			uint64_t m_Generation;
		};

		struct Scan
		{
			lua_State* State;
			Snapshot* Result;
			std::unordered_set<uintptr_t>* Seen;
			size_t Count = 0;
			int Roots = 0;
			size_t Capacity = 0;
			bool Overflow = false;
		};

		void AddActor( Scan& Visit, const Instance& Value )
		{
			const uintptr_t Address = reinterpret_cast<uintptr_t>( Value.get( ) );
			if ( Value && Instances::Helpers::IsClass( Address, "Actor" ) && Visit.Seen->insert( Address ).second )
				Visit.Result->Actors.push_back( Value );
		}

		bool Collect( void* Data, lua_Page*, GCObject* Object )
		{
			auto& Visit = *static_cast<Scan*>( Data );
			if ( isdead( Visit.State->global, Object ) )
				return false;
			if ( Object->gch.tt == LUA_TTHREAD )
			{
				auto* State = gco2th( Object );
				if ( !State->userdata || State->userdata->actor.expired( ) )
					return false;
				++Visit.Count;
				if ( Visit.Roots )
				{
					if ( Visit.Count > Visit.Capacity )
					{
						Visit.Overflow = true;
						return false;
					}

					setthvalue( Visit.State, Visit.State->top, State );
					Visit.State->top++;
					lua_rawseti( Visit.State, Visit.Roots, static_cast<int>( Visit.Count ) );
					AddActor( Visit, State->userdata->actor.lock( ) );
				}
			}
			else if ( Visit.Roots && Object->gch.tt == LUA_TUSERDATA )
			{
				auto* Userdata = gco2u( Object );
				auto* Game = uvalue( luaA_toobject( Visit.State, Visit.Roots - 1 ) );
				if ( Userdata->tag == Game->tag && Userdata->metatable == Game->metatable && Userdata->len == sizeof( Instance ) )
					AddActor( Visit, *reinterpret_cast<const Instance*>( Userdata->data ) );
			}
			return false;
		}

		void ScanVm( lua_State* Main, Snapshot& Result, std::unordered_set<uintptr_t>& Seen )
		{
			const int Top = lua_gettop( Main );
			try
			{
				lua_rawcheckstack( Main, 6 );
				lua_getglobal( Main, "game" );
				Instances::Helpers::CheckInstance( Main, -1 );
				Scan Visit{ Main, &Result, &Seen };
				luaM_visitgco( Main, &Visit, Collect );
				if ( Visit.Count > 1048576 )
					throw std::runtime_error( "Actor thread collection exceeds limit" );
				Visit.Capacity = Visit.Count;
				lua_createtable( Main, static_cast<int>( Visit.Capacity ), 0 );
				Visit.Roots = lua_gettop( Main );
				Visit.Count = 0;
				luaC_threadbarrier( Main );
				luaM_visitgco( Main, &Visit, Collect );
				if ( Visit.Overflow )
					throw std::runtime_error( "Actor thread set changed during collection" );
				for ( size_t Index = 1; Index <= Visit.Count; ++Index )
				{
					lua_rawgeti( Main, Visit.Roots, static_cast<int>( Index ) );
					auto* State = lua_tothread( Main, -1 );
					auto Actor = State->userdata->actor.lock( );
					if ( Actor )
						Result.Threads.push_back( std::make_shared<Reference>( State, std::move( Actor ) ) );
					lua_pop( Main, 1 );
				}
			}
			catch ( ... )
			{
				lua_settop( Main, Top );
				throw;
			}
			lua_settop( Main, Top );
		}
	}

	uint64_t Generation( )
	{
		return g_Env->SessionGeneration.load( );
	}

	bool Parallel( lua_State* L )
	{
		const uintptr_t Owner = Read<uintptr_t>( Context( L ) + Layout::Execution );
		return Owner && Read<uint8_t>( Owner + Layout::Parallel ) != 0;
	}

	Snapshot Enumerate( lua_State* L )
	{
		const uintptr_t Owner = Context( L ) + Layout::VmOwner;
		if ( Parallel( L ) )
			throw std::runtime_error( "Actor enumeration requires serial execution" );
		std::vector<lua_State*> Vms{ g_Env->RobloxState->global->mainthread };
		std::unordered_set<void*> Globals{ Vms.front( )->global };
		{
			struct PoolLock
			{
				uintptr_t Address;
				explicit PoolLock( uintptr_t Value ) : Address( Value )
				{
					Update::Roblox::Functions::LockSignal( Address );
				}
				~PoolLock( )
				{
					Update::Roblox::Functions::UnlockSignal( Address );
				}
			} Guard( Owner + Layout::Guard );
			for ( size_t Pool = 0; Pool < 2; ++Pool )
			{
				const uintptr_t Vector = Owner + Layout::Pool + Pool * Layout::PoolStride;
				const auto Bounds = Read<std::array<uintptr_t, 3>>( Vector );
				if ( Bounds[ 1 ] < Bounds[ 0 ] || Bounds[ 2 ] < Bounds[ 1 ] || ( Bounds[ 1 ] - Bounds[ 0 ] ) % sizeof( uintptr_t ) || Bounds[ 1 ] - Bounds[ 0 ] > 65536 )
					throw std::runtime_error( "invalid Actor VM pool" );
				for ( uintptr_t Cursor = Bounds[ 0 ]; Cursor != Bounds[ 1 ]; Cursor += sizeof( uintptr_t ) )
				{
					const uintptr_t Vm = Read<uintptr_t>( Cursor );
					const uintptr_t Storage = Vm + Layout::VmState;
					const auto Encoded = Read<std::array<uint32_t, 2>>( Storage );
					const uintptr_t Address = uint64_t( uint32_t( Encoded[ 0 ] - uint32_t( Storage ) ) ) | ( uint64_t( uint32_t( Encoded[ 1 ] - uint32_t( Storage ) ) ) << 32 );
					if ( !Read<uint8_t>( Vm + Layout::ActorVm ) || !Scheduler::Helpers::ValidState( Address, g_Env->ScriptContext ) )
						throw std::runtime_error( "invalid Actor VM state; verify Update.h" );
					auto* Main = reinterpret_cast<lua_State*>( Address );
					if ( Globals.insert( Main->global ).second )
						Vms.push_back( Main->global->mainthread );
				}
			}
		}
		Snapshot Result;
		std::unordered_set<uintptr_t> Seen;
		const int Top = lua_gettop( L );
		try
		{
			Instances::Helpers::PushInstances( L );
			for ( int Index = 1, Count = lua_objlen( L, -1 ); Index <= Count; ++Index )
			{
				lua_rawgeti( L, -1, Index );
				auto Value = Instances::Helpers::CheckInstance( L, -1 );
				const uintptr_t Address = reinterpret_cast<uintptr_t>( Value.get( ) );
				if ( Instances::Helpers::IsClass( Address, "Actor" ) && Seen.insert( Address ).second )
					Result.Actors.push_back( Value );
				lua_pop( L, 1 );
			}
			for ( auto* Main : Vms )
				ScanVm( Main, Result, Seen );
		}
		catch ( ... )
		{
			lua_settop( L, Top );
			throw;
		}
		lua_settop( L, Top );
		return Result;
	}

	ThreadRef CreateTarget( lua_State* L, const Instance& Actor )
	{
		const uintptr_t Owner = Context( L );
		if ( Parallel( L ) || Deleted( Actor ) )
			throw std::runtime_error( "cannot initialize this Actor in the current state" );
		const int Top = lua_gettop( L );
		/* mod = state */
		lua_getglobal( L, "Instance" );
		lua_getfield( L, -1, "new" );
		lua_remove( L, -2 );
		lua_pushliteral( L, "ModuleScript" );
		lua_call( L, 1, 1 );
		const int ScriptIndex = lua_gettop( L );
		lua_pushliteral( L, "AlpenActorContext" );
		lua_setfield( L, ScriptIndex, "Name" );
		lua_getfield( L, ScriptIndex, "Destroy" );
		const int DestroyIndex = lua_gettop( L );
		auto Cleanup = [ & ]
		{
			lua_pushvalue( L, DestroyIndex );
			lua_pushvalue( L, ScriptIndex );
			lua_pcall( L, 1, 0, 0 );
			lua_settop( L, Top );
		};
		try
		{
			PushInstance( L, Actor );
			lua_setfield( L, ScriptIndex, "Parent" );
			auto Script = CheckInstance( L, ScriptIndex, "ModuleScript" );
			const uint32_t Identity = 2; // Native game-script pool, matching LocalScripts.
			auto* State = reinterpret_cast<lua_State* ( * ) ( uintptr_t, const uint32_t*, const Instance* )>( Layout::GetVm )( Owner, &Identity, &Script );
			if ( !Scheduler::Helpers::ValidState( reinterpret_cast<uintptr_t>( State ), Owner ) ||
				 !Read<uint8_t>( reinterpret_cast<uintptr_t>( State->userdata->shared->scriptVmState ) + Layout::ActorVm ) )
				throw std::runtime_error( "native Actor VM initialization failed" );
			auto Result = std::make_shared<Reference>( State, Actor );
			Cleanup( );
			return Result;
		}
		catch ( ... )
		{
			Cleanup( );
			throw;
		}
	}

	bool Deleted( const Instance& Value )
	{
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Value.get( ) );
		return !Value || ( !Read<uintptr_t>( Address + Layout::Parent ) && ( Read<uint8_t>( Address + Layout::ParentLocked ) & Layout::ParentLockedMask ) != 0 );
	}

	Instance CheckInstance( lua_State* L, int Index, const char* Class )
	{
		return Instances::Helpers::CheckInstance( L, Index, Class );
	}

	void PushInstance( lua_State* L, const Instance& Value )
	{
		Update::Roblox::Functions::PushInstance( L, &Value );
	}

	std::shared_ptr<void> ReadValue( lua_State* L, int Index )
	{
		auto Value = std::make_shared<RBX::Variant>( );
		if ( !Update::Roblox::Functions::ReadVariant( L, Index, Value.get( ), false ) )
			throw std::runtime_error( "unsupported userdata in Actor arguments" );
		return Value;
	}

	void PushValue( lua_State* L, const std::shared_ptr<void>& Value )
	{
		const int Top = lua_gettop( L );
		Update::Roblox::Functions::PushVariant( static_cast<const RBX::Variant*>( Value.get( ) ), L );
		if ( lua_gettop( L ) != Top + 1 )
			throw std::runtime_error( "native Actor value conversion failed" );
	}

	std::string Compile( const std::string& Source )
	{
		return Alpen::Execution::Compile( Source );
	}

	void Load( lua_State* L, const std::string& Bytecode )
	{
		if ( luau_load( L, "=Actors", Bytecode.data( ), Bytecode.size( ), 0 ) != LUA_OK )
			throw std::runtime_error( lua_tostring( L, -1 ) ? lua_tostring( L, -1 ) : "Actor bytecode load failed" );
		ProtoCapsEqual( clvalue( luaA_toobject( L, -1 ) )->l.p, &g_Env->MaxAvailableInstanceCapabilities );
	}

	void Prepare( lua_State* L, const Instance& Actor )
	{
		Context( L );
		auto Script = L->userdata->script.lock( );
		L->userdata->actor = Actor;
		L->userdata->script.reset( );
		ThreadCapsEqual( L, 8, g_Env->MaxAvailableInstanceCapabilities );
		Helpers::PrepareEnvironment( L );
		PushInstance( L, Script );
		lua_setglobal( L, "script" );
	}

	void Enqueue( lua_State* L, int Arguments )
	{
		const uintptr_t Owner = Context( L );
		WeakThreadRef Reference;
		reinterpret_cast<void* ( * ) ( WeakThreadRef*, lua_State* )>( Layout::CreateThreadRef )( &Reference, L );
		reinterpret_cast<void ( * )( uintptr_t, WeakThreadRef*, int )>( Layout::QueueParallel )( Owner + Layout::Queue, &Reference, Arguments );
	}
}

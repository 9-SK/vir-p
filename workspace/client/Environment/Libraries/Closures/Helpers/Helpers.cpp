#include "Helpers.h"
#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "core/Execution/Execution.h"
#include "core/Globals.h"
#include <VM/src/lapi.h>
#include <VM/src/ldo.h>
#include <VM/src/lfunc.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <VM/src/ltable.h>
#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <string>

namespace Closures::Helpers
{
	namespace
	{
		struct FrameContext
		{
			lua_State* State;
			Closure* Target;
			Closure* Snapshot;
		};

		char g_Wrappers;
		char g_Hooks;

		void PushFunction( lua_State* L, Closure* Function )
		{
			luaC_threadbarrier( L );
			setclvalue( L, L->top, Function );
			L->top++;
		}

		void PushRegistry( lua_State* L, void* Key )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, Key );

			if ( lua_istable( L, -1 ) )
				return;

			lua_pop( L, 1 );
			lua_newtable( L );
			lua_newtable( L );
			lua_pushliteral( L, "k" );
			lua_setfield( L, -2, "__mode" );
			lua_setmetatable( L, -2 );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, Key );
		}

		void PushEntry( lua_State* L, void* Key, int Index )
		{
			Index = lua_absindex( L, Index );
			PushRegistry( L, Key );
			lua_pushvalue( L, Index );
			lua_rawget( L, -2 );
			lua_remove( L, -2 );
		}

		void SetEntry( lua_State* L, void* Key, int Index, int Value )
		{
			Index = lua_absindex( L, Index );
			Value = lua_absindex( L, Value );
			PushRegistry( L, Key );
			lua_pushvalue( L, Index );
			lua_pushvalue( L, Value );
			lua_rawset( L, -3 );
			lua_pop( L, 1 );
		}

		int PrepareEntry( lua_State* L, void* Key, int Index )
		{
			Index = lua_absindex( L, Index );
			PushRegistry( L, Key );
			int Table = lua_gettop( L );
			luaH_set( L, hvalue( luaA_toobject( L, Table ) ), luaA_toobject( L, Index ) );
			return Table;
		}

		void CommitEntry( lua_State* L, int Table, int Index, int Value )
		{
			lua_pushvalue( L, Index );
			lua_pushvalue( L, Value );
			lua_rawset( L, Table );
		}

		int Continue( lua_State* L, int Status )
		{
			if ( Status == LUA_OK )
				return lua_gettop( L );

			lua_rawcheckstack( L, 5 );
			PushFunction( L, clvalue( L->ci->func ) );
			PushEntry( L, &g_Wrappers, -1 );
			bool Sanitize = false;

			if ( lua_istable( L, -1 ) )
			{
				lua_rawgeti( L, -1, 2 );
				Sanitize = lua_toboolean( L, -1 );
				lua_pop( L, 1 );
			}

			lua_pop( L, 2 );

			if ( Sanitize && lua_type( L, -1 ) == LUA_TSTRING )
			{
				size_t Length = 0;
				const char* Message = lua_tolstring( L, -1, &Length );

				for ( size_t Index = 0; Index + 3 < Length; ++Index )
				{
					if ( Message[ Index ] != ':' || Message[ Index + 1 ] < '0' || Message[ Index + 1 ] > '9' )
						continue;

					size_t End = Index + 1;

					while ( End < Length && Message[ End ] >= '0' && Message[ End ] <= '9' )
						++End;

					if ( End + 1 < Length && Message[ End ] == ':' && Message[ End + 1 ] == ' ' )
					{
						lua_pushlstring( L, Message + End + 2, Length - End - 2 );
						lua_replace( L, -2 );
						break;
					}
				}
			}

			lua_error( L );
		}

		int Dispatch( lua_State* L )
		{
			int Arguments = lua_gettop( L );
			lua_rawcheckstack( L, 6 );
			PushFunction( L, clvalue( L->ci->func ) );
			PushEntry( L, &g_Wrappers, -1 );

			if ( !lua_istable( L, -1 ) )
				luaL_error( L, "closure target is unavailable" );

			lua_rawgeti( L, -1, 1 );
			lua_remove( L, -2 );
			lua_remove( L, -2 );
			lua_insert( L, 1 );
			L->ci->errfunc = 0;
			L->ci->flags |= LUA_CALLINFO_HANDLE;
			StkId Function = L->top - Arguments - 1;
			int Status = luaD_pcall( L, []( lua_State* State, void* Context )
									 {
										 luaD_callint( State, static_cast<StkId>( Context ), LUA_MULTRET, lua_isyieldable( State ) != 0 );
									 },
									 Function, savestack( L, Function ), 0 );
			expandstacklimit( L, L->top );

			if ( Status == LUA_OK && isyielded( L ) )
				return C_CALL_YIELD;

			L->ci->flags &= ~LUA_CALLINFO_HANDLE;
			return Continue( L, Status );
		}

		void PushBinding( lua_State* L, int Function, bool Sanitize )
		{
			Function = lua_absindex( L, Function );
			lua_createtable( L, 2, 0 );
			lua_pushvalue( L, Function );
			lua_rawseti( L, -2, 1 );
			lua_pushboolean( L, Sanitize );
			lua_rawseti( L, -2, 2 );
		}

		bool PinFrames( void* Context, lua_Page*, GCObject* Object )
		{
			auto* Frames = static_cast<FrameContext*>( Context );

			if ( isdead( Frames->State->global, Object ) )
				return false;

			if ( Object->gch.tt == LUA_TTABLE && Frames->Target->isC )
				gco2h( Object )->safeenv = false;

			if ( Object->gch.tt != LUA_TTHREAD )
				return false;

			lua_State* Thread = gco2th( Object );

			for ( CallInfo* Frame = Thread->base_ci; Frame <= Thread->ci; ++Frame )
			{
				if ( ttisfunction( Frame->func ) && clvalue( Frame->func ) == Frames->Target )
				{
					luaC_threadbarrier( Thread );
					setclvalue( Thread, Frame->func, Frames->Snapshot );
				}
			}

			return false;
		}

		void PreserveFrames( lua_State* L, Closure* Target, Closure* Snapshot )
		{
			FrameContext Context{ L, Target, Snapshot };
			luaM_visitgco( L, &Context, PinFrames );
		}

		void InvalidateDirectCalls( lua_State* L, Closure* Target )
		{
			for ( size_t Tag = 0; Tag < UTAG_INTERNAL_LIMIT; ++Tag )
			{
				auto& Direct = L->global->udatadirect[ Tag ];
				if ( ttisfunction( &Direct.indextm ) && clvalue( &Direct.indextm ) == Target )
				{
					Direct.index = nullptr;
					L->global->udatadirectfields[ Tag ] = nullptr;
				}
				if ( ttisfunction( &Direct.newindextm ) && clvalue( &Direct.newindextm ) == Target )
					Direct.newindex = nullptr;
				if ( ttisfunction( &Direct.namecalltm ) && clvalue( &Direct.namecalltm ) == Target )
					Direct.namecall = nullptr;
			}
		}

		void Copy( lua_State* L, Closure* Target, Closure* Source )
		{
			Target->env = Source->env;
			Target->stacksize = Source->stacksize;
			Target->preload = Source->preload;
			luaC_objbarrier( L, Target, Source->env );

			if ( Target->isC )
			{
				Target->c.f = Source->c.f;
				Target->c.cont = static_cast<lua_Continuation>( Source->c.cont );
				Target->c.debugname = Source->c.debugname;

				if ( Source->c.debugname )
					luaC_objbarrier( L, Target, Source->c.debugname );

				for ( int Index = 0; Index < Target->nupvalues; ++Index )
				{
					setobj( L, &Target->c.upvals[ Index ], &Source->c.upvals[ Index ] );
					luaC_barrier( L, Target, &Target->c.upvals[ Index ] );
				}
			}
			else
			{
				Target->l.p = Source->l.p;
				luaC_objbarrier( L, Target, Source->l.p );

				for ( int Index = 0; Index < Target->nupvalues; ++Index )
				{
					setobj( L, &Target->l.uprefs[ Index ], &Source->l.uprefs[ Index ] );
					luaC_barrier( L, Target, &Target->l.uprefs[ Index ] );
				}
			}
		}

		void PushBridge( lua_State* L, Closure* Target, int Hook )
		{
			Hook = lua_absindex( L, Hook );
			static const std::string Bytecode = Alpen::Execution::Compile( "return (\"__alpen_bridge\") (...)" );

			if ( luau_load( L, "=closure", Bytecode.data( ), Bytecode.size( ), 0 ) != LUA_OK )
				lua_error( L );

			Proto* Prototype = clvalue( luaA_toobject( L, -1 ) )->l.p;
			int Constant = -1;

			for ( int Index = 0; Index < Prototype->sizek; ++Index )
			{
				if ( ttisstring( &Prototype->k[ Index ] ) && std::strcmp( tsvalue( &Prototype->k[ Index ] )->data, "__alpen_bridge" ) == 0 )
					Constant = Index;
			}

			if ( Constant < 0 )
				luaL_error( L, "invalid closure bridge" );

			Closure* Bridge = luaF_newLclosure( L, Target->nupvalues, Target->env, Prototype );
			Prototype->nups = Target->nupvalues;
			setobj( L, &Prototype->k[ Constant ], luaA_toobject( L, Hook ) );
			luaC_barrier( L, Prototype, &Prototype->k[ Constant ] );
			setclvalue( L, L->top - 1, Bridge );
			Prototype->userdata = static_cast<uint64_t*>( Target->l.p->userdata );
			Prototype->debugname = static_cast<TString*>( Target->l.p->debugname );
			Prototype->source = static_cast<TString*>( Target->l.p->source );
			Prototype->linedefined = Target->l.p->linedefined;
			if ( Prototype->debugname )
				luaC_objbarrier( L, Prototype, Prototype->debugname );
			if ( Prototype->source )
				luaC_objbarrier( L, Prototype, Prototype->source );
		}
	}

	Closure* CheckFunction( lua_State* L, int Index )
	{
		luaL_checktype( L, Index, LUA_TFUNCTION );
		return clvalue( luaA_toobject( L, Index ) );
	}

	bool IsCaller( lua_State* L )
	{
		L = Oth::Helpers::Original( L );
		if ( !L->userdata || !L->userdata->script.expired( ) )
			return false;
		for ( CallInfo* Frame = L->base_ci + 1; Frame <= L->ci; ++Frame )
		{
			if ( !ttisfunction( Frame->func ) )
				continue;
			Closure* Function = clvalue( Frame->func );
			if ( !Function->isC )
				return Function->l.p->userdata == &g_Env->MaxAvailableInstanceCapabilities;
		}
		return L->userdata->capabilities == g_Env->MaxAvailableInstanceCapabilities;
	}

	bool IsExecutor( Closure* Function )
	{
		if ( !Function->isC )
			return Function->l.p->userdata == &g_Env->MaxAvailableInstanceCapabilities;

		MEMORY_BASIC_INFORMATION FunctionMemory{ };
		MEMORY_BASIC_INFORMATION ModuleMemory{ };
		return VirtualQuery( reinterpret_cast<const void*>( Function->c.f ), &FunctionMemory, sizeof( FunctionMemory ) ) && VirtualQuery( reinterpret_cast<const void*>( Dispatch ), &ModuleMemory, sizeof( ModuleMemory ) ) && FunctionMemory.AllocationBase == ModuleMemory.AllocationBase;
	}

	bool IsBridge( lua_State* L, Closure* Function )
	{
		if ( Function->isC )
			return false;
		TValue Key;
		setpvalue( &Key, &g_Wrappers, 0 );
		const TValue* Wrappers = luaH_get( hvalue( registry( L ) ), &Key );
		if ( !ttistable( Wrappers ) )
			return false;
		setclvalue( L, &Key, Function );
		const TValue* Value = luaH_get( hvalue( Wrappers ), &Key );
		return ttisboolean( Value ) && bvalue( Value );
	}

	int PushClone( lua_State* L, int Index )
	{
		Index = lua_absindex( L, Index );
		Closure* Source = CheckFunction( L, Index );
		lua_rawcheckstack( L, 8 );
		luaC_checkGC( L );
		Closure* Clone = Source->isC ? luaF_newCclosure( L, Source->nupvalues, Source->env ) : luaF_newLclosure( L, Source->nupvalues, Source->env, Source->l.p );
		Copy( L, Clone, Source );
		PushFunction( L, Clone );
		int Result = lua_gettop( L );

		if ( ( Source->isC && Source->c.f == Dispatch ) || IsBridge( L, Source ) )
		{
			PushEntry( L, &g_Wrappers, Index );
			SetEntry( L, &g_Wrappers, Result, -1 );
			lua_pop( L, 1 );
		}

		return 1;
	}

	int PushWrapper( lua_State* L, int Index )
	{
		Index = lua_absindex( L, Index );
		Closure* Function = CheckFunction( L, Index );
		lua_rawcheckstack( L, 8 );
		lua_pushcclosurek( L, Dispatch, nullptr, 0, Continue );
		Closure* Wrapper = clvalue( luaA_toobject( L, -1 ) );
		Wrapper->env = Function->env;
		luaC_objbarrier( L, Wrapper, Function->env );
		int Result = lua_gettop( L );
		PushBinding( L, Index, true );
		SetEntry( L, &g_Wrappers, Result, -1 );
		lua_pop( L, 1 );
		return 1;
	}

	int PushHook( lua_State* L, int TargetIndex, int HookIndex )
	{
		TargetIndex = lua_absindex( L, TargetIndex );
		HookIndex = lua_absindex( L, HookIndex );
		Closure* Target = CheckFunction( L, TargetIndex );
		Closure* Hook = CheckFunction( L, HookIndex );
		lua_rawcheckstack( L, 12 );
		PushClone( L, TargetIndex );
		int Original = lua_gettop( L );
		Closure* Snapshot = clvalue( luaA_toobject( L, Original ) );

		if ( Target == Hook )
			return 1;

		PushEntry( L, &g_Hooks, TargetIndex );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			PushClone( L, TargetIndex );
		}

		int FirstOriginal = lua_gettop( L );

		if ( Target->isC )
		{
			if ( Hook->isC && Hook->c.f == Dispatch )
				PushEntry( L, &g_Wrappers, HookIndex );
			else
				PushBinding( L, HookIndex, false );

			if ( !lua_istable( L, -1 ) )
				luaL_error( L, "closure target is unavailable" );

			int Binding = lua_gettop( L );
			int Wrappers = PrepareEntry( L, &g_Wrappers, TargetIndex );
			int Hooks = PrepareEntry( L, &g_Hooks, TargetIndex );
			PreserveFrames( L, Target, Snapshot );
			CommitEntry( L, Wrappers, TargetIndex, Binding );
			CommitEntry( L, Hooks, TargetIndex, FirstOriginal );
			InvalidateDirectCalls( L, Target );
			Target->env->safeenv = false;
			L->gt->safeenv = false;
			Target->c.f = Dispatch;
			Target->c.cont = Continue;
		}
		else
		{
			PushBridge( L, Target, HookIndex );
			Closure* Bridge = clvalue( luaA_toobject( L, -1 ) );
			lua_pushboolean( L, true );
			int Binding = lua_gettop( L );
			int Wrappers = PrepareEntry( L, &g_Wrappers, TargetIndex );
			int Hooks = PrepareEntry( L, &g_Hooks, TargetIndex );
			PreserveFrames( L, Target, Snapshot );
			CommitEntry( L, Wrappers, TargetIndex, Binding );
			CommitEntry( L, Hooks, TargetIndex, FirstOriginal );
			Target->l.p = Bridge->l.p;
			Target->stacksize = Bridge->stacksize;
			Target->preload = 0;
			luaC_objbarrier( L, Target, Bridge->l.p );
		}

		lua_pushvalue( L, Original );
		return 1;
	}

	int IsHooked( lua_State* L )
	{
		CheckFunction( L, 1 );
		lua_rawcheckstack( L, 3 );
		PushEntry( L, &g_Hooks, 1 );
		lua_pushboolean( L, lua_isfunction( L, -1 ) );
		return 1;
	}

	int Restore( lua_State* L, int Index )
	{
		Index = lua_absindex( L, Index );
		Closure* Target = CheckFunction( L, Index );
		lua_rawcheckstack( L, 10 );
		PushEntry( L, &g_Hooks, Index );

		if ( !lua_isfunction( L, -1 ) )
			luaL_error( L, "restorefunction: function is not hooked" );

		int OriginalIndex = lua_gettop( L );
		Closure* Original = clvalue( luaA_toobject( L, OriginalIndex ) );
		PushClone( L, Index );
		Closure* Snapshot = clvalue( luaA_toobject( L, -1 ) );
		PushEntry( L, &g_Wrappers, OriginalIndex );
		int Binding = lua_gettop( L );
		int Wrappers = PrepareEntry( L, &g_Wrappers, Index );

		int Hooks = PrepareEntry( L, &g_Hooks, Index );
		lua_pushnil( L );
		int Empty = lua_gettop( L );
		PreserveFrames( L, Target, Snapshot );

		CommitEntry( L, Wrappers, Index, Binding );

		CommitEntry( L, Hooks, Index, Empty );
		Copy( L, Target, Original );
		return 0;
	}
}

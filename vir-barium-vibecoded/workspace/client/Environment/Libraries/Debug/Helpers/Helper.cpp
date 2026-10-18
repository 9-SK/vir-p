#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "Helper.h"
#include "client/Environment/Libraries/Stack/Helpers/Helper.h"
#include "client/Environment/Libraries/Closures/Helpers/Helpers.h"
#include "core/Execution/Execution.h"
#include <Luau/Bytecode.h>
#include <Luau/BytecodeUtils.h>
#include <VM/src/lapi.h>
#include <VM/src/ldebug.h>
#include <VM/src/ldo.h>
#include <VM/src/lfunc.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <vector>

namespace Debug::Helpers
{
	namespace
	{
		struct ProtoContext
		{
			lua_State* State;
			Proto* Prototype;
			int Table = 0;
			int Count = 0;
		};

		int CheckInteger( lua_State* L, int Index )
		{
			luaL_checktype( L, Index, LUA_TNUMBER );
			double Value = lua_tonumber( L, Index );
			if ( !std::isfinite( Value ) || std::floor( Value ) != Value || Value < ( std::numeric_limits<int>::min )( ) || Value > ( std::numeric_limits<int>::max )( ) )
				luaL_argerror( L, Index, "expected an integer" );
			return static_cast<int>( Value );
		}

		int CheckIndex( lua_State* L, int Argument, int Count )
		{
			int Index = CheckInteger( L, Argument );
			if ( Index < 1 || Index > Count )
				luaL_argerror( L, Argument, "index out of range" );
			return Index - 1;
		}

		CallInfo* FindFrame( lua_State* L, int Argument, lua_State* Thread = nullptr )
		{
			if ( !Thread )
				Thread = L;
			CallInfo* Frame = Oth::Helpers::Frame( Thread, CheckInteger( L, Argument ) );
			if ( Frame )
				return Frame;
			luaL_argerror( L, Argument, "stack level out of range" );
		}

		CallInfo* CheckFrame( lua_State* L, int Argument, lua_State* Thread = nullptr )
		{
			CallInfo* Frame = FindFrame( L, Argument, Thread );
			if ( !isLua( Frame ) )
				luaL_argerror( L, Argument, "C closures are not supported" );
			return Frame;
		}

		Closure* CheckFunction( lua_State* L )
		{
			lua_rawcheckstack( L, 4 );
			if ( lua_type( L, 1 ) == LUA_TNUMBER )
			{
				Closure* Function = ci_func( CheckFrame( L, 1 ) );
				luaC_threadbarrier( L );
				setclvalue( L, L->top, Function );
				L->top++;
				return Function;
			}
			luaL_checktype( L, 1, LUA_TFUNCTION );
			Closure* Function = clvalue( luaA_toobject( L, 1 ) );
			if ( Function->isC )
				luaL_argerror( L, 1, "C closures are not supported" );
			return Function;
		}

		int Inactive( lua_State* L )
		{
			luaL_error( L, "inactive prototype cannot be called" );
		}

		Proto* Resolve( Closure* Function )
		{
			Proto* Prototype = Function->l.p;
			if ( Prototype->sizek == 1 && Prototype->sizep == 1 && iscfunction( &Prototype->k[ 0 ] ) && clvalue( &Prototype->k[ 0 ] )->c.f == Inactive )
				return Prototype->p[ 0 ];
			return Prototype;
		}

		void PushValue( lua_State* L, const TValue* Value )
		{
			luaC_threadbarrier( L );
			setobj2s( L, L->top, Value );
			L->top++;
		}

		bool IsConstant( const TValue* Value )
		{
			return ttisnil( Value ) || ttisboolean( Value ) || ttisnumber( Value ) || ttisstring( Value );
		}

		void PushConstant( lua_State* L, const TValue* Value )
		{
			if ( IsConstant( Value ) )
				PushValue( L, Value );
			else
				lua_pushnil( L );
		}

		std::vector<int> Imports( lua_State* L, Proto* Prototype )
		{
			std::array<uint8_t, 256> Opcodes;
			Opcodes.fill( 255 );
			const auto* Lookup = reinterpret_cast<const uint8_t*>( Update::LuaU::OpCodeLookupTable );
			for ( unsigned Index = 0; Index < LOP__COUNT; ++Index )
				Opcodes[ Lookup[ ( Index * 227 ) & 255 ] ] = static_cast<uint8_t>( Index );
			std::vector<int> Result( Prototype->sizek, -1 );
			for ( int Index = 0; Index < Prototype->sizecode; )
			{
				uint32_t Word = Prototype->code[ Index ];
				uint8_t Opcode = Opcodes[ LUAU_INSN_OP( Word ) ];
				if ( Opcode >= LOP__COUNT )
					luaL_error( L, "unsupported function opcode" );
				int Length = Luau::getOpLength( static_cast<LuauOpcode>( Opcode ) );
				if ( Length > Prototype->sizecode - Index )
					luaL_error( L, "truncated function instruction" );
				if ( Opcode == LOP_GETIMPORT )
				{
					unsigned Constant = ( Word >> 16 ) & 65535;
					if ( Constant >= static_cast<unsigned>( Prototype->sizek ) )
						luaL_error( L, "invalid import constant" );
					Result[ Constant ] = Index;
				}
				Index += Length;
			}
			return Result;
		}

		void PushInactive( lua_State* L, Closure* Parent, Proto* Source )
		{
			static const std::string Bytecode = Alpen::Execution::Compile( "return (\"inactive prototype\")()" );
			if ( luau_load( L, "=prototype", Bytecode.data( ), Bytecode.size( ), 0 ) != LUA_OK )
				lua_error( L );
			Proto* Prototype = clvalue( luaA_toobject( L, -1 ) )->l.p;
			if ( Prototype->sizek != 1 || Prototype->sizep != 0 )
				luaL_error( L, "invalid prototype wrapper" );
			lua_pushcfunction( L, Inactive, nullptr );
			setobj( L, &Prototype->k[ 0 ], luaA_toobject( L, -1 ) );
			luaC_barrier( L, Prototype, &Prototype->k[ 0 ] );
			lua_pop( L, 1 );
			Prototype->p = luaM_newarray( L, 1, Proto*, Prototype->memcat );
			Prototype->p[ 0 ] = Source;
			Prototype->sizep = 1;
			luaC_objbarrier( L, Prototype, Source );
			Prototype->debugname = static_cast<TString*>( Source->debugname );
			Prototype->source = static_cast<TString*>( Source->source );
			Prototype->linedefined = Source->linedefined;
			Prototype->userdata = static_cast<uint64_t*>( Source->userdata );
			if ( Prototype->debugname )
				luaC_objbarrier( L, Prototype, static_cast<TString*>( Prototype->debugname ) );
			if ( Prototype->source )
				luaC_objbarrier( L, Prototype, static_cast<TString*>( Prototype->source ) );
			Prototype->nups = Source->nups;
			Prototype->numparams = Source->numparams;
			Prototype->is_vararg = Source->is_vararg;
			Prototype->maxstacksize = ( std::max ) ( Prototype->maxstacksize, Source->numparams );
			const auto* Opcodes = reinterpret_cast<const uint8_t*>( Update::LuaU::OpCodeLookupTable );
			if ( LUAU_INSN_OP( Prototype->code[ 0 ] ) == Opcodes[ ( LOP_PREPVARARGS * 227 ) & 255 ] )
				Prototype->code[ 0 ] = Source->is_vararg ? Opcodes[ ( LOP_PREPVARARGS * 227 ) & 255 ] | ( uint32_t( Source->numparams ) << 8 ) : Opcodes[ ( LOP_NOP * 227 ) & 255 ];
			Closure* Function = luaF_newLclosure( L, Source->nups, Parent->env, Prototype );
			setclvalue( L, L->top - 1, Function );
		}

		bool CollectProtos( void* Data, lua_Page*, GCObject* Object )
		{
			auto* Context = static_cast<ProtoContext*>( Data );
			if ( Object->gch.tt != LUA_TFUNCTION || isdead( Context->State->global, Object ) )
				return false;
			Closure* Function = gco2cl( Object );
			if ( Function->isC || Function->preload || Function->l.p != Context->Prototype )
				return false;
			++Context->Count;
			if ( Context->Table )
			{
				lua_State* L = Context->State;
				luaC_threadbarrier( L );
				setclvalue( L, L->top, Function );
				L->top++;
				lua_rawseti( L, Context->Table, Context->Count );
			}
			return false;
		}

		TValue* Upvalue( Closure* Function, int Index )
		{
			TValue* Value = &Function->l.uprefs[ Index ];
			return ttisupval( Value ) ? upvalue( Value )->v : Value;
		}

		bool BarrierThread( void* Data, lua_Page*, GCObject* Object )
		{
			if ( Object->gch.tt != LUA_TTHREAD )
				return false;
			lua_State* Thread = gco2th( Object );
			uintptr_t Slot = reinterpret_cast<uintptr_t>( Data );
			if ( Slot >= reinterpret_cast<uintptr_t>( Thread->stack ) && Slot < reinterpret_cast<uintptr_t>( Thread->stack_last ) )
			{
				luaC_threadbarrier( Thread );
			}
			return false;
		}
	}

	int GetConstant( lua_State* L )
	{
		int Index = CheckInteger( L, 2 );
		if ( Index < 1 )
			luaL_argerror( L, 2, "index out of range" );
		Proto* Prototype = Resolve( CheckFunction( L ) );
		if ( Index > Prototype->sizek || Imports( L, Prototype )[ Index - 1 ] >= 0 )
			lua_pushnil( L );
		else
			PushConstant( L, &Prototype->k[ Index - 1 ] );
		return 1;
	}

	int GetInfo( lua_State* L )
	{
		lua_rawcheckstack( L, 4 );
		const char* Options = luaL_optstring( L, 2, "slnuaf" );
		int Level = -1;
		lua_State* Thread = L;
		if ( lua_type( L, 1 ) == LUA_TNUMBER )
		{
			CallInfo* Frame = Oth::Helpers::Frame( Thread, CheckInteger( L, 1 ) );
			if ( !Frame )
				luaL_argerror( L, 1, "stack level out of range" );
			Level = static_cast<int>( Thread->ci - Frame );
		}
		else
		{
			luaL_checktype( L, 1, LUA_TFUNCTION );
			lua_pushvalue( L, 1 );
		}
		for ( const char* Option = Options; *Option; ++Option )
			if ( !std::strchr( "sSlunaf", *Option ) )
				luaL_argerror( L, 2, "invalid option" );
		lua_Debug Info{ };
		if ( !lua_getinfo( Thread, Level, "slnuaf", &Info ) )
			luaL_argerror( L, 1, "stack level out of range" );
		if ( Thread != L )
			lua_xmove( Thread, L, 1 );
		int Function = lua_gettop( L );
		lua_createtable( L, 0, 9 );
		if ( std::strchr( Options, 's' ) || std::strchr( Options, 'S' ) )
		{
			lua_pushstring( L, Info.source );
			lua_setfield( L, -2, "source" );
			lua_pushstring( L, Info.short_src );
			lua_setfield( L, -2, "short_src" );
			lua_pushstring( L, Info.what );
			lua_setfield( L, -2, "what" );
			lua_pushinteger( L, Info.linedefined );
			lua_setfield( L, -2, "linedefined" );
		}
		if ( std::strchr( Options, 'l' ) )
		{
			lua_pushinteger( L, Info.currentline );
			lua_setfield( L, -2, "currentline" );
		}
		if ( std::strchr( Options, 'n' ) )
		{
			lua_pushstring( L, Info.name ? Info.name : "" );
			lua_setfield( L, -2, "name" );
		}
		if ( std::strchr( Options, 'u' ) || std::strchr( Options, 'a' ) )
		{
			lua_pushinteger( L, Info.nupvals );
			lua_setfield( L, -2, "nups" );
			lua_pushinteger( L, Info.nparams );
			lua_setfield( L, -2, "numparams" );
			lua_pushinteger( L, Info.isvararg );
			lua_setfield( L, -2, "is_vararg" );
		}
		if ( std::strchr( Options, 'f' ) )
		{
			lua_pushvalue( L, Function );
			lua_setfield( L, -2, "func" );
		}
		return 1;
	}

	int GetConstants( lua_State* L )
	{
		Proto* Prototype = Resolve( CheckFunction( L ) );
		auto Cached = Imports( L, Prototype );
		lua_createtable( L, Prototype->sizek, 0 );
		for ( int Index = 0; Index < Prototype->sizek; ++Index )
		{
			if ( Cached[ Index ] >= 0 )
				lua_pushnil( L );
			else
				PushConstant( L, &Prototype->k[ Index ] );
			lua_rawseti( L, -2, Index + 1 );
		}
		return 1;
	}

	int GetProto( lua_State* L )
	{
		if ( !lua_isnoneornil( L, 3 ) )
			luaL_checktype( L, 3, LUA_TBOOLEAN );
		bool Active = lua_toboolean( L, 3 );
		Closure* Function = CheckFunction( L );
		Proto* Prototype = Resolve( Function );
		Proto* Child = Prototype->p[ CheckIndex( L, 2, Prototype->sizep ) ];
		if ( !Active )
			PushInactive( L, Function, Child );
		else
		{
			luaC_checkGC( L );
			ProtoContext Context{ L, Child };
			luaM_visitgco( L, &Context, CollectProtos );
			lua_createtable( L, Context.Count, 0 );
			Context.Table = lua_gettop( L );
			Context.Count = 0;
			luaM_visitgco( L, &Context, CollectProtos );
		}
		return 1;
	}

	int GetProtos( lua_State* L )
	{
		Closure* Function = CheckFunction( L );
		Proto* Prototype = Resolve( Function );
		lua_createtable( L, Prototype->sizep, 0 );
		for ( int Index = 0; Index < Prototype->sizep; ++Index )
		{
			PushInactive( L, Function, Prototype->p[ Index ] );
			lua_rawseti( L, -2, Index + 1 );
		}
		return 1;
	}

	int GetStack( lua_State* L )
	{
		bool Foreign = lua_isthread( L, 1 );
		lua_State* Thread = Foreign ? lua_tothread( L, 1 ) : L;
		if ( Thread->global != L->global )
			luaL_argerror( L, 1, "thread belongs to a different Lua state" );
		int Argument = Foreign ? 2 : 1;
		bool Single = !lua_isnoneornil( L, Argument + 1 );
		int Slot = Single ? CheckInteger( L, Argument + 1 ) : 0;
		lua_rawcheckstack( L, 4 );
		CallInfo* Frame = Oth::Helpers::Frame( Thread, CheckInteger( L, Argument ) );
		if ( !Frame || !isLua( Frame ) )
			luaL_argerror( L, Argument, "Lua stack level expected" );
		if ( Frame->flags & LUA_CALLINFO_NATIVE )
			luaL_error( L, "native stack frame is unavailable" );
		int Count = static_cast<int>( Frame->top - Frame->base );
		if ( Single )
		{
			if ( Slot < 1 || Slot > Count )
				luaL_argerror( L, Argument + 1, "index out of range" );
			PushValue( L, &Frame->base[ Slot - 1 ] );
		}
		else
		{
			ptrdiff_t Base = savestack( Thread, Frame->base );
			ptrdiff_t End = savestack( L, Thread == L ? ( std::max ) ( L->top, Frame->top ) : L->top );
			lua_rawcheckstack( L, static_cast<int>( restorestack( L, End ) - L->top ) + Count + 3 );
			L->top = restorestack( L, End );
			int First = lua_gettop( L ) + 1;
			for ( int Index = 0; Index < Count; ++Index )
				PushValue( L, &restorestack( Thread, Base )[ Index ] );
			lua_createtable( L, Count, 0 );
			for ( int Index = 0; Index < Count; ++Index )
			{
				lua_pushvalue( L, First + Index );
				lua_rawseti( L, -2, Index + 1 );
			}
		}
		return 1;
	}

	int GetUpvalue( lua_State* L )
	{
		Closure* Function = CheckFunction( L );
		PushValue( L, Upvalue( Function, CheckIndex( L, 2, Function->nupvalues ) ) );
		return 1;
	}

	int GetUpvalues( lua_State* L )
	{
		Closure* Function = CheckFunction( L );
		lua_createtable( L, Function->nupvalues, 0 );
		for ( int Index = 0; Index < Function->nupvalues; ++Index )
		{
			PushValue( L, Upvalue( Function, Index ) );
			lua_rawseti( L, -2, Index + 1 );
		}
		return 1;
	}

	int SetConstant( lua_State* L )
	{
		luaL_checkany( L, 3 );
		Proto* Prototype = Resolve( CheckFunction( L ) );
		int Index = CheckIndex( L, 2, Prototype->sizek );
		const TValue* Value = luaA_toobject( L, 3 );
		TValue* Constant = &Prototype->k[ Index ];
		auto Cached = Imports( L, Prototype );
		if ( !IsConstant( Value ) )
			luaL_argerror( L, 3, "expected a number, string, boolean or nil" );
		if ( !IsConstant( Constant ) || Cached[ Index ] >= 0 )
			luaL_argerror( L, 2, "constant is reserved by the VM" );
		if ( ttype( Constant ) != ttype( Value ) )
			luaL_argerror( L, 3, "constant type must match" );
		if ( Prototype->execdata )
		{
			if ( !L->global->ecb.disable )
				luaL_error( L, "native prototype cannot be modified" );
			L->global->ecb.disable( L, Prototype );
		}
		setobj( L, Constant, Value );
		luaC_barrier( L, Prototype, Constant );
		for ( int Item = 0; Item < Prototype->sizek; ++Item )
		{
			if ( Cached[ Item ] < 0 )
				continue;
			uint32_t Import = Prototype->code[ Cached[ Item ] + 1 ];
			unsigned Count = Import >> 30;
			if ( int( ( Import >> 20 ) & 1023 ) == Index || ( Count > 1 && int( ( Import >> 10 ) & 1023 ) == Index ) || ( Count > 2 && int( Import & 1023 ) == Index ) )
				setnilvalue( &Prototype->k[ Item ] );
		}
		return 0;
	}

	int SetStack( lua_State* L )
	{
		bool Foreign = lua_isthread( L, 1 );
		lua_State* Thread = Foreign ? lua_tothread( L, 1 ) : L;
		if ( Thread->global != L->global )
			luaL_argerror( L, 1, "thread belongs to a different Lua state" );
		int Argument = Foreign ? 2 : 1;
		luaL_checkany( L, Argument + 2 );
		CallInfo* Frame = Oth::Helpers::Frame( Thread, CheckInteger( L, Argument ) );
		if ( !Frame || !isLua( Frame ) )
			luaL_argerror( L, Argument, "Lua stack level expected" );
		if ( Frame->flags & LUA_CALLINFO_NATIVE )
			luaL_error( L, "native stack frame is unavailable" );
		int Index = CheckIndex( L, Argument + 1, static_cast<int>( Frame->top - Frame->base ) );
		TValue* Slot = &Frame->base[ Index ];
		for ( CallInfo* Active = Frame + 1; Active <= Thread->ci; ++Active )
			if ( Slot == Active->func )
				luaL_argerror( L, Argument + 1, "cannot replace an active call frame" );
		luaC_threadbarrier( Thread );
		setobj( Thread, Slot, luaA_toobject( L, Argument + 2 ) );
		return 0;
	}

	int SetUpvalue( lua_State* L )
	{
		luaL_checkany( L, 3 );
		Closure* Function = CheckFunction( L );
		int Index = CheckIndex( L, 2, Function->nupvalues );
		TValue* Reference = &Function->l.uprefs[ Index ];
		const TValue* Value = luaA_toobject( L, 3 );
		setobj( L, Upvalue( Function, Index ), Value );
		if ( ttisupval( Reference ) )
		{
			UpVal* Up = upvalue( Reference );
			if ( upisopen( Up ) )
			{
				luaM_visitgco( L, Up->v, BarrierThread );
			}
			else
			{
				luaC_barrier( L, Up, Value );
			}
		}
		else
		{
			luaC_barrier( L, Function, Value );
		}
		return 0;
	}
}

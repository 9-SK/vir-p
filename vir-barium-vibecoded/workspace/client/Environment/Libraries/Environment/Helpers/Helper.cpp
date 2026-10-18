#include "Helper.h"
#include "client/Environment/Libraries/Closures/Helpers/Helpers.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <VM/src/ltable.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Libraries::Environment::Helpers
{
	namespace
	{
		struct GCContext
		{
			lua_State* State;
			LuaTable* Result = nullptr;
			size_t Count = 0;
			size_t Capacity = 0;
			int Table = 0;
			bool Tables = false;
			int Type = 0;
		};

		bool Collect( void* Context, lua_Page*, GCObject* Object )
		{
			auto* Objects = static_cast<GCContext*>( Context );
			lua_State* L = Objects->State;
			int Type = Object->gch.tt;

			if ( Objects->Type && Objects->Type != Type )
				return false;

			if ( isdead( L->global, Object ) || ( Type != LUA_TFUNCTION && Type != LUA_TUSERDATA && !( Objects->Tables && Type == LUA_TTABLE ) ) )
				return false;

			if ( Type == LUA_TTABLE && gco2h( Object ) == Objects->Result )
				return false;

			++Objects->Count;

			if ( Objects->Table && Objects->Count <= Objects->Capacity )
			{
				L->top->value.gc = Object;
				L->top->tt = Type;
				L->top++;
				lua_rawseti( L, Objects->Table, static_cast<int>( Objects->Count ) );
			}

			return false;
		}

		struct FilterOptions
		{
			TString* Name = nullptr;
			TString* Hash = nullptr;
			LuaTable* Constants = nullptr;
			LuaTable* Upvalues = nullptr;
			LuaTable* Keys = nullptr;
			LuaTable* Values = nullptr;
			LuaTable* Pairs = nullptr;
			LuaTable* Metatable = nullptr;
			bool IgnoreExecutor = true;
		};

		const TValue* ReadOption( lua_State* L, const char* Name, int Type )
		{
			lua_pushstring( L, Name );
			lua_rawget( L, 2 );

			if ( !lua_isnil( L, -1 ) && lua_type( L, -1 ) != Type )
				luaL_error( L, "filtergc: %s must be a %s", Name, lua_typename( L, Type ) );

			return luaA_toobject( L, -1 );
		}

		TString* ReadString( lua_State* L, const char* Name )
		{
			const TValue* Value = ReadOption( L, Name, LUA_TSTRING );
			return ttisnil( Value ) ? nullptr : tsvalue( Value );
		}

		LuaTable* ReadTable( lua_State* L, const char* Name )
		{
			const TValue* Value = ReadOption( L, Name, LUA_TTABLE );
			return ttisnil( Value ) ? nullptr : hvalue( Value );
		}

		template <typename Predicate>
		bool EveryEntry( LuaTable* Table, Predicate Match )
		{
			if ( !Table )
				return true;

			for ( int Index = 0; Index < Table->sizearray; ++Index )
			{
				const TValue* Value = &Table->array[ Index ];

				if ( ttisnil( Value ) )
					continue;

				TValue Key{ };
				setnvalue( &Key, Index + 1 );

				if ( !Match( &Key, Value ) )
					return false;
			}

			for ( int Index = 0; Index < sizenode( Table ); ++Index )
			{
				LuaNode* Node = gnode( Table, Index );

				if ( ttisnil( gval( Node ) ) )
					continue;

				TValue Key{ };
				Key.value = Node->key.value;
				std::memcpy( Key.extra, Node->key.extra, sizeof( Key.extra ) );
				Key.tt = Node->key.tt;

				if ( !Match( &Key, gval( Node ) ) )
					return false;
			}

			return true;
		}

		bool EqualValue( const TValue* Left, const TValue* Right )
		{
			if ( ttisnumber( Left ) && ttisnumber( Right ) && std::isnan( nvalue( Left ) ) && std::isnan( nvalue( Right ) ) )
				return true;

			return luaO_rawequalObj( Left, Right ) != 0;
		}

		bool HasValue( LuaTable* Table, const TValue* Value )
		{
			return !EveryEntry( Table, [ & ]( const TValue*, const TValue* Item )
								{
									return !EqualValue( Item, Value );
								} );
		}

		bool MatchesTable( LuaTable* Table, const FilterOptions& Options )
		{
			if ( Options.Metatable && Table->metatable != Options.Metatable )
				return false;

			return EveryEntry( Options.Keys, [ & ]( const TValue*, const TValue* Key )
							   {
								   return !ttisnil( luaH_get( Table, Key ) );
							   } ) &&
				   EveryEntry( Options.Values, [ & ]( const TValue*, const TValue* Value )
							   {
								   return HasValue( Table, Value );
							   } ) &&
				   EveryEntry( Options.Pairs, [ & ]( const TValue* Key, const TValue* Value )
							   {
								   return EqualValue( luaH_get( Table, Key ), Value );
							   } );
		}

		bool MatchesFunction( Closure* Function, const FilterOptions& Options )
		{
			if ( Options.IgnoreExecutor && Closures::Helpers::IsExecutor( Function ) )
				return false;

			if ( Function->isC && ( Options.Constants || Options.Upvalues || Options.Hash ) )
				return false;

			if ( Options.Name )
			{
				TString* Name = Function->isC ? Function->c.debugname : static_cast<TString*>( Function->l.p->debugname );

				if ( !Name || Name->len != Options.Name->len || std::memcmp( Name->data, Options.Name->data, Name->len ) != 0 )
					return false;
			}

			return EveryEntry( Options.Constants, [ & ]( const TValue*, const TValue* Value )
							   {
								   Proto* Prototype = Function->l.p;

								   for ( int Index = 0; Index < Prototype->sizek; ++Index )
								   {
									   if ( EqualValue( &Prototype->k[ Index ], Value ) )
										   return true;
								   }

								   return false;
							   } ) &&
				   EveryEntry( Options.Upvalues, [ & ]( const TValue*, const TValue* Value )
							   {
								   for ( int Index = 0; Index < Function->nupvalues; ++Index )
								   {
									   const TValue* Upvalue = &Function->l.uprefs[ Index ];

									   if ( ttisupval( Upvalue ) )
										   Upvalue = upvalue( Upvalue )->v;

									   if ( EqualValue( Upvalue, Value ) )
										   return true;
								   }

								   return false;
							   } );
		}

		bool MatchesHash( Closure* Function, TString* Expected, std::unordered_map<Proto*, std::string>& Hashes )
		{
			auto Found = Hashes.find( Function->l.p );

			if ( Found == Hashes.end( ) )
			{
				std::string Value;

				try
				{
					Value = Closures::Helpers::FunctionHash( Function );
				}
				catch ( const std::runtime_error& )
				{
					return false;
				}

				Found = Hashes.emplace( Function->l.p, std::move( Value ) ).first;
			}

			return Expected->len == Found->second.size( ) && std::memcmp( Expected->data, Found->second.data( ), Expected->len ) == 0;
		}
	}

	int PushObjects( lua_State* L, int Type, bool Tables )
	{
		lua_rawcheckstack( L, 2 );
		GCContext Objects{ L };
		Objects.Type = Type;
		Objects.Tables = Tables;
		luaM_visitgco( L, &Objects, Collect );

		if ( Objects.Count > static_cast<size_t>( std::numeric_limits<int>::max( ) ) )
			luaL_error( L, "too many garbage collector objects" );

		lua_createtable( L, static_cast<int>( Objects.Count ), 0 );
		Objects.Result = hvalue( luaA_toobject( L, -1 ) );
		Objects.Table = lua_gettop( L );
		Objects.Capacity = Objects.Count;
		Objects.Count = 0;
		luaC_threadbarrier( L );
		luaM_visitgco( L, &Objects, Collect );

		if ( Objects.Count > Objects.Capacity )
			luaL_error( L, "garbage collector changed during enumeration" );

		return 1;
	}

	int PushGC( lua_State* L )
	{
		if ( !lua_isnoneornil( L, 1 ) )
			luaL_checktype( L, 1, LUA_TBOOLEAN );

		return PushObjects( L, 0, lua_toboolean( L, 1 ) != 0 );
	}

	int PushFiltered( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TSTRING );
		luaL_checktype( L, 2, LUA_TTABLE );

		if ( !lua_isnoneornil( L, 3 ) )
			luaL_checktype( L, 3, LUA_TBOOLEAN );

		bool ReturnOne = lua_toboolean( L, 3 );
		size_t Length = 0;
		const char* TypeName = lua_tolstring( L, 1, &Length );
		bool Functions = Length == 8 && std::memcmp( TypeName, "function", 8 ) == 0;

		if ( !Functions && !( Length == 5 && std::memcmp( TypeName, "table", 5 ) == 0 ) )
			luaL_argerror( L, 1, "expected 'function' or 'table'" );

		lua_rawcheckstack( L, 12 );
		FilterOptions Options;

		if ( Functions )
		{
			Options.Name = ReadString( L, "Name" );
			Options.Hash = ReadString( L, "Hash" );
			Options.Constants = ReadTable( L, "Constants" );
			Options.Upvalues = ReadTable( L, "Upvalues" );
			const TValue* Ignore = ReadOption( L, "IgnoreExecutor", LUA_TBOOLEAN );
			Options.IgnoreExecutor = ttisnil( Ignore ) || bvalue( Ignore );
		}
		else
		{
			Options.Keys = ReadTable( L, "Keys" );
			Options.Values = ReadTable( L, "Values" );
			Options.Pairs = ReadTable( L, "KeyValuePairs" );
			Options.Metatable = ReadTable( L, "Metatable" );
		}

		PushObjects( L, Functions ? LUA_TFUNCTION : LUA_TTABLE );
		int Candidates = lua_gettop( L );
		int Total = lua_objlen( L, Candidates );
		int Count = 0;
		std::unordered_map<Proto*, std::string> Hashes;

		for ( int Index = 1; Index <= Total; ++Index )
		{
			lua_rawgeti( L, Candidates, Index );
			const TValue* Value = luaA_toobject( L, -1 );
			bool Match = Functions ? MatchesFunction( clvalue( Value ), Options ) : MatchesTable( hvalue( Value ), Options );

			if ( Match && Options.Hash )
				Match = MatchesHash( clvalue( Value ), Options.Hash, Hashes );

			if ( Match && ReturnOne )
				return 1;

			if ( Match )
				lua_rawseti( L, Candidates, ++Count );
			else
				lua_pop( L, 1 );
		}

		if ( ReturnOne )
		{
			lua_pushnil( L );
			return 1;
		}

		for ( int Index = Count + 1; Index <= Total; ++Index )
		{
			lua_pushnil( L );
			lua_rawseti( L, Candidates, Index );
		}

		return 1;
	}
}

#include "client/Environment/Libraries/Oth/Helpers/Helper.h"
#include "Helper.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <VM/src/ltable.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <string>

namespace Stack::Helpers
{
	namespace
	{
		char g_Hidden;

		int Integer( lua_State* L, int Argument, int Default )
		{
			if ( lua_isnoneornil( L, Argument ) )
				return Default;
			luaL_checktype( L, Argument, LUA_TNUMBER );
			const double Value = lua_tonumber( L, Argument );
			if ( !std::isfinite( Value ) || std::floor( Value ) != Value || Value < 0 || Value > ( std::numeric_limits<int>::max )( ) )
				luaL_argerror( L, Argument, "Level out of range" );
			return static_cast<int>( Value );
		}

		void PushMarks( lua_State* L )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Hidden );
			if ( lua_istable( L, -1 ) )
				return;
			lua_pop( L, 1 );
			lua_newtable( L );
			lua_newtable( L );
			lua_pushliteral( L, "k" );
			lua_setfield( L, -2, "__mode" );
			lua_setmetatable( L, -2 );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Hidden );
		}

		Closure* Target( lua_State* L )
		{
			if ( lua_isfunction( L, 1 ) )
				return clvalue( luaA_toobject( L, 1 ) );
			if ( lua_type( L, 1 ) != LUA_TNUMBER )
				luaL_argerror( L, 1, "expected a function or stack level" );
			const int Level = Integer( L, 1, 1 );
			if ( Level >= L->ci - L->base_ci )
				luaL_error( L, "Level out of range" );
			return ci_func( L->ci - Level );
		}
	}

	bool Hidden( lua_State* L, Closure* Function )
	{
		const TValue* Marks = luaH_getp( hvalue( registry( L ) ), &g_Hidden, 0 );
		if ( !ttistable( Marks ) )
			return false;
		TValue Key;
		setclvalue( L, &Key, Function );
		const TValue* Value = luaH_get( hvalue( Marks ), &Key );
		return ttisboolean( Value ) && bvalue( Value );
	}

	int PhysicalLevel( lua_State* L, int Level )
	{
		for ( CallInfo* Frame = L->ci; Frame > L->base_ci; --Frame )
		{
			if ( !ttisfunction( Frame->func ) || Hidden( L, ci_func( Frame ) ) )
				continue;
			if ( Level-- == 0 )
				return static_cast<int>( L->ci - Frame );
		}
		return -1;
	}

	int SetHidden( lua_State* L )
	{
		if ( !lua_isnoneornil( L, 2 ) )
			luaL_checktype( L, 2, LUA_TBOOLEAN );
		lua_rawcheckstack( L, 4 );
		Closure* Function = Target( L );
		const bool Value = lua_isnoneornil( L, 2 ) || lua_toboolean( L, 2 );
		luaC_threadbarrier( L );
		setclvalue( L, L->top, Function );
		++L->top;
		const int Index = lua_gettop( L );
		PushMarks( L );
		lua_pushvalue( L, Index );
		if ( Value )
			lua_pushboolean( L, true );
		else
			lua_pushnil( L );
		lua_rawset( L, -3 );
		return 0;
	}

	int IsHidden( lua_State* L )
	{
		lua_pushboolean( L, Hidden( L, Target( L ) ) );
		return 1;
	}

	int Info( lua_State* L )
	{
		const bool HasThread = lua_isthread( L, 1 );
		lua_State* Thread = HasThread ? lua_tothread( L, 1 ) : L;
		const int Argument = HasThread ? 2 : 1;
		size_t Length = 0;
		const char* Options = luaL_checklstring( L, Argument + 1, &Length );
		bool Seen[ 256 ]{ };
		for ( size_t Index = 0; Index < Length; ++Index )
		{
			const unsigned char Option = Options[ Index ];
			if ( !Option || !std::strchr( "slnfa", Option ) )
				luaL_argerror( L, Argument + 1, "invalid option" );
			if ( Seen[ Option ] )
				luaL_argerror( L, Argument + 1, "duplicate option" );
			Seen[ Option ] = true;
		}
		int Level;
		if ( !HasThread && lua_isfunction( L, Argument ) )
			Level = -lua_gettop( L );
		else
		{
			luaL_checktype( L, Argument, LUA_TNUMBER );
			CallInfo* Active = Oth::Helpers::Frame( Thread, Integer( L, Argument, 0 ) );
			if ( !Active )
				return 0;
			Level = static_cast<int>( Thread->ci - Active );
		}
		lua_rawcheckstack( L, 8 );
		lua_rawcheckstack( Thread, 1 );
		lua_Debug Result{ };
		if ( !lua_getinfo( Thread, Level, Options, &Result ) )
			return 0;
		int Function = 0;
		if ( Seen[ 'f' ] )
		{
			if ( Thread != L )
				lua_xmove( Thread, L, 1 );
			Function = lua_gettop( L );
		}
		int Count = 0;
		for ( size_t Index = 0; Index < Length; ++Index )
		{
			switch ( Options[ Index ] )
			{
			case 's':
				lua_pushstring( L, Result.short_src );
				break;
			case 'l':
				lua_pushinteger( L, Result.currentline );
				break;
			case 'n':
				lua_pushstring( L, Result.name ? Result.name : "" );
				break;
			case 'f':
				lua_pushvalue( L, Function );
				break;
			case 'a':
				lua_pushinteger( L, Result.nparams );
				lua_pushboolean( L, Result.isvararg );
				++Count;
				break;
			}
			++Count;
		}
		return Count;
	}

	int Traceback( lua_State* L )
	{
		const bool HasThread = lua_isthread( L, 1 );
		lua_State* Thread = HasThread ? lua_tothread( L, 1 ) : L;
		const int Argument = HasThread ? 2 : 1;
		size_t Length = 0;
		const char* Message = luaL_optlstring( L, Argument, nullptr, &Length );
		const int Level = Integer( L, Argument + 1, Thread == L ? 1 : 0 );
		std::string Text;
		if ( Message )
		{
			Text.assign( Message, Length );
			Text += '\n';
		}
		CallInfo* Start = Oth::Helpers::Frame( Thread, Level );
		int Physical = Start ? static_cast<int>( Thread->ci - Start ) : -1;
		lua_Debug Frame{ };
		while ( Physical >= 0 && lua_getinfo( Thread, Physical, "sln", &Frame ) )
		{
			const CallInfo* Active = Thread->ci - Physical++;
			if ( !std::strcmp( Frame.what, "C" ) || Hidden( Thread, ci_func( Active ) ) )
				continue;
			Text += Frame.short_src;
			if ( Frame.currentline > 0 )
				Text += ':' + std::to_string( Frame.currentline );
			if ( Frame.name )
				Text += std::string( " function " ) + Frame.name;
			Text += '\n';
		}
		lua_pushlstring( L, Text.data( ), Text.size( ) );
		return 1;
	}

	int GetEnvironment( lua_State* L )
	{
		lua_rawcheckstack( L, 2 );
		if ( lua_isfunction( L, 1 ) )
			lua_pushvalue( L, 1 );
		else
		{
			lua_State* Thread = L;
			CallInfo* Active = Oth::Helpers::Frame( Thread, Integer( L, 1, 1 ) );
			lua_Debug Frame{ };
			if ( !Active || !lua_getinfo( Thread, static_cast<int>( Thread->ci - Active ), "f", &Frame ) )
				luaL_argerror( L, 1, "Level out of range" );
			if ( Thread != L )
				lua_xmove( Thread, L, 1 );
		}
		if ( lua_iscfunction( L, -1 ) )
			lua_pushvalue( L, LUA_GLOBALSINDEX );
		else
			lua_getfenv( L, -1 );
		lua_setsafeenv( L, -1, false );
		return 1;
	}

	int Error( lua_State* L )
	{
		const int Level = luaL_optinteger( L, 2, 1 );
		lua_settop( L, 1 );
		if ( lua_isstring( L, 1 ) && Level > 0 )
		{
			lua_State* Thread = L;
			CallInfo* Active = Oth::Helpers::Frame( Thread, Level );
			if ( Active )
			{
				luaL_where( Thread, static_cast<int>( Thread->ci - Active ) );
				if ( Thread != L )
					lua_xmove( Thread, L, 1 );
				lua_pushvalue( L, 1 );
				lua_concat( L, 2 );
			}
		}
		lua_error( L );
	}
}

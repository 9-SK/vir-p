#include "Transfer.h"
#include "Native.h"
#include <cstring>
#include <lua.h>
#include <stdexcept>

namespace Actors
{
	void Transfer::Account( size_t Bytes )
	{
		constexpr size_t Limit = 32 * 1024 * 1024;
		if ( Bytes > Limit - m_Bytes || ++m_Values > 65536 )
			throw std::runtime_error( "Actor arguments exceed transfer limits" );
		m_Bytes += Bytes;
	}

	Transfer::Transfer( lua_State* L, int First )
	{
		const int Top = lua_gettop( L );
		try
		{
			for ( int Index = First; Index <= Top; ++Index )
				m_Arguments.push_back( Read( L, Index, 0 ) );
		}
		catch ( ... )
		{
			lua_settop( L, Top );
			throw;
		}
	}

	Transfer::Value Transfer::Read( lua_State* L, int Index, size_t Depth )
	{
		if ( Depth > 128 || !lua_checkstack( L, 4 ) )
			throw std::runtime_error( "Actor argument nesting exceeds transfer limits" );
		Account( sizeof( Value ) );
		Index = lua_absindex( L, Index );
		Value Item;
		Item.Type = lua_type( L, Index );
		switch ( Item.Type )
		{
		case LUA_TNIL:
			break;
		case LUA_TBOOLEAN:
			Item.Number = lua_toboolean( L, Index );
			break;
		case LUA_TNUMBER:
			Item.Number = lua_tonumber( L, Index );
			break;
		case LUA_TVECTOR:
			std::memcpy( Item.Vector, lua_tovector( L, Index ), sizeof( Item.Vector ) );
			break;
		case LUA_TSTRING:
		case LUA_TBUFFER:
		{
			size_t Length = 0;
			const void* Data = Item.Type == LUA_TSTRING ? static_cast<const void*>( lua_tolstring( L, Index, &Length ) ) : lua_tobuffer( L, Index, &Length );
			Account( Length );
			Item.Bytes.assign( static_cast<const char*>( Data ), Length );
			break;
		}
		case LUA_TTABLE:
		{
			const void* Identity = lua_topointer( L, Index );
			auto Existing = m_Seen.find( Identity );
			if ( Existing != m_Seen.end( ) )
			{
				Item.Table = Existing->second;
				break;
			}
			Item.Table = m_Tables.size( );
			m_Seen.emplace( Identity, Item.Table );
			m_Tables.emplace_back( );
			lua_pushnil( L );
			while ( lua_next( L, Index ) )
			{
				Value Key = Read( L, -2, Depth + 1 );
				Value Entry = Read( L, -1, Depth + 1 );
				m_Tables[ Item.Table ].emplace_back( std::move( Key ), std::move( Entry ) );
				lua_pop( L, 1 );
			}
			break;
		}
		case LUA_TUSERDATA:
			Item.Native = Native::ReadValue( L, Index );
			break;
		default:
			throw std::runtime_error( std::string( "cannot transfer Actor argument of type " ) + lua_typename( L, Item.Type ) );
		}
		return Item;
	}

	void Transfer::PushValue( lua_State* L, const Value& Item, int Tables ) const
	{
		switch ( Item.Type )
		{
		case LUA_TNIL:
			lua_pushnil( L );
			break;
		case LUA_TBOOLEAN:
			lua_pushboolean( L, Item.Number != 0 );
			break;
		case LUA_TNUMBER:
			lua_pushnumber( L, Item.Number );
			break;
		case LUA_TVECTOR:
			lua_pushvector( L, Item.Vector[ 0 ], Item.Vector[ 1 ], Item.Vector[ 2 ] );
			break;
		case LUA_TSTRING:
			lua_pushlstring( L, Item.Bytes.data( ), Item.Bytes.size( ) );
			break;
		case LUA_TBUFFER:
			std::memcpy( lua_newbuffer( L, Item.Bytes.size( ) ), Item.Bytes.data( ), Item.Bytes.size( ) );
			break;
		case LUA_TTABLE:
			lua_rawgeti( L, Tables, static_cast<int>( Item.Table + 1 ) );
			break;
		case LUA_TUSERDATA:
			Native::PushValue( L, Item.Native );
			break;
		}
	}

	void Transfer::Push( lua_State* L ) const
	{
		const int Top = lua_gettop( L );
		if ( !lua_checkstack( L, Count( ) + 5 ) )
			throw std::runtime_error( "Actor destination stack is full" );
		try
		{
			lua_createtable( L, static_cast<int>( m_Tables.size( ) ), 0 );
			const int Tables = lua_gettop( L );
			for ( size_t Index = 0; Index < m_Tables.size( ); ++Index )
			{
				lua_createtable( L, 0, static_cast<int>( m_Tables[ Index ].size( ) ) );
				lua_rawseti( L, Tables, static_cast<int>( Index + 1 ) );
			}
			for ( size_t Index = 0; Index < m_Tables.size( ); ++Index )
			{
				lua_rawgeti( L, Tables, static_cast<int>( Index + 1 ) );
				for ( const auto& [ Key, Entry ] : m_Tables[ Index ] )
				{
					PushValue( L, Key, Tables );
					PushValue( L, Entry, Tables );
					lua_rawset( L, -3 );
				}
				lua_pop( L, 1 );
			}
			for ( const auto& Item : m_Arguments )
				PushValue( L, Item, Tables );
			lua_remove( L, Tables );
		}
		catch ( ... )
		{
			lua_settop( L, Top );
			throw;
		}
	}

	int Transfer::Count( ) const
	{
		return static_cast<int>( m_Arguments.size( ) );
	}
}

#include "core/Roblox/Update.h"
#include "core/Scheduler/Helper.h"
#include "Helpers.h"
#include <Luau/Bytecode.h>
#include <Luau/BytecodeUtils.h>
#include <VM/src/lobject.h>
#include <VM/src/ltable.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace Closures::Helpers
{
	namespace
	{
		constexpr size_t MaximumHashBytes = 64 * 1024 * 1024;

		void Append( std::string& Bytes, const void* Data, size_t Length )
		{
			if ( Length > MaximumHashBytes - Bytes.size( ) )
				throw std::runtime_error( "function exceeds the hash size limit" );

			Bytes.append( static_cast<const char*>( Data ), Length );
		}

		template <typename T>
		void Append( std::string& Bytes, const T& Value )
		{
			Append( Bytes, &Value, sizeof( Value ) );
		}

		void EncodeProto( std::string& Bytes, Proto* Prototype, const std::array<uint8_t, 256>& Opcodes, int Depth );

		void EncodeValue( std::string& Bytes, const TValue* Value, const std::array<uint8_t, 256>& Opcodes, int Depth )
		{
			if ( Depth > 64 )
				throw std::runtime_error( "function constants exceed the nesting limit" );

			Append( Bytes, static_cast<uint8_t>( ttype( Value ) ) );

			switch ( ttype( Value ) )
			{
			case LUA_TNIL:
				break;
			case LUA_TBOOLEAN:
				Append( Bytes, static_cast<uint8_t>( bvalue( Value ) != 0 ) );
				break;
			case LUA_TNUMBER:
				Append( Bytes, nvalue( Value ) );
				break;
			case LUA_TINTEGER:
				Append( Bytes, lvalue( Value ) );
				break;
			case LUA_TVECTOR:
				Append( Bytes, vvalue( Value ), sizeof( float ) * LUA_VECTOR_SIZE );
				break;
			case LUA_TSTRING:
			{
				TString* String = tsvalue( Value );
				Append( Bytes, static_cast<uint32_t>( String->len ) );
				Append( Bytes, String->data, String->len );
				break;
			}
			case LUA_TFUNCTION:
				if ( clvalue( Value )->isC )
					throw std::runtime_error( "native function constant cannot be hashed" );
				EncodeProto( Bytes, clvalue( Value )->l.p, Opcodes, Depth + 1 );
				break;
			case LUA_TTABLE:
			{
				LuaTable* Table = hvalue( Value );
				std::vector<std::string> Entries;
				size_t Total = 0;
				auto Add = [ & ]( const TValue* Key, const TValue* Item )
				{
					std::string Entry;
					EncodeValue( Entry, Key, Opcodes, Depth + 1 );
					EncodeValue( Entry, Item, Opcodes, Depth + 1 );

					if ( Entry.size( ) > MaximumHashBytes - Total )
						throw std::runtime_error( "function table constant exceeds the hash size limit" );

					Total += Entry.size( );
					Entries.push_back( std::move( Entry ) );
				};

				for ( int Index = 0; Index < Table->sizearray; ++Index )
				{
					if ( !ttisnil( &Table->array[ Index ] ) )
					{
						TValue Key;
						setnvalue( &Key, Index + 1 );
						Add( &Key, &Table->array[ Index ] );
					}
				}

				for ( int Index = 0; Index < sizenode( Table ); ++Index )
				{
					LuaNode* Node = gnode( Table, Index );

					if ( !ttisnil( gval( Node ) ) )
					{
						TValue Key;
						Key.value = Node->key.value;
						std::memcpy( Key.extra, Node->key.extra, sizeof( Key.extra ) );
						Key.tt = Node->key.tt;
						Add( &Key, gval( Node ) );
					}
				}

				std::sort( Entries.begin( ), Entries.end( ) );
				Append( Bytes, static_cast<uint32_t>( Entries.size( ) ) );

				for ( const auto& Entry : Entries )
					Append( Bytes, Entry.data( ), Entry.size( ) );

				break;
			}
			default:
				throw std::runtime_error( "unsupported function constant" );
			}
		}

		void EncodeProto( std::string& Bytes, Proto* Prototype, const std::array<uint8_t, 256>& Opcodes, int Depth )
		{
			if ( Depth > 64 || Prototype->sizecode < 0 || Prototype->sizek < 0 || Prototype->sizep < 0 || static_cast<size_t>( Prototype->sizek ) > MaximumHashBytes / sizeof( TValue ) || static_cast<size_t>( Prototype->sizep ) > MaximumHashBytes / sizeof( Proto* ) || static_cast<size_t>( Prototype->sizecode ) > MaximumHashBytes / sizeof( Instruction ) )
				throw std::runtime_error( "invalid function prototype" );

			std::string Code;
			Code.reserve( static_cast<size_t>( Prototype->sizecode ) * sizeof( Instruction ) );
			std::string Imports( Prototype->sizek, '\0' );

			for ( int Index = 0; Index < Prototype->sizecode; )
			{
				Instruction Word = Prototype->code[ Index ];
				uint8_t Opcode = Opcodes[ Word & 255 ];

				if ( Opcode >= LOP__COUNT )
					throw std::runtime_error( "invalid function opcode" );

				int Length = Luau::getOpLength( static_cast<LuauOpcode>( Opcode ) );

				if ( Length <= 0 || Length > Prototype->sizecode - Index )
					throw std::runtime_error( "truncated function instruction" );

				Word = ( Word & ~Instruction( 255 ) ) | Opcode;

				if ( Opcode == LOP_GETIMPORT )
				{
					unsigned Constant = ( Word >> 16 ) & 65535;

					if ( Constant < Imports.size( ) )
						Imports[ Constant ] = 1;
				}

				switch ( Opcode )
				{
				case LOP_GETGLOBAL:
				case LOP_SETGLOBAL:
				case LOP_GETTABLEKS:
				case LOP_SETTABLEKS:
				case LOP_NAMECALL:
					Word &= 0x00ffffff;
					break;
				}

				Append( Code, Word );

				for ( int Auxiliary = 1; Auxiliary < Length; ++Auxiliary )
					Append( Code, Prototype->code[ Index + Auxiliary ] );

				Index += Length;
			}

			Append( Bytes, static_cast<uint32_t>( Prototype->sizek ) );

			for ( int Index = 0; Index < Prototype->sizek; ++Index )
			{
				if ( Imports[ Index ] )
					Append( Bytes, static_cast<uint8_t>( LUA_TNIL ) );
				else
					EncodeValue( Bytes, &Prototype->k[ Index ], Opcodes, Depth + 1 );
			}

			Append( Bytes, Prototype->numparams );
			Append( Bytes, Prototype->is_vararg );
			Append( Bytes, Prototype->nups );
			Append( Bytes, static_cast<uint32_t>( Prototype->sizecode ) );
			Append( Bytes, Code.data( ), Code.size( ) );
			Append( Bytes, static_cast<uint32_t>( Prototype->sizep ) );

			for ( int Index = 0; Index < Prototype->sizep; ++Index )
				EncodeProto( Bytes, Prototype->p[ Index ], Opcodes, Depth + 1 );
		}

		std::string Hash( const std::string& Bytes )
		{
			unsigned char Digest[ 48 ]{ };
			BCRYPT_ALG_HANDLE Algorithm = nullptr;
			BCRYPT_HASH_HANDLE Handle = nullptr;
			NTSTATUS Status = BCryptOpenAlgorithmProvider( &Algorithm, BCRYPT_SHA384_ALGORITHM, nullptr, 0 );

			if ( Status >= 0 )
				Status = BCryptCreateHash( Algorithm, &Handle, nullptr, 0, nullptr, 0, 0 );

			if ( Status >= 0 )
				Status = BCryptHashData( Handle, reinterpret_cast<unsigned char*>( const_cast<char*>( Bytes.data( ) ) ), static_cast<ULONG>( Bytes.size( ) ), 0 );

			if ( Status >= 0 )
				Status = BCryptFinishHash( Handle, Digest, sizeof( Digest ), 0 );

			if ( Handle )
				BCryptDestroyHash( Handle );

			if ( Algorithm )
				BCryptCloseAlgorithmProvider( Algorithm, 0 );

			if ( Status < 0 )
				throw std::runtime_error( "failed to hash function" );

			constexpr char Hex[] = "0123456789abcdef";
			std::string Result( sizeof( Digest ) * 2, '\0' );

			for ( size_t Index = 0; Index < sizeof( Digest ); ++Index )
			{
				Result[ Index * 2 ] = Hex[ Digest[ Index ] >> 4 ];
				Result[ Index * 2 + 1 ] = Hex[ Digest[ Index ] & 15 ];
			}

			return Result;
		}
	}

	std::string SerializeFunction( Closure* Function )
	{
		std::array<uint8_t, 256> Opcodes;
		Opcodes.fill( 255 );
		const auto* Lookup = reinterpret_cast<const uint8_t*>( Update::LuaU::OpCodeLookupTable );

		for ( unsigned Index = 0; Index < LOP__COUNT; ++Index )
			Opcodes[ Lookup[ ( Index * 227 ) & 255 ] ] = static_cast<uint8_t>( Index );

		std::string Bytes;
		EncodeProto( Bytes, Function->l.p, Opcodes, 0 );
		return Bytes;
	}

	std::string FunctionHash( Closure* Function )
	{
		return Hash( SerializeFunction( Function ) );
	}

	int PushHash( lua_State* L )
	{
		Closure* Function = CheckFunction( L, 1 );

		if ( Function->isC )
			luaL_argerror( L, 1, "lua function expected" );

		std::string Bytes;
		std::string Error;

		try
		{
			Bytes = SerializeFunction( Function );
		}
		catch ( const std::exception& Exception )
		{
			Error = Exception.what( );
		}

		if ( !Error.empty( ) )
			luaL_error( L, "%s", Error.c_str( ) );

		bool Asynchronous = Bytes.size( ) >= 32768 && lua_isyieldable( L );
		auto Work = [ Bytes = std::move( Bytes ) ]( ) -> Yielded
		{
			std::string Result;
			std::string Error;

			try
			{
				Result = Hash( Bytes );
			}
			catch ( const std::exception& Exception )
			{
				Error = Exception.what( );
			}

			return [ Result = std::move( Result ), Error = std::move( Error ) ]( lua_State* State )
			{
				if ( !Error.empty( ) )
					luaL_error( State, "%s", Error.c_str( ) );

				lua_pushlstring( State, Result.data( ), Result.size( ) );
				return 1;
			};
		};

		if ( Asynchronous )
			return Yielding::YieldExec( L, Work );

		return Work( )( L );
	}
}

#include "Helper.h"
#include "core/Scheduler/Helper.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <lua.h>
#include <lualib.h>
#include <lz4.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Encoding::Helpers
{
	namespace
	{
		constexpr size_t MaximumBytes = 64 * 1024 * 1024;
		constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		using Transform = std::string ( * )( std::string_view );

		std::string Encode( std::string_view Data )
		{
			std::string Result( ( Data.size( ) + 2 ) / 3 * 4, '=' );
			size_t Output = 0;

			for ( size_t Index = 0; Index < Data.size( ); Index += 3 )
			{
				uint32_t Value = static_cast<unsigned char>( Data[ Index ] ) << 16;
				size_t Count = ( std::min ) ( size_t( 3 ), Data.size( ) - Index );

				if ( Count > 1 )
					Value |= static_cast<unsigned char>( Data[ Index + 1 ] ) << 8;

				if ( Count > 2 )
					Value |= static_cast<unsigned char>( Data[ Index + 2 ] );

				Result[ Output++ ] = Alphabet[ Value >> 18 ];
				Result[ Output++ ] = Alphabet[ ( Value >> 12 ) & 63 ];
				Result[ Output++ ] = Count > 1 ? Alphabet[ ( Value >> 6 ) & 63 ] : '=';
				Result[ Output++ ] = Count > 2 ? Alphabet[ Value & 63 ] : '=';
			}

			return Result;
		}

		std::string Decode( std::string_view Data )
		{
			std::array<int, 256> Lookup;
			Lookup.fill( -1 );

			for ( int Index = 0; Index < 64; ++Index )
				Lookup[ static_cast<unsigned char>( Alphabet[ Index ] ) ] = Index;

			std::string Result;
			Result.reserve( Data.size( ) / 4 * 3 );
			uint32_t Value = 0;
			unsigned Bits = 0;
			size_t Symbols = 0;
			size_t Padding = 0;

			for ( unsigned char Character : Data )
			{
				if ( Character == ' ' || Character == '\r' || Character == '\n' || Character == '\t' )
					continue;

				if ( Character == '=' )
				{
					if ( ++Padding > 2 )
						throw std::runtime_error( "invalid Base64 padding" );

					continue;
				}

				int Digit = Lookup[ Character ];

				if ( Digit < 0 || Padding )
					throw std::runtime_error( "invalid Base64 data" );

				++Symbols;
				Value = ( Value << 6 ) | static_cast<unsigned>( Digit );
				Bits += 6;

				if ( Bits >= 8 )
				{
					Bits -= 8;
					Result.push_back( static_cast<char>( ( Value >> Bits ) & 255 ) );
				}
			}

			if ( Symbols % 4 == 1 || ( Padding && ( ( Symbols + Padding ) % 4 || Symbols % 4 == 0 ) ) || ( Bits && ( Value & ( ( 1u << Bits ) - 1 ) ) ) )
				throw std::runtime_error( "invalid Base64 padding" );

			if ( Result.size( ) > MaximumBytes )
				throw std::runtime_error( "decoded data exceeds the size limit" );

			return Result;
		}

		size_t ReadLength( std::string_view Data, size_t& Index, size_t Length )
		{
			if ( Length != 15 )
				return Length;

			unsigned Byte;

			do
			{
				if ( Index == Data.size( ) )
					throw std::runtime_error( "truncated LZ4 length" );

				Byte = static_cast<unsigned char>( Data[ Index++ ] );

				if ( Byte > MaximumBytes - Length )
					throw std::runtime_error( "decoded data exceeds the size limit" );

				Length += Byte;
			} while ( Byte == 255 );

			return Length;
		}

		size_t DecodedSize( std::string_view Data )
		{
			size_t Index = 0;
			size_t Total = 0;

			while ( Index < Data.size( ) )
			{
				unsigned Token = static_cast<unsigned char>( Data[ Index++ ] );
				size_t Literals = ReadLength( Data, Index, Token >> 4 );

				if ( Literals > Data.size( ) - Index )
					throw std::runtime_error( "truncated LZ4 literals" );

				if ( Literals > MaximumBytes - Total )
					throw std::runtime_error( "decoded data exceeds the size limit" );

				Index += Literals;
				Total += Literals;

				if ( Index == Data.size( ) )
					return Total;

				if ( Data.size( ) - Index < 2 )
					throw std::runtime_error( "truncated LZ4 match" );

				unsigned Distance = static_cast<unsigned char>( Data[ Index ] ) | ( static_cast<unsigned char>( Data[ Index + 1 ] ) << 8 );
				Index += 2;

				if ( !Distance || Distance > Total )
					throw std::runtime_error( "invalid LZ4 match distance" );

				size_t Match = ReadLength( Data, Index, Token & 15 ) + 4;

				if ( Match > MaximumBytes - Total )
					throw std::runtime_error( "decoded data exceeds the size limit" );

				Total += Match;
			}

			throw std::runtime_error( "truncated LZ4 block" );
		}

		std::string Compress( std::string_view Data )
		{
			int Capacity = LZ4_compressBound( static_cast<int>( Data.size( ) ) );
			std::string Result( Capacity, '\0' );
			int Length = LZ4_compress_default( Data.data( ), Result.data( ), static_cast<int>( Data.size( ) ), Capacity );

			if ( Length <= 0 )
				throw std::runtime_error( "LZ4 compression failed" );

			Result.resize( Length );
			return Result;
		}

		std::string Decompress( std::string_view Data )
		{
			size_t Length = DecodedSize( Data );
			std::string Result( Length, '\0' );
			int Decoded = LZ4_decompress_safe( Data.data( ), Result.data( ), static_cast<int>( Data.size( ) ), static_cast<int>( Length ) );

			if ( Decoded < 0 || static_cast<uint32_t>( Decoded ) != Length )
				throw std::runtime_error( "invalid LZ4 data" );

			return Result;
		}

		int PushResult( lua_State* L, Transform Function )
		{
			luaL_checktype( L, 1, LUA_TSTRING );
			size_t Length = 0;
			const char* Data = lua_tolstring( L, 1, &Length );
			size_t Limit = MaximumBytes;

			if ( Function == Decode )
				Limit = ( MaximumBytes + 2 ) / 3 * 4;
			else if ( Function == Decompress )
				Limit = static_cast<size_t>( LZ4_compressBound( static_cast<int>( MaximumBytes ) ) );

			if ( Length > Limit )
				luaL_argerror( L, 1, "encoding input exceeds the size limit" );

			bool Asynchronous = ( Length >= 32768 || ( Function == Decompress && Length >= 128 ) ) && lua_isyieldable( L );

			auto Work = [ Input = std::make_shared<std::string>( Data, Length ), Function ]( ) -> Yielded
			{
				std::string Result;
				std::string Error;

				try
				{
					Result = Function( *Input );
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

	int EncodeBase64( lua_State* L )
	{
		return PushResult( L, Encode );
	}

	int DecodeBase64( lua_State* L )
	{
		return PushResult( L, Decode );
	}

	int CompressLZ4( lua_State* L )
	{
		return PushResult( L, Compress );
	}

	int DecompressLZ4( lua_State* L )
	{
		return PushResult( L, Decompress );
	}
}

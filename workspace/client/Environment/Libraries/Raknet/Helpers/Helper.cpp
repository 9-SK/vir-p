#include "Helper.h"
#include "utils/Packets/Logger.h"
#include "utils/Packets/Protocol/PingItem.h"
#include <cstring>
#include <lua.h>
#include <lualib.h>

namespace Raknet::Helpers
{
	namespace
	{
		void Number( lua_State* L, const char* Name, double Value )
		{
			lua_pushnumber( L, Value );
			lua_setfield( L, -2, Name );
		}

		void Boolean( lua_State* L, const char* Name, bool Value )
		{
			lua_pushboolean( L, Value );
			lua_setfield( L, -2, Name );
		}

		void Text( lua_State* L, const char* Name, const std::string& Value )
		{
			lua_pushlstring( L, Value.data( ), Value.size( ) );
			lua_setfield( L, -2, Name );
		}

		int Limit( lua_State* L, int Index )
		{
			const double Value = luaL_optnumber( L, Index, 32 );
			luaL_argcheck( L, Value >= 1 && Value <= 256, Index, "limit must be an integer between 1 and 256" );
			const int Count = static_cast<int>( Value );
			luaL_argcheck( L, Value == Count, Index, "limit must be an integer between 1 and 256" );
			return Count;
		}

		void Push( lua_State* L, const Packets::Packet& Value )
		{
			lua_createtable( L, 0, 16 );
			Text( L, "Stage", Value.Source == Packets::Stage::Send ? "send" : "serialized" );
			Text( L, "TimeMs", std::to_string( Value.Time ) );
			Text( L, "Generation", std::to_string( Value.Generation ) );
			Number( L, "ThreadId", Value.Thread );
			Number( L, "Length", Value.Length );
			Number( L, "CapturedLength", Value.Captured );
			Boolean( L, "Truncated", Value.Captured < Value.Length );
			Text( L, "Summary", Packets::Logger::Format( Value ) );
			std::memcpy( lua_newbuffer( L, Value.Captured ), Value.Data.data( ), Value.Captured );
			lua_setfield( L, -2, "AsBuffer" );
			std::span<const uint8_t> Bytes( Value.Data.data( ), Value.Captured );
			if ( Value.Source == Packets::Stage::Send )
			{
				if ( !Bytes.empty( ) )
					Number( L, "PacketId", Bytes[ 0 ] );
				Number( L, "Receipt", Value.Receipt );
				Number( L, "Priority", Value.Priority );
				Number( L, "Reliability", Value.Reliability );
				Number( L, "Channel", Value.Channel );
				Boolean( L, "Broadcast", Value.Broadcast );
				if ( Bytes.size( ) < 2 || Bytes[ 0 ] != 131 )
					return;
				Bytes = Bytes.subspan( 1 );
			}
			Packets::Protocol::PingItem Ping;
			if ( !Packets::Protocol::PingDecoder::Decode( Bytes, Ping ) ||
				 ( Value.Source == Packets::Stage::PingItem && Ping.Bytes != Bytes.size( ) ) )
				return;
			lua_createtable( L, 0, 12 );
			Number( L, "ItemId", Ping.Item );
			Number( L, "Type", Ping.Type );
			Text( L, "Time", std::to_string( Ping.Time ) );
			Number( L, "FirstSerializeTime", Ping.FirstSerializeTime );
			Number( L, "SendStats", Ping.SendStats );
			Number( L, "ExtraStats", Ping.ExtraStats );
			Number( L, "Mode", Ping.Mode );
			if ( Ping.HasFps )
			{
				Number( L, "RenderFps", Ping.Fps[ 0 ] );
				Number( L, "PhysicsFps", Ping.Fps[ 1 ] );
				Number( L, "HeartbeatFps", Ping.Fps[ 2 ] );
			}
			if ( Ping.HasMarkers )
				Number( L, "VmMarkers", Ping.Markers );
			if ( Ping.HasMemory )
				Number( L, "MemoryMb", Ping.Memory );
			if ( Ping.HasTrace )
				Boolean( L, "TracePresent", Ping.Trace != 0 );
			lua_setfield( L, -2, "Ping" );
		}
	}

	int Read( lua_State* L )
	{
		const int Count = Limit( L, 1 );
		luaL_checkstack( L, 8, "packet inspection" );
		lua_createtable( L, Count, 0 );
		const auto Generation = Packets::g_Logger->Generation( );
		Packets::Packet Value;
		for ( int Index = 1; Index <= Count && Packets::g_Logger->Read( Value, Generation ); ++Index )
		{
			Push( L, Value );
			lua_rawseti( L, -2, Index );
		}
		return 1;
	}

	int Inspect( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TFUNCTION );
		const int Count = Limit( L, 2 );
		luaL_checkstack( L, 8, "packet inspection" );
		const auto Generation = Packets::g_Logger->Generation( );
		Packets::Packet Value;
		int Processed = 0;
		while ( Processed < Count && Packets::g_Logger->Read( Value, Generation ) )
		{
			lua_pushvalue( L, 1 );
			Push( L, Value );
			if ( lua_pcall( L, 1, 0, 0 ) != LUA_OK )
				lua_error( L );
			++Processed;
		}
		lua_pushinteger( L, Processed );
		return 1;
	}
}

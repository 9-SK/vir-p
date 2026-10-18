#include "Transport.h"
#include "Memory.h"
#include "core/Roblox/Update.h"
#include <array>
#include <cstring>
#include <mutex>

namespace Network
{
	namespace
	{
		std::mutex g_TransportMutex;

		bool Resolve( ) noexcept
		{
			if ( !Memory::Profile( ) )
				return false;
			std::array<uint8_t, 7> Instruction{ };
			const auto Address = Update::Roblox::Flags::TransportRegistration;
			if ( !Memory::Copy( Instruction.data( ), reinterpret_cast<void*>( Address ), Instruction.size( ) ) ||
				 Instruction[ 0 ] != 0x4c || Instruction[ 1 ] != 0x8d || Instruction[ 2 ] != 0x05 )
				return false;
			int32_t Relative = 0;
			std::memcpy( &Relative, Instruction.data( ) + 3, sizeof( Relative ) );
			return Address + Instruction.size( ) + Relative == Update::Roblox::Flags::UseRbxTransportClient;
		}
	}

	bool Transport::UseRaknet( std::string& Error )
	{
		std::lock_guard Lock( g_TransportMutex );
		if ( !Resolve( ) )
		{
			Error = "transport flag registration does not match this client";
			return false;
		}
		uint8_t Current = 0;
		const auto Address = Update::Roblox::Flags::UseRbxTransportClient;
		if ( !Memory::Read( Address, Current ) || Current > 1 )
		{
			Error = "transport flag is not a readable Boolean";
			return false;
		}
		const uint8_t Disabled = 0;
		SIZE_T Written = 0;
		if ( Current && ( !WriteProcessMemory( GetCurrentProcess( ), reinterpret_cast<void*>( Address ), &Disabled, sizeof( Disabled ), &Written ) || Written != sizeof( Disabled ) ) )
		{
			Error = "could not set UseRbxTransportClient3 to false";
			return false;
		}
		if ( !Memory::Read( Address, Current ) || Current != 0 )
		{
			Error = "transport flag readback failed";
			return false;
		}
		Error.clear( );
		return true;
	}

	int Transport::ReadFlag( ) noexcept
	{
		uint8_t Value = 0;
		return Resolve( ) && Memory::Read( Update::Roblox::Flags::UseRbxTransportClient, Value ) && Value <= 1 ? int( Value ) : -1;
	}
}

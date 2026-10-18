#include "Memory.h"
#include "core/Roblox/Update.h"
#include <array>
#include <cstring>

namespace Network
{
	bool Memory::Copy( void* Destination, const void* Source, size_t Size ) noexcept
	{
		__try
		{
			std::memcpy( Destination, Source, Size );
			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	bool Memory::Profile( ) noexcept
	{
		const auto Base = reinterpret_cast<uintptr_t>( GetModuleHandleW( nullptr ) );
		IMAGE_DOS_HEADER Dos{ };
		IMAGE_NT_HEADERS64 Pe{ };
		return Base && Read( Base, Dos ) && Dos.e_magic == IMAGE_DOS_SIGNATURE && Dos.e_lfanew > 0 && Dos.e_lfanew < 1048576 &&
			   Read( Base + Dos.e_lfanew, Pe ) && Pe.Signature == IMAGE_NT_SIGNATURE && Pe.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
			   Pe.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && Pe.FileHeader.TimeDateStamp == Update::Roblox::Network::TimeStamp;
	}

	bool Memory::Executable( uintptr_t Address ) noexcept
	{
		MEMORY_BASIC_INFORMATION Region{ };
		if ( !VirtualQuery( reinterpret_cast<void*>( Address ), &Region, sizeof( Region ) ) || Region.State != MEM_COMMIT ||
			 ( Region.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) != 0 )
			return false;
		return ( Region.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) != 0;
	}

	bool Memory::Match( uintptr_t Address, std::span<const uint8_t> Bytes ) noexcept
	{
		std::array<uint8_t, 64> Current{ };
		return !Bytes.empty( ) && Bytes.size( ) <= Current.size( ) && Executable( Address ) &&
			   Copy( Current.data( ), reinterpret_cast<const void*>( Address ), Bytes.size( ) ) &&
			   std::memcmp( Current.data( ), Bytes.data( ), Bytes.size( ) ) == 0;
	}
}

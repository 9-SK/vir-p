#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Network
{
	class Memory
	{
	  public:
		static bool Copy( void* Destination, const void* Source, size_t Size ) noexcept;
		static bool Profile( ) noexcept;
		static bool Match( uintptr_t Address, std::span<const uint8_t> Bytes ) noexcept;
		static bool Executable( uintptr_t Address ) noexcept;

		template <typename T>
		static bool Read( uintptr_t Address, T& Value ) noexcept
		{
			return Copy( &Value, reinterpret_cast<const void*>( Address ), sizeof( T ) );
		}
	};
}

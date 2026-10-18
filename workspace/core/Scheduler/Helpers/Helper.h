#pragma once
#include <Windows.h>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Scheduler::Helpers
{
	template <typename T>
	bool Read( uintptr_t Address, T& Value )
	{
		static_assert( std::is_trivially_copyable_v<T> );
		SIZE_T Count = 0;
		return Address >= 65536 && Address <= std::numeric_limits<uintptr_t>::max( ) - sizeof( T ) &&
			ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), &Value, sizeof( T ), &Count ) && Count == sizeof( T );
	}

	bool ValidState( uintptr_t Address, uintptr_t ScriptContext );
}

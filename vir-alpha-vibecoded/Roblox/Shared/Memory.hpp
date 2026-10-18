#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace Memory
{
	inline bool CanRead(uintptr_t address, size_t size)
	{
		if (address < 0x10000 || size == 0)
			return false;

		MEMORY_BASIC_INFORMATION info{};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info))
			return false;
		if (info.State != MEM_COMMIT)
			return false;
		if ((info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
			return false;

		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		return address + size <= regionEnd;
	}
}

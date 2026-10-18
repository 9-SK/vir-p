#pragma once

#include <lstate.h>
#include <Windows.h>
#include <RBX/update.hpp>

namespace Utils
{
	inline bool IsInGame(uintptr_t DataModel)
	{
		__try
		{
			uint32_t GameLoaded = *reinterpret_cast<uint32_t*>(DataModel + Offsets::DataModel::GameLoaded);
			if (GameLoaded != 31)
				return false;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

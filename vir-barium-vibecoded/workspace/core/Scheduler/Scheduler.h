#pragma once
#include "Helper.h"

namespace Scheduler
{
	[[nodiscard]] lua_State* GetLuaState( );
	bool Initialize( );
	void Retire( uintptr_t DataModel );
	inline void
	ExecuteY( std::string Script )
	{
		g_Env->QueueScript( Script );
	}
}

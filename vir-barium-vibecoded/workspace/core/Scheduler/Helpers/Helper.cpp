#include "Helper.h"
#include <VM/src/lstate.h>

namespace Scheduler::Helpers
{
	bool ValidState( uintptr_t Address, uintptr_t ScriptContext )
	{
		lua_State State{ };
		if ( !Read( Address, State ) || State.tt != LUA_TTHREAD || State.status > LUA_BREAK || !State.global || !State.userdata || !State.gt )
			return false;
		const uintptr_t Stack = reinterpret_cast<uintptr_t>( State.stack );
		const uintptr_t Last = reinterpret_cast<uintptr_t>( State.stack_last );
		const uintptr_t Top = reinterpret_cast<uintptr_t>( State.top );
		const uintptr_t Base = reinterpret_cast<uintptr_t>( State.base );
		const uintptr_t Frames = reinterpret_cast<uintptr_t>( State.base_ci );
		const uintptr_t Current = reinterpret_cast<uintptr_t>( State.ci );
		const uintptr_t End = reinterpret_cast<uintptr_t>( State.end_ci );
		if ( !Stack || Last < Stack || Last - Stack > 64 * 1024 * 1024 || Base < Stack || Top < Base || Top > Last ||
			!Frames || Current < Frames || End < Current || End - Frames > 16 * 1024 * 1024 || ( Current - Frames ) % sizeof( CallInfo ) )
			return false;
		uintptr_t Shared = 0;
		uintptr_t Owner = 0;
		lua_State* Main = nullptr;
		uint8_t Tag = 0;
		return Read( reinterpret_cast<uintptr_t>( State.userdata ) + offsetof( RobloxExtraSpace, shared ), Shared ) && Shared &&
			Read( Shared + offsetof( RobloxShared, scriptContext ), Owner ) && Owner == ScriptContext &&
			Read( reinterpret_cast<uintptr_t>( State.global ) + offsetof( global_State, mainthread ), Main ) && Main &&
			Read( reinterpret_cast<uintptr_t>( Main ), Tag ) && Tag == LUA_TTHREAD;
	}
}

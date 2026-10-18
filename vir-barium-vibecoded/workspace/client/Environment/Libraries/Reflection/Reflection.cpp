#include "Reflection.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"

namespace Reflection
{
	void Initialize( )
	{
		Env::RegisterGlobal( "getthreadidentity", Helpers::GetThreadIdentity );
		Env::RegisterGlobal( "setthreadidentity", Helpers::SetThreadIdentity );
		Env::RegisterGlobal( "getthreadcontext", Helpers::GetThreadIdentity );
		Env::RegisterGlobal( "setthreadcontext", Helpers::SetThreadIdentity );

		Env::RegisterGlobal( "gethiddenproperty", Helpers::GetHiddenProperty );
		Env::RegisterGlobal( "sethiddenproperty", Helpers::SetHiddenProperty );
		Env::RegisterGlobal( "isscriptable", Helpers::IsScriptable );
		Env::RegisterGlobal( "setscriptable", Helpers::SetScriptable );
	}
}

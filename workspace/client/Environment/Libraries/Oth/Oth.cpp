#include "Oth.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include <lua.h>

namespace Oth
{
	void Initialize( )
	{
		lua_State* L = g_Alpen->State( );
		Helpers::PushLibrary( L );
		lua_setglobal( L, "oth" );
		Env::RegisterGlobal( "checkcallstack", Helpers::CheckCallstack );
	}
}


#include "Actors.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"

namespace Actors
{
	void Initialize( )
	{
		Helpers::Register( g_Alpen->State( ) );
	}

	void Shutdown( )
	{
		Helpers::Shutdown( );
	}
}

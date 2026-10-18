#include "Signals.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"

namespace Signals
{
	void Initialize( )
	{
		Env::RegisterGlobal( "getconnections", Helpers::GetConnections );
		Env::RegisterGlobal( "firesignal", Helpers::FireSignal );
		Env::RegisterGlobal( "replicatesignal", Helpers::ReplicateSignal );
	}
}

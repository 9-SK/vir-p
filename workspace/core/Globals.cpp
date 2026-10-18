#include "Globals.h"
#include "Execution/Execution.h"
#include "Scheduler/Helper.h"
#include <mutex>
#include <string>
#include <utility>

void RBXGlobals::RuntimeErr( const char* Error )
{
	MessageBoxA( nullptr, Error, "Alpen", 0x10 );
	ExitProcess( 0x4360364 );
}

void RBXGlobals::Log( const char* Message )
{
	MessageBoxA( nullptr, Message, "Alpen", 0x10 );
}

void RBXGlobals::QueueScript( const std::string& Script )
{
	std::lock_guard<std::mutex> Lock( ScriptQueueMutex );
	ScriptQueue.push_back( Script );
}

void RBXGlobals::Execute( const std::string& Script )
{
	if ( !Script.empty( ) && ExecutorState )
		Alpen::Execution::Execute( ExecutorState, Script );
}

int RBXGlobals::Yielder( lua_State* L )
{
	if ( !L || !ExecutorState || L->global != RobloxGlobal || !LastDatamodel || GetDataModel( ) != LastDatamodel )
		return 0;

	std::string Script;
	{
		std::lock_guard<std::mutex> Lock( ScriptQueueMutex );
		if ( !ScriptQueue.empty( ) )
		{
			Script = std::move( ScriptQueue.front( ) );
			ScriptQueue.pop_front( );
		}
	}

	if ( !Script.empty( ) )
		Execute( Script );

	Yielding::Yield( );
	return 0;
}

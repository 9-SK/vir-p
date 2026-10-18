#pragma once

struct lua_State;

namespace Debug::Helpers
{
	int GetConstant( lua_State* L );
	int GetInfo( lua_State* L );
	int GetConstants( lua_State* L );
	int GetProto( lua_State* L );
	int GetProtos( lua_State* L );
	int GetStack( lua_State* L );
	int GetUpvalue( lua_State* L );
	int GetUpvalues( lua_State* L );
	int SetConstant( lua_State* L );
	int SetStack( lua_State* L );
	int SetUpvalue( lua_State* L );
}

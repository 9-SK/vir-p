#pragma once

struct lua_State;
struct CallInfo;

namespace Oth::Helpers
{
	lua_State* Original( lua_State* L );
	CallInfo* Frame( lua_State*& L, int Level );
	int Namecall( lua_State* L );
	int SetNamecall( lua_State* L );
	int CheckCallstack( lua_State* L );
	void PushLibrary( lua_State* L );
}

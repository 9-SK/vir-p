#pragma once

struct lua_State;

namespace Encoding::Helpers
{
	int EncodeBase64( lua_State* L );
	int DecodeBase64( lua_State* L );
	int CompressLZ4( lua_State* L );
	int DecompressLZ4( lua_State* L );
}

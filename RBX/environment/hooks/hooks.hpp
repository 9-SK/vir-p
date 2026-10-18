#pragma once

#include <lua.h>
#include <lualib.h>

// ── Original function pointers (set during hook installation) ──
extern lua_CFunction original_index;
extern lua_CFunction original_namecall;
extern lua_CFunction original_newindex;

// ── Install hooks on the game userdata metatable ───────────────
void InitializeHooks(lua_State* L);

// ── Anti-detection: hook debug.info and getfenv on state ───────
void InitializeAntiDetect(lua_State* L);

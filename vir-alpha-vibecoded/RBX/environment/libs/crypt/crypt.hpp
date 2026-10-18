#pragma once

#include <lstate.h>
#include <lualib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Crypt
{
    static int funcnameinc = 0;

    // Lua-facing functions
    int base64encode(lua_State* L);
    int base64decode(lua_State* L);
    int base64_encode(lua_State* L);
    int base64_decode(lua_State* L);
    int generatebytes(lua_State* L);
    int generatekey(lua_State* L);
    int crypt_encrypt(lua_State* L);
    int crypt_decrypt(lua_State* L);
    int hash(lua_State* L);
    int hmac(lua_State* L);
    int random(lua_State* L);
    int lz4compress(lua_State* L);
    int lz4decompress(lua_State* L);
    int getfunctionhash(lua_State* L);

    void register_lib(lua_State* L);
}

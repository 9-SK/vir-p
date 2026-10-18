#pragma once

#include <lstate.h>
#include <string>
#include <regex>


struct RegexObject {
    uint32_t magic;
    std::regex pattern;
    std::string patternStr;
};

constexpr uint32_t REGEX_MAGIC = 0x52454758; // "REGX"

namespace Regex {

    static int funcnameinc = 0;

    int regex_match(lua_State* L);
    int regex_matchmany(lua_State* L);
    int regex_replace(lua_State* L);
    int regex_index(lua_State* L);
    int regex_gc(lua_State* L);
    int regex_new(lua_State* L);
    int regex_escape(lua_State* L);

    void register_lib(lua_State* L);
}

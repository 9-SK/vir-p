#include "Regex.hpp"

#include <regex>
#include <cstring>

#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/environment/env.hpp>


namespace Regex {

    int regex_match(lua_State* L)
    {
        auto* obj = reinterpret_cast<RegexObject*>(lua_touserdata(L, 1));
        if (!obj || obj->magic != REGEX_MAGIC) luaL_error(L, ("invalid Regex object"));
        const char* contents = luaL_checkstring(L, 2);
        std::string str(contents);
        std::smatch match;
        if (std::regex_search(str, match, obj->pattern))
            lua_pushstring(L, match[0].str().c_str());
        else
            lua_pushnil(L);
        return 1;
    }

    int regex_matchmany(lua_State* L)
    {
        auto* obj = reinterpret_cast<RegexObject*>(lua_touserdata(L, 1));
        if (!obj || obj->magic != REGEX_MAGIC) luaL_error(L, ("invalid Regex object"));
        const char* contents = luaL_checkstring(L, 2);
        std::string str(contents);
        lua_newtable(L);
        int idx = 1;
        auto begin = std::sregex_iterator(str.begin(), str.end(), obj->pattern);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            lua_pushstring(L, (*it)[0].str().c_str());
            lua_rawseti(L, -2, idx++);
        }
        return 1;
    }

    int regex_replace(lua_State* L)
    {
        auto* obj = reinterpret_cast<RegexObject*>(lua_touserdata(L, 1));
        if (!obj || obj->magic != REGEX_MAGIC) luaL_error(L, ("invalid Regex object"));
        const char* contents = luaL_checkstring(L, 2);
        const char* replacement = luaL_checkstring(L, 3);
        std::string result = std::regex_replace(
            std::string(contents), obj->pattern, std::string(replacement),
            std::regex_constants::format_first_only
        );
        lua_pushstring(L, result.c_str());
        return 1;
    }

    int regex_index(lua_State* L)
    {
        const char* key = luaL_checkstring(L, 2);
        if (strcmp(key, ("Match")) == 0) { lua_pushcfunction(L, regex_match, ("Match")); return 1; }
        if (strcmp(key, ("MatchMany")) == 0) { lua_pushcfunction(L, regex_matchmany, ("MatchMany")); return 1; }
        if (strcmp(key, ("Replace")) == 0) { lua_pushcfunction(L, regex_replace, ("Replace")); return 1; }
        lua_pushnil(L);
        return 1;
    }

    int regex_gc(lua_State* L)
    {
        auto* obj = reinterpret_cast<RegexObject*>(lua_touserdata(L, 1));
        if (obj && obj->magic == REGEX_MAGIC) {
            obj->magic = 0;
            obj->~RegexObject();
        }
        return 0;
    }

    int regex_new(lua_State* L)
    {
        const char* pattern = luaL_checkstring(L, 1);
        auto* obj = reinterpret_cast<RegexObject*>(
            lua_newuserdata(L, sizeof(RegexObject))
            );

        try {
            new(obj) RegexObject();
            obj->magic = REGEX_MAGIC;
            obj->patternStr = pattern;
            obj->pattern = std::regex(pattern, std::regex::ECMAScript);
        }
        catch (const std::regex_error& e) {
            obj->magic = 0;
            lua_pushfstring(L, ("Regex.new: invalid pattern: %s"), e.what());
            lua_error(L);
        }

        lua_newtable(L);
        lua_pushcfunction(L, regex_index, ("__index"));
        lua_setfield(L, -2, ("__index"));
        lua_pushcfunction(L, regex_gc, ("__gc"));
        lua_setfield(L, -2, ("__gc"));
        lua_pushstring(L, ("Regex"));
        lua_setfield(L, -2, ("__type"));
        lua_setmetatable(L, -2);

        return 1;
    }

    int regex_escape(lua_State* L)
    {
        const char* str = luaL_checkstring(L, 1);
        std::string result;
        for (const char* c = str; *c; ++c)
        {
            if (strchr(R"(\.^$|?*+()[]{})", *c))
                result += '\\';
            result += *c;
        }
        lua_pushlstring(L, result.data(), result.size());
        return 1;
    }

    void register_lib(lua_State* L)
    {
        lua_newtable(L);
        Environment->make_table_function(L, ("new"), regex_new, Regex::funcnameinc++);
        Environment->make_table_function(L, ("Escape"), regex_escape, Regex::funcnameinc++);
        lua_setglobal(L, ("Regex"));
    }
}

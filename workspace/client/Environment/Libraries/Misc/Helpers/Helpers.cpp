#include "Helpers.h"
#include "client/Environment/Libraries/Http/Http.h"
#include "core/Scheduler/Helper.h"
#include <cctype>
#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <string>

namespace Misc::Helpers
{
	struct RequestOptions
	{
		std::string Url;
		std::string Method;
		std::string Body;
		std::string UserId;
		cpr::Header Headers;
		cpr::Cookies Cookies;
	};

	void ReadField( lua_State* L, const char* Name )
	{
		lua_pushstring( L, Name );
		lua_rawget( L, 1 );
	}

	std::string ReadString( lua_State* L, const char* Name, const char* Default = nullptr )
	{
		ReadField( L, Name );

		if ( lua_isnil( L, -1 ) && Default )
		{
			lua_pop( L, 1 );
			return Default;
		}

		if ( lua_type( L, -1 ) != LUA_TSTRING )
			luaL_error( L, "request.%s must be a string", Name );

		size_t Length = 0;
		const char* Value = lua_tolstring( L, -1, &Length );
		std::string Result( Value, Length );
		lua_pop( L, 1 );
		return Result;
	}

	bool IsToken( const std::string& Value )
	{
		if ( Value.empty( ) )
			return false;

		for ( unsigned char Character : Value )
		{
			if ( !( Character >= 'a' && Character <= 'z' ) && !( Character >= 'A' && Character <= 'Z' ) && !( Character >= '0' && Character <= '9' ) && !std::strchr( "!#$%&'*+-.^_`|~", Character ) )
				return false;
			if ( !Character )
				return false;
		}

		return true;
	}

	void ReadDictionary( lua_State* L, RequestOptions& Options, bool Cookies )
	{
		const char* Name = Cookies ? "Cookies" : "Headers";
		ReadField( L, Name );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			return;
		}

		if ( !lua_istable( L, -1 ) )
			luaL_error( L, "request.%s must be a table", Name );

		lua_pushnil( L );

		while ( lua_next( L, -2 ) )
		{
			if ( lua_type( L, -2 ) != LUA_TSTRING || lua_type( L, -1 ) != LUA_TSTRING )
				luaL_error( L, "request.%s must contain string keys and values", Name );

			size_t KeyLength = 0;
			size_t ValueLength = 0;
			const char* KeyData = lua_tolstring( L, -2, &KeyLength );
			const char* ValueData = lua_tolstring( L, -1, &ValueLength );
			std::string Key( KeyData, KeyLength );
			std::string Value( ValueData, ValueLength );

			if ( !IsToken( Key ) || Value.find( char( 0 ) ) != std::string::npos || Value.find( char( 13 ) ) != std::string::npos || Value.find( char( 10 ) ) != std::string::npos )
				luaL_error( L, "invalid request.%s entry", Name );

			if ( Cookies )
				Options.Cookies.emplace_back( cpr::Cookie{ Key, Value } );
			else
				Options.Headers[ Key ] = std::move( Value );

			lua_pop( L, 1 );
		}

		lua_pop( L, 1 );
	}

	std::string ReadUserId( lua_State* L )
	{
		lua_xpush( L->global->mainthread, L, LUA_GLOBALSINDEX );
		lua_getfield( L, -1, "game" );
		lua_remove( L, -2 );
		std::string UserId = "0";

		if ( lua_isuserdata( L, -1 ) )
		{
			lua_getfield( L, -1, "GetService" );
			lua_pushvalue( L, -2 );
			lua_pushliteral( L, "Players" );
			lua_call( L, 2, 1 );
			lua_getfield( L, -1, "LocalPlayer" );

			if ( lua_isuserdata( L, -1 ) )
			{
				lua_getfield( L, -1, "UserId" );
				UserId = std::to_string( static_cast<int64_t>( lua_tonumber( L, -1 ) ) );
				lua_pop( L, 1 );
			}

			lua_pop( L, 2 );
		}

		lua_pop( L, 1 );
		return UserId;
	}

	int PushResponse( lua_State* L, const cpr::Response& Response )
	{
		lua_rawcheckstack( L, 4 );
		lua_createtable( L, 0, 5 );
		lua_pushboolean( L, Response.error.code == cpr::ErrorCode::OK && Response.status_code >= 200 && Response.status_code < 300 );
		lua_setfield( L, -2, "Success" );
		lua_pushlstring( L, Response.text.data( ), Response.text.size( ) );
		lua_setfield( L, -2, "Body" );
		lua_pushinteger( L, static_cast<int>( Response.status_code ) );
		lua_setfield( L, -2, "StatusCode" );
		const std::string& Message = Response.error.code == cpr::ErrorCode::OK ? Response.reason : Response.error.message;
		lua_pushlstring( L, Message.data( ), Message.size( ) );
		lua_setfield( L, -2, "StatusMessage" );
		lua_createtable( L, 0, static_cast<int>( Response.header.size( ) ) );

		for ( const auto& [ Key, Value ] : Response.header )
		{
			lua_pushlstring( L, Key.data( ), Key.size( ) );
			lua_pushlstring( L, Value.data( ), Value.size( ) );
			lua_rawset( L, -3 );
		}

		lua_setfield( L, -2, "Headers" );
		return 1;
	}

	int PushRequest( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TTABLE );
		lua_rawcheckstack( L, 8 );
		auto Options = std::make_shared<RequestOptions>( );
		Options->Url = ReadString( L, "Url" );
		Options->Method = ReadString( L, "Method", "GET" );
		Options->Body = ReadString( L, "Body", "" );

		if ( Options->Url.find( char( 0 ) ) != std::string::npos || Options->Url.find( char( 13 ) ) != std::string::npos || Options->Url.find( char( 10 ) ) != std::string::npos || ( Options->Url.find( "http://" ) != 0 && Options->Url.find( "https://" ) != 0 ) )
			luaL_error( L, "request.Url must be an HTTP or HTTPS URL" );

		for ( char& Character : Options->Method )
			Character = static_cast<char>( std::toupper( static_cast<unsigned char>( Character ) ) );

		const std::string& Method = Options->Method;

		if ( Method != "GET" && Method != "HEAD" && Method != "POST" && Method != "PUT" && Method != "DELETE" && Method != "OPTIONS" && Method != "PATCH" )
			luaL_error( L, "unsupported request.Method" );

		ReadDictionary( L, *Options, false );
		ReadDictionary( L, *Options, true );
		Options->UserId = ReadUserId( L );

		return Yielding::YieldExec( L, [ Options ]( ) -> Yielded
		{
			HW_PROFILE_INFOA Profile{ };
			Options->Headers[ "Alpen-User-Identifier" ] = Options->UserId;
			Options->Headers[ "Alpen-Fingerprint" ] = GetCurrentHwProfileA( &Profile ) ? Profile.szHwProfileGuid : "Unknown";

			if ( Options->Headers.find( "User-Agent" ) == Options->Headers.end( ) )
				Options->Headers[ "User-Agent" ] = "Alpen/1.0.0";

			auto Response = std::make_shared<cpr::Response>( Http::Send( Options->Url, Options->Method, Options->Body, Options->Headers, Options->Cookies ) );

			return [ Response ]( lua_State* State )
			{
				return PushResponse( State, *Response );
			};
		} );
	}
}

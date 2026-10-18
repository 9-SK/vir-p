#include "Http.h"
#include "client/Environment/Env.h"
#include "core/Scheduler/Helper.h"
#include <cpr/cpr.h>
#include <lua.h>
#include <lualib.h>
#include <stdexcept>
#include <string>

namespace Http
{
	cpr::Response Send( const std::string& Url, const std::string& Method, const std::string& Body, const cpr::Header& Headers, const cpr::Cookies& Cookies )
	{
		static const CURLcode Initialized = curl_global_init( CURL_GLOBAL_DEFAULT );

		if ( Initialized != CURLE_OK )
			throw std::runtime_error( curl_easy_strerror( Initialized ) );

		cpr::Session Session;
		Session.SetUrl( cpr::Url{ Url } );
		Session.SetHeader( Headers );
		Session.SetUserAgent( cpr::UserAgent{ "Roblox/WinInet" } );
		Session.SetConnectTimeout( cpr::ConnectTimeout{ 10000 } );
		Session.SetTimeout( cpr::Timeout{ 60000 } );
		Session.SetRedirect( cpr::Redirect{ 10L } );
		Session.SetVerifySsl( cpr::VerifySsl{ true } );
		curl_easy_setopt( Session.GetCurlHolder( )->handle, CURLOPT_PROTOCOLS_STR, "http,https" );
		curl_easy_setopt( Session.GetCurlHolder( )->handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https" );
		curl_easy_setopt( Session.GetCurlHolder( )->handle, CURLOPT_ACCEPT_ENCODING, "" );

		Session.SetCookies( Cookies );
		if ( !Body.empty( ) )
			Session.SetBody( cpr::Body{ Body } );

		if ( Method == "GET" )
			return Session.Get( );
		if ( Method == "HEAD" )
			return Session.Head( );
		if ( Method == "POST" )
			return Session.Post( );
		if ( Method == "PUT" )
			return Session.Put( );
		if ( Method == "DELETE" )
			return Session.Delete( );
		if ( Method == "OPTIONS" )
			return Session.Options( );
		if ( Method == "PATCH" )
			return Session.Patch( );

		throw std::runtime_error( "unsupported HTTP method" );
	}

	namespace
	{
		std::string GetBody( const std::string& Url, const cpr::Header& Headers )
		{
			cpr::Response Response = Send( Url, "GET", { }, Headers, { } );

			if ( Response.error.code != cpr::ErrorCode::OK )
				throw std::runtime_error( Response.error.message );

			if ( Response.status_code < 200 || Response.status_code >= 300 )
				throw std::runtime_error( "HTTP status " + std::to_string( Response.status_code ) );

			return std::move( Response.text );
		}

		int HttpGet( lua_State* L )
		{
			int UrlIndex = lua_type( L, 1 ) == LUA_TSTRING ? 1 : 2;
			luaL_checktype( L, UrlIndex, LUA_TSTRING );
			size_t Length = 0;
			const char* Source = lua_tolstring( L, UrlIndex, &Length );
			std::string Url( Source, Length );

			if ( Url.find( char( 0 ) ) != std::string::npos || Url.find( char( 13 ) ) != std::string::npos || Url.find( char( 10 ) ) != std::string::npos )
				luaL_argerror( L, UrlIndex, "invalid URL" );

			if ( Url.find( "http://" ) != 0 && Url.find( "https://" ) != 0 )
				luaL_argerror( L, UrlIndex, "expected http:// or https://" );

			lua_xpush( L->global->mainthread, L, LUA_GLOBALSINDEX );
			lua_getfield( L, -1, "game" );
			lua_remove( L, -2 );
			std::string GameId;
			std::string PlaceId;

			if ( lua_isuserdata( L, -1 ) )
			{
				lua_getfield( L, -1, "GameId" );
				GameId = std::to_string( static_cast<uint64_t>( lua_tonumber( L, -1 ) ) );
				lua_pop( L, 1 );
				lua_getfield( L, -1, "PlaceId" );
				PlaceId = std::to_string( static_cast<uint64_t>( lua_tonumber( L, -1 ) ) );
				lua_pop( L, 1 );
			}

			lua_pop( L, 1 );
			return Yielding::YieldExec( L, [ Url = std::move( Url ), GameId = std::move( GameId ), PlaceId = std::move( PlaceId ) ]( ) -> Yielded
			{
				HW_PROFILE_INFOA Profile{ };
				std::string Fingerprint = GetCurrentHwProfileA( &Profile ) ? Profile.szHwProfileGuid : "Unknown";
				std::string SessionId = R"({"GameId":")" + GameId + R"(","PlaceId":")" + PlaceId + R"("})";
				cpr::Header Headers{
					{ "Accept", "*/*" },
					{ "Roblox-Session-Id", SessionId },
					{ "Roblox-Place-Id", PlaceId },
					{ "Roblox-Game-Id", GameId },
					{ "Exploit-Identifier", "Alpen" },
					{ "Exploit-Guid", Fingerprint },
					{ "Alpen-Fingerprint", Fingerprint } };

				try
				{
					std::string Body = GetBody( Url, Headers );

					return [ Body = std::move( Body ) ]( lua_State* State ) -> int
					{
						lua_pushlstring( State, Body.data( ), Body.size( ) );
						return 1;
					};
				}
				catch ( const std::exception& Exception )
				{
					return [ Error = std::string( "HttpGet failed: " ) + Exception.what( ) ]( lua_State* State ) -> int
					{
						lua_pushlstring( State, Error.data( ), Error.size( ) );
						return -1;
					};
				}
			} );
		}
	}

	void Initialize( )
	{
		Env::RegisterMethod( "HttpGet", HttpGet );
		Env::RegisterMethod( "HttpGetAsync", HttpGet );
	}
}

#include "Helper.h"
#include "core/Scheduler/Helper.h"
#include <array>
#include <curl/curl.h>
#include <deque>
#include <memory>
#include <vector>
#include <winsock2.h>

namespace WebSocket::Helpers
{
	namespace
	{
		constexpr size_t g_MaxBytes = 16 * 1024 * 1024;
		constexpr size_t g_MaxMessages = 256;

		struct Connection
		{
			std::unique_ptr<CURL, decltype( &curl_easy_cleanup )> Handle{ nullptr, curl_easy_cleanup };
			std::atomic<bool> Cancelled{ false };
			std::atomic<bool> Closing{ false };
			std::atomic<bool> Closed{ false };
			std::atomic<size_t> PendingBytes{ 0 };
			std::atomic<size_t> PendingMessages{ 0 };
			std::mutex Mutex;
			std::deque<std::string> Outgoing;
			size_t OutgoingBytes = 0;
			uint64_t Generation = 0;
			uintptr_t DataModel = 0;
			std::array<int, 2> Events{ LUA_NOREF, LUA_NOREF };
			int Fire = LUA_NOREF;
			bool CloseDelivered = false;
			const void* Object = nullptr;
		};

		using ConnectionRef = std::shared_ptr<Connection>;
		std::vector<std::weak_ptr<Connection>> g_Connections;

		void Release( lua_State* L, Connection& Socket )
		{
			for ( int& Reference : Socket.Events )
			{
				lua_unref( L, Reference );
				Reference = LUA_NOREF;
			}
			lua_unref( L, Socket.Fire );
			Socket.Fire = LUA_NOREF;
		}

		bool QueueEvent( const ConnectionRef& Socket, bool Close, std::string Message = { } )
		{
			if ( !Close && ( Socket->PendingMessages.load( ) >= g_MaxMessages || Message.size( ) > g_MaxBytes - Socket->PendingBytes.load( ) ) )
				return false;

			std::lock_guard<std::mutex> Lock( g_Env->YieldQueueMutex );
			if ( Socket->Cancelled || Socket->Generation != g_Env->SessionGeneration.load( ) )
				return false;

			const size_t Length = Message.size( );
			g_Env->YieldQueue.emplace( [ Socket, Close, Message = std::move( Message ) ]
									   {
										   Socket->PendingBytes.fetch_sub( Message.size( ) );
										   Socket->PendingMessages.fetch_sub( 1 );
										   if ( Socket->Cancelled || Socket->Generation != g_Env->SessionGeneration.load( ) || Socket->DataModel != GetDataModel( ) || !g_Env->ExecutorState || Socket->CloseDelivered )
											   return;

										   lua_State* L = g_Env->ExecutorState;
										   const int Top = lua_gettop( L );
										   struct Delivery
										   {
											   Connection* Socket;
											   const std::string* Message;
											   bool Close;
										   } Event{ Socket.get( ), &Message, Close };
										   luaD_pcall( L, []( lua_State* State, void* Context )
													   {
														   auto* Event = static_cast<Delivery*>( Context );
														   lua_rawcheckstack( State, 4 );
														   lua_getref( State, Event->Socket->Fire );
														   lua_getref( State, Event->Socket->Events[ Event->Close ? 1 : 0 ] );
														   if ( !Event->Close )
															   lua_pushlstring( State, Event->Message->data( ), Event->Message->size( ) );
														   lua_call( State, Event->Close ? 1 : 2, 0 );
													   },
													   &Event, savestack( L, L->top ), 0 );
										   lua_settop( L, Top );
										   if ( Close )
										   {
											   Socket->CloseDelivered = true;
											   Release( L, *Socket );
										   }
									   } );
			Socket->PendingBytes.fetch_add( Length );
			Socket->PendingMessages.fetch_add( 1 );
			return true;
		}

		std::string RedirectUrl( const std::string& Current, const char* Location )
		{
			std::unique_ptr<CURLU, decltype( &curl_url_cleanup )> Url{ curl_url( ), curl_url_cleanup };
			if ( !Url )
				throw std::runtime_error( "failed to allocate WebSocket URL" );
			if ( curl_url_set( Url.get( ), CURLUPART_URL, Current.c_str( ), 0 ) != CURLUE_OK ||
				 curl_url_set( Url.get( ), CURLUPART_URL, Location, CURLU_NON_SUPPORT_SCHEME ) != CURLUE_OK )
				throw std::runtime_error( "invalid WebSocket redirect URL" );

			char* Value = nullptr;
			if ( curl_url_get( Url.get( ), CURLUPART_SCHEME, &Value, 0 ) != CURLUE_OK )
				throw std::runtime_error( "invalid WebSocket redirect scheme" );
			std::unique_ptr<char, decltype( &curl_free )> Scheme{ Value, curl_free };
			const std::string Protocol = Scheme.get( );
			if ( Protocol != "http" && Protocol != "https" && Protocol != "ws" && Protocol != "wss" )
				throw std::runtime_error( "unsupported WebSocket redirect scheme" );
			if ( curl_url_set( Url.get( ), CURLUPART_SCHEME, Protocol == "https" || Protocol == "wss" ? "wss" : "ws", 0 ) != CURLUE_OK ||
				 curl_url_get( Url.get( ), CURLUPART_URL, &Value, 0 ) != CURLUE_OK )
				throw std::runtime_error( "invalid WebSocket redirect URL" );
			std::unique_ptr<char, decltype( &curl_free )> Resolved{ Value, curl_free };
			return Resolved.get( );
		}

		void Open( const ConnectionRef& Socket, const std::string& Url )
		{
			static const CURLcode Initialized = curl_global_init( CURL_GLOBAL_DEFAULT );
			if ( Initialized != CURLE_OK )
				throw std::runtime_error( curl_easy_strerror( Initialized ) );
			std::string Current = Url;
			const auto Deadline = std::chrono::steady_clock::now( ) + std::chrono::seconds( 15 );
			for ( size_t Redirects = 0;; ++Redirects )
			{
				if ( Socket->Cancelled )
					throw std::runtime_error( "WebSocket connection cancelled" );
				const auto Remaining = std::chrono::duration_cast<std::chrono::milliseconds>( Deadline - std::chrono::steady_clock::now( ) ).count( );
				if ( Remaining <= 0 )
					throw std::runtime_error( "WebSocket connection timed out" );
				Socket->Handle.reset( curl_easy_init( ) );
				if ( !Socket->Handle )
					throw std::runtime_error( "failed to allocate WebSocket" );

				CURL* Handle = Socket->Handle.get( );
				std::array<char, CURL_ERROR_SIZE> Error{ };
				curl_easy_setopt( Handle, CURLOPT_ERRORBUFFER, Error.data( ) );
				curl_easy_setopt( Handle, CURLOPT_URL, Current.c_str( ) );
				curl_easy_setopt( Handle, CURLOPT_PROTOCOLS_STR, "ws,wss" );
				curl_easy_setopt( Handle, CURLOPT_CONNECT_ONLY, 2L );
				curl_easy_setopt( Handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L );
				curl_easy_setopt( Handle, CURLOPT_TIMEOUT_MS, static_cast<long>( Remaining ) );
				curl_easy_setopt( Handle, CURLOPT_SSL_VERIFYPEER, 1L );
				curl_easy_setopt( Handle, CURLOPT_SSL_VERIFYHOST, 2L );
				curl_easy_setopt( Handle, CURLOPT_NOSIGNAL, 1L );
				curl_easy_setopt( Handle, CURLOPT_NOPROGRESS, 0L );
				curl_easy_setopt( Handle, CURLOPT_XFERINFODATA, Socket.get( ) );
				curl_easy_setopt( Handle, CURLOPT_XFERINFOFUNCTION, +[]( void* Context, curl_off_t, curl_off_t, curl_off_t, curl_off_t )
																	{
																		return static_cast<Connection*>( Context )->Cancelled.load( ) ? 1 : 0;
																	} );
				const CURLcode Result = curl_easy_perform( Handle );
				curl_easy_setopt( Handle, CURLOPT_ERRORBUFFER, nullptr );
				long Status = 0;
				curl_easy_getinfo( Handle, CURLINFO_RESPONSE_CODE, &Status );
				if ( Result == CURLE_HTTP_RETURNED_ERROR && ( Status == 301 || Status == 302 || Status == 303 || Status == 307 || Status == 308 ) )
				{
					if ( Redirects >= 5 )
						throw std::runtime_error( "too many WebSocket redirects" );
					curl_header* Location = nullptr;
					if ( curl_easy_header( Handle, "Location", 0, CURLH_HEADER, -1, &Location ) != CURLHE_OK || !Location->value[ 0 ] )
						throw std::runtime_error( "WebSocket redirect missing Location header" );
					Current = RedirectUrl( Current, Location->value );
					continue;
				}
				if ( Result != CURLE_OK )
					throw std::runtime_error( Error[ 0 ] ? Error.data( ) : curl_easy_strerror( Result ) );
				if ( Status != 101 )
					throw std::runtime_error( "WebSocket server did not switch protocols" );
				curl_easy_setopt( Handle, CURLOPT_TIMEOUT_MS, 0L );
				return;
			}
		}

		void Receive( const ConnectionRef& Socket )
		{
			CURL* Handle = Socket->Handle.get( );
			curl_socket_t Descriptor = CURL_SOCKET_BAD;
			if ( curl_easy_getinfo( Handle, CURLINFO_ACTIVESOCKET, &Descriptor ) != CURLE_OK || Descriptor == CURL_SOCKET_BAD )
				return;

			std::array<char, 16384> Buffer{ };
			std::string Incoming;
			std::string Outgoing;
			std::string ClosePayload = "\x03\xe8";
			size_t Sent = 0;
			bool Sending = false;
			bool SendingClose = false;
			bool CloseSent = false;
			bool CloseReceived = false;
			auto SendDeadline = std::chrono::steady_clock::time_point::max( );
			auto CloseDeadline = std::chrono::steady_clock::time_point::max( );

			while ( !Socket->Cancelled )
			{
				const auto Now = std::chrono::steady_clock::now( );
				if ( Socket->Closing && CloseDeadline == std::chrono::steady_clock::time_point::max( ) )
					CloseDeadline = Now + std::chrono::seconds( 3 );
				if ( Now >= CloseDeadline || Now >= SendDeadline )
					break;

				if ( !Sending && !CloseSent )
				{
					std::lock_guard<std::mutex> Lock( Socket->Mutex );
					if ( !Socket->Outgoing.empty( ) && !CloseReceived )
					{
						Outgoing = std::move( Socket->Outgoing.front( ) );
						Socket->Outgoing.pop_front( );
						Socket->OutgoingBytes -= Outgoing.size( );
						Sending = true;
					}
					else if ( Socket->Closing )
					{
						Outgoing = ClosePayload;
						Sending = true;
						SendingClose = true;
					}
					if ( Sending )
					{
						Sent = 0;
						SendDeadline = Now + std::chrono::seconds( 15 );
					}
				}

				bool Progress = false;
				if ( Sending )
				{
					size_t Count = 0;
					const CURLcode Result = curl_ws_send( Handle, Outgoing.data( ) + Sent, Outgoing.size( ) - Sent, &Count, 0, SendingClose ? CURLWS_CLOSE : CURLWS_TEXT );
					if ( Result != CURLE_OK && Result != CURLE_AGAIN )
						break;
					Sent += Count;
					Progress = Count != 0;
					if ( Result == CURLE_OK && Sent == Outgoing.size( ) )
					{
						Sending = false;
						SendDeadline = std::chrono::steady_clock::time_point::max( );
						CloseSent = SendingClose;
						Outgoing.clear( );
					}
				}
				if ( CloseSent && CloseReceived )
					break;

				size_t Count = 0;
				const curl_ws_frame* Frame = nullptr;
				const CURLcode Result = curl_ws_recv( Handle, Buffer.data( ), Buffer.size( ), &Count, &Frame );
				if ( Result == CURLE_OK )
				{
					Progress = true;
					if ( Frame->flags & CURLWS_CLOSE )
					{
						CloseReceived = true;
						Socket->Closing = true;
						if ( Count <= 125 && Frame->bytesleft == 0 )
							ClosePayload.assign( Buffer.data( ), Count );
					}
					else if ( Frame->flags & ( CURLWS_TEXT | CURLWS_BINARY ) )
					{
						if ( Count > g_MaxBytes - Incoming.size( ) )
							break;
						Incoming.append( Buffer.data( ), Count );
						if ( Frame->bytesleft == 0 && !( Frame->flags & CURLWS_CONT ) )
						{
							if ( !QueueEvent( Socket, false, std::move( Incoming ) ) )
								break;
							Incoming.clear( );
						}
					}
				}
				else if ( Result != CURLE_AGAIN )
					break;

				if ( !Progress )
				{
					fd_set Read;
					fd_set Write;
					FD_ZERO( &Read );
					FD_ZERO( &Write );
					FD_SET( Descriptor, &Read );
					if ( Sending )
						FD_SET( Descriptor, &Write );
					timeval Timeout{ 0, 20000 };
					if ( select( 0, &Read, Sending ? &Write : nullptr, nullptr, &Timeout ) == SOCKET_ERROR )
						break;
				}
			}
		}

		void Run( const ConnectionRef& Socket )
		{
			try
			{
				Receive( Socket );
			}
			catch ( const std::exception& Exception )
			{
				OutputDebugStringA( Exception.what( ) );
			}
			Socket->Closed = true;
			Socket->Handle.reset( );
			{
				std::lock_guard<std::mutex> Lock( Socket->Mutex );
				Socket->Outgoing.clear( );
				Socket->OutgoingBytes = 0;
			}
			QueueEvent( Socket, true );
		}

		ConnectionRef& CheckSocket( lua_State* L )
		{
			auto* Socket = static_cast<ConnectionRef*>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
			if ( !lua_istable( L, 1 ) || lua_topointer( L, 1 ) != ( *Socket )->Object )
				luaL_argerror( L, 1, "expected this WebSocket" );
			if ( ( *Socket )->Generation != g_Env->SessionGeneration.load( ) )
				luaL_error( L, "WebSocket belongs to an expired session" );
			return *Socket;
		}

		int Send( lua_State* L )
		{
			ConnectionRef& Socket = CheckSocket( L );
			luaL_checktype( L, 2, LUA_TSTRING );
			size_t Length = 0;
			const char* Message = lua_tolstring( L, 2, &Length );
			if ( Length > g_MaxBytes )
				luaL_argerror( L, 2, "message exceeds 16 MiB" );
			const char* Error = nullptr;
			{
				std::lock_guard<std::mutex> Lock( Socket->Mutex );
				if ( Socket->Closing || Socket->Closed || Socket->Cancelled )
					Error = "WebSocket is closed";
				else if ( Socket->Outgoing.size( ) >= g_MaxMessages || Length > g_MaxBytes - Socket->OutgoingBytes )
					Error = "WebSocket send queue is full";
				else
				{
					Socket->Outgoing.emplace_back( Message, Length );
					Socket->OutgoingBytes += Length;
				}
			}
			if ( Error )
				luaL_error( L, "%s", Error );
			return 0;
		}

		int Close( lua_State* L )
		{
			CheckSocket( L )->Closing = true;
			return 0;
		}

		int Attach( lua_State* L )
		{
			auto* Socket = static_cast<ConnectionRef*>( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
			auto* Owner = static_cast<ConnectionRef*>( lua_newuserdatadtor( L, sizeof( ConnectionRef ), []( void* Data )
																			{
																				auto* Socket = static_cast<ConnectionRef*>( Data );
																				( *Socket )->Closing = true;
																				Socket->~ConnectionRef( );
																			} ) );
			new ( Owner ) ConnectionRef( *Socket );
			lua_newtable( L );
			( *Socket )->Object = lua_topointer( L, -1 );
			lua_pushvalue( L, -2 );
			lua_pushcclosure( L, Send, "WebSocket.Send", 1 );
			lua_setfield( L, -2, "Send" );
			lua_pushvalue( L, -2 );
			lua_pushcclosure( L, Close, "WebSocket.Close", 1 );
			lua_setfield( L, -2, "Close" );

			for ( size_t Event = 0; Event < 2; ++Event )
			{
				lua_xpush( L->global->mainthread, L, LUA_GLOBALSINDEX );
				lua_getfield( L, -1, "Instance" );
				lua_remove( L, -2 );
				lua_getfield( L, -1, "new" );
				lua_remove( L, -2 );
				lua_pushstring( L, "BindableEvent" );
				lua_call( L, 1, 1 );
				( *Socket )->Events[ Event ] = lua_ref( L, -1 );
				if ( Event == 0 )
				{
					lua_getfield( L, -1, "Fire" );
					( *Socket )->Fire = lua_ref( L, -1 );
					lua_pop( L, 1 );
				}
				lua_getfield( L, -1, "Event" );
				lua_remove( L, -2 );
				lua_setfield( L, -2, Event == 0 ? "OnMessage" : "OnClose" );
			}
			lua_setreadonly( L, -1, true );
			lua_remove( L, -2 );
			return 1;
		}
	}

	int Connect( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TSTRING );
		size_t Length = 0;
		const char* Source = lua_tolstring( L, 1, &Length );
		std::string Url( Source, Length );
		if ( Url.find_first_of( std::string( "\0\r\n", 3 ) ) != std::string::npos || ( !Url.starts_with( "ws://" ) && !Url.starts_with( "wss://" ) ) )
			luaL_argerror( L, 1, "expected a ws:// or wss:// URL" );
		std::erase_if( g_Connections, []( const auto& Socket )
					   {
						   return Socket.expired( );
					   } );
		if ( g_Connections.size( ) >= 64 )
			luaL_error( L, "too many WebSocket connections" );

		auto Socket = std::make_shared<Connection>( );
		Socket->Generation = g_Env->SessionGeneration.load( );
		Socket->DataModel = GetDataModel( );
		g_Connections.emplace_back( Socket );
		return Yielding::YieldExec( L, [ Socket, Url = std::move( Url ) ]
									{
										Open( Socket, Url );
										return [ Socket ]( lua_State* State )
										{
											lua_State* Scheduler = g_Env->ExecutorState;
											if ( !Scheduler || Scheduler->global != State->global || lua_status( Scheduler ) != LUA_OK )
											{
												Socket->Cancelled = true;
												lua_pushliteral( State, "WebSocket scheduler is unavailable" );
												return -1;
											}
											const int Top = lua_gettop( Scheduler );
											const int Status = luaD_pcall( Scheduler, []( lua_State* L, void* Context )
																		   {
																			   lua_rawcheckstack( L, LUA_MINSTACK );
																			   lua_pushlightuserdata( L, Context );
																			   lua_pushcclosure( L, Attach, "WebSocket.connect", 1 );
																			   lua_call( L, 0, 1 );
																		   },
																		   const_cast<ConnectionRef*>( &Socket ), savestack( Scheduler, Scheduler->top ), 0 );
											lua_xmove( Scheduler, State, 1 );
											lua_settop( Scheduler, Top );
											if ( Status != LUA_OK )
											{
												Socket->Cancelled = true;
												Release( State, *Socket );
												return -1;
											}
											return 1;
										};
									} );
	}

	int Connected( lua_State* L, int Status )
	{
		if ( Status != LUA_OK )
			lua_error( L );
		luaL_checktype( L, -1, LUA_TTABLE );
		lua_getfield( L, -1, "Send" );
		if ( !lua_getupvalue( L, -1, 1 ) )
			luaL_error( L, "WebSocket connection is unavailable" );
		auto* Socket = static_cast<ConnectionRef*>( lua_touserdata( L, -1 ) );
		if ( !Socket || ( *Socket )->Cancelled || ( *Socket )->Generation != g_Env->SessionGeneration.load( ) )
			luaL_error( L, "WebSocket connection expired" );
		bool Failed = false;
		try
		{
			Yielding::Submit( [ Connection = *Socket ]
							  {
								  Run( Connection );
							  } );
		}
		catch ( const std::exception& Exception )
		{
			( *Socket )->Cancelled = true;
			Release( L, **Socket );
			lua_pushstring( L, Exception.what( ) );
			Failed = true;
		}
		if ( Failed )
			lua_error( L );
		lua_pop( L, 2 );
		return 1;
	}

	void Shutdown( )
	{
		for ( const auto& Reference : g_Connections )
		{
			if ( auto Socket = Reference.lock( ) )
				Socket->Cancelled = true;
		}
		g_Connections.clear( );
	}
}

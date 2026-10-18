#include "Helper.h"
#include "client/Environment/Libraries/Instances/Helpers/Helpers.h"
#include "core/Roblox/Update.h"
#include "core/Roblox/Variant.h"
#include <VM/src/lstate.h>
#include <VM/src/ludata.h>
#include <cmath>
#include <cstring>
#include <emmintrin.h>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Signals::Helpers
{
	namespace
	{
		struct Binding
		{
			uintptr_t Descriptor;
			std::shared_ptr<uintptr_t> Custom;
			std::weak_ptr<uintptr_t> Source;
		};

		struct Signal
		{
			Binding* Event;
			const std::string* Name;
			const std::string* Class;
			bool Scriptable;
			uintptr_t Capabilities;
		};

		struct Node
		{
			volatile long Strong;
			volatile long Weak;
			uintptr_t Invoke;
			Node* Next;
			uintptr_t Previous;
			uintptr_t Graph;
			uintptr_t Destroy;
		};

		struct LiveReference
		{
			uintptr_t Links[ 5 ];
			lua_State* Thread;
			int ThreadReference;
			int ValueReference;
		};

		struct NativeConnection
		{
			Node* Value;
			std::weak_ptr<uintptr_t> Slot;
		};

		struct TouchFunction
		{
			uintptr_t Vtable;
			std::shared_ptr<uintptr_t> Slot;
		};

		struct TouchCapture
		{
			uintptr_t Storage[ 7 ];
			TouchFunction* Function;
		};

		struct Guard
		{
			uintptr_t Mutex = Update::Roblox::Functions::SignalGuard( );

			Guard( )
			{
				Update::Roblox::Functions::LockSignal( Mutex );
			}

			~Guard( )
			{
				Update::Roblox::Functions::UnlockSignal( Mutex );
			}
		};

		struct NodeRelease
		{
			void operator( )( Node* Value ) const
			{
				uintptr_t Address = reinterpret_cast<uintptr_t>( Value );
				Update::Roblox::Functions::ReleaseSignalNode( &Address );
			}
		};

		using NodeRef = std::unique_ptr<Node, NodeRelease>;
		std::shared_ptr<uintptr_t> LuaSlot( const NodeRef& Value );

		bool Retain( volatile long* Count )
		{
			long Value = *Count;
			while ( Value > 0 )
			{
				const long Previous = _InterlockedCompareExchange( Count, Value + 1, Value );
				if ( Previous == Value )
					return true;
				Value = Previous;
			}
			return false;
		}

		struct Connection
		{
			Node* Value = nullptr;
			uintptr_t Graph = 0;
			NodeRef Suspended;
			std::weak_ptr<uintptr_t> Source;
			bool Disconnected = false;
			const bool LuaConnection;

			Connection( Node* Node, uintptr_t Owner, const std::weak_ptr<uintptr_t>& Instance )
				: Value( Node ), Graph( Owner ), Source( Instance ), LuaConnection( LuaSlot( Lock( ) ) != nullptr )
			{
				if ( Value )
					_InterlockedIncrement( &Value->Weak );
				if ( Graph )
					_InterlockedIncrement( reinterpret_cast<volatile long*>( Graph ) + 1 );
			}

			~Connection( )
			{
				Enable( );
				if ( Value && _InterlockedDecrement( &Value->Weak ) == 0 )
					Update::Roblox::Functions::FreeSignalNode( reinterpret_cast<uintptr_t>( Value ) );
				if ( Graph && _InterlockedDecrement( reinterpret_cast<volatile long*>( Graph ) + 1 ) == 0 )
					Update::Roblox::Functions::FreeSignalGraph( Graph );
			}

			void Retire( )
			{
				Disconnected = true;
				Suspended.reset( );
			}

			NodeRef Lock( ) const
			{
				return NodeRef{ Value && Retain( &Value->Strong ) ? Value : nullptr };
			}

			bool Enabled( ) const
			{
				uintptr_t Address = reinterpret_cast<uintptr_t>( Value );
				return !Disconnected && !Suspended && Update::Roblox::Functions::SignalConnected( &Address );
			}

			void Enable( )
			{
				if ( !Suspended )
					return;
				auto Slot = LuaSlot( Suspended );
				if ( Slot && *reinterpret_cast<bool*>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::Cancelled ) )
					Disconnected = true;
				if ( !Disconnected && !Source.expired( ) && Retain( reinterpret_cast<volatile long*>( Graph ) ) )
				{
					bool Linked = false;
					{
						Guard Lock;
						for ( auto* Entry = *reinterpret_cast<Node**>( Graph + sizeof( uintptr_t ) ); Entry; Entry = Entry->Next )
							if ( Entry == Suspended.get( ) )
							{
								Linked = true;
								break;
							}
						_InterlockedIncrement( reinterpret_cast<volatile long*>( Graph ) + 1 );
						Suspended->Graph = Graph;
					}
					if ( !Linked )
						Update::Roblox::Functions::InsertSignalNode( Graph, reinterpret_cast<uintptr_t>( Suspended.get( ) ) );
					uintptr_t Address = Graph;
					Update::Roblox::Functions::ReleaseSignalGraph( &Address );
				}
				Suspended.reset( );
			}

			void Disable( )
			{
				if ( !Enabled( ) )
					return;
				Suspended = Lock( );
				if ( Suspended )
				{
					uintptr_t Address = reinterpret_cast<uintptr_t>( Value );
					Update::Roblox::Functions::SignalDisconnect( &Address );
				}
			}
		};

		using ConnectionRef = std::shared_ptr<Connection>;
		using ConnectionMap = std::unordered_map<Node*, ConnectionRef>;
		char g_Connections;
		char g_Methods;

		static_assert( sizeof( Signal ) == 40 );
		static_assert( sizeof( Binding ) == 40 );
		static_assert( sizeof( NativeConnection ) == 24 );
		static_assert( sizeof( Node ) == 48 );
		static_assert( sizeof( TouchFunction ) == 24 );
		static_assert( sizeof( TouchCapture ) == 64 );

		template <typename T>
		T Read( uintptr_t Address )
		{
			T Value{ };
			if ( !Address || Address > UINTPTR_MAX - sizeof( T ) || !ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), &Value, sizeof( T ), nullptr ) )
				throw std::runtime_error( "signal metadata is unavailable" );
			return Value;
		}

		Signal& CheckSignal( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TUSERDATA );
			if ( lua_userdatatag( L, 1 ) != Read<int>( Update::Roblox::Signals::Tag ) || lua_objlen( L, 1 ) != sizeof( Signal ) )
				luaL_argerror( L, 1, "expected an RBXScriptSignal" );
			auto* Value = static_cast<Signal*>( lua_touserdata( L, 1 ) );
			if ( !Value->Event )
				luaL_argerror( L, 1, "invalid signal" );
			return *Value;
		}

		ConnectionMap& Connections( lua_State* L )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Connections );
			auto* Map = static_cast<ConnectionMap*>( lua_touserdata( L, -1 ) );
			lua_pop( L, 1 );
			if ( !Map )
			{
				void* Storage = lua_newuserdatadtor( L, sizeof( ConnectionMap ), []( void* Data )
													 {
														 auto* Map = static_cast<ConnectionMap*>( Data );
														 for ( const auto& [ Value, Entry ] : *Map )
															 Entry->Retire( );
														 Map->~ConnectionMap( );
													 } );
				Map = new ( Storage ) ConnectionMap{ };
				lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Connections );
			}
			return *Map;
		}

		std::shared_ptr<uintptr_t> LuaSlot( const NodeRef& Value )
		{
			if ( !Value || !( Value->Previous & 4 ) )
				return { };
			const std::shared_ptr<uintptr_t>* Capture = nullptr;
			if ( Value->Destroy == Update::Roblox::Signals::DestroyLua )
				Capture = reinterpret_cast<const std::shared_ptr<uintptr_t>*>( Value.get( ) + 1 );
			else if ( Value->Destroy == Update::Roblox::Signals::Touch::Destroy )
			{
				const auto* Function = reinterpret_cast<const TouchCapture*>( Value.get( ) + 1 )->Function;
				if ( Function && ( Function->Vtable == Update::Roblox::Signals::Touch::VectorCallback || Function->Vtable == Update::Roblox::Signals::Touch::SpanCallback ) )
					Capture = &Function->Slot;
			}
			if ( !Capture || !*Capture )
				return { };
			const auto& Slot = *Capture;
			if ( *Slot != Update::Roblox::Signals::LuaSlot && *Slot != Update::Roblox::Signals::LuaSlotProtected && *Slot != Update::Roblox::Signals::WaitSlot )
				return { };
			return Slot;
		}

		std::shared_ptr<LiveReference> Callback( const std::shared_ptr<uintptr_t>& Slot )
		{
			if ( !Slot || *Slot == Update::Roblox::Signals::WaitSlot || Read<uint32_t>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::CallbackKind ) > 1 )
				return { };
			return reinterpret_cast<const std::weak_ptr<LiveReference>*>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::Callback )->lock( );
		}

		std::shared_ptr<LiveReference> ThreadReference( const std::shared_ptr<uintptr_t>& Slot )
		{
			if ( !Slot )
				return { };
			if ( *Slot == Update::Roblox::Signals::WaitSlot )
				return reinterpret_cast<const std::weak_ptr<LiveReference>*>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::WaitingThread )->lock( );
			auto Reference = Callback( Slot );
			if ( Reference && Reference->Thread )
				return Reference;
			return reinterpret_cast<const std::weak_ptr<LiveReference>*>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::CachedThread )->lock( );
		}

		Connection& CheckConnection( lua_State* L, int Index = 1 )
		{
			Index = lua_absindex( L, Index );
			luaL_checktype( L, Index, LUA_TUSERDATA );
			bool Valid = false;
			const size_t Size = sizeof( ConnectionRef ) + sizeof( void ( * )( lua_State*, void* ) );
			if ( lua_userdatatag( L, Index ) == UTAG_IDTOR && lua_objlen( L, Index ) == Size && lua_getmetatable( L, Index ) )
			{
				lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Methods );
				Valid = lua_rawequal( L, -1, -2 );
				lua_pop( L, 2 );
			}
			if ( !Valid )
				luaL_argerror( L, Index, "expected a Connection" );
			auto* Entry = static_cast<ConnectionRef*>( lua_touserdata( L, Index ) );
			if ( !*Entry )
				luaL_argerror( L, Index, "invalid Connection" );
			return **Entry;
		}

		int Disconnect( lua_State* L )
		{
			auto& Entry = CheckConnection( L );
			Entry.Disconnected = true;
			auto Value = Entry.Lock( );
			if ( Value )
			{
				auto Slot = LuaSlot( Value );
				if ( Slot )
					*reinterpret_cast<bool*>( reinterpret_cast<uintptr_t>( Slot.get( ) ) + Update::Roblox::Signals::Cancelled ) = true;
				uintptr_t Address = reinterpret_cast<uintptr_t>( Value.get( ) );
				Update::Roblox::Functions::SignalDisconnect( &Address );
			}
			Entry.Suspended.reset( );
			return 0;
		}

		int Disable( lua_State* L )
		{
			CheckConnection( L ).Disable( );
			return 0;
		}

		int Enable( lua_State* L )
		{
			CheckConnection( L ).Enable( );
			return 0;
		}

		int Invoke( lua_State* L, const char* Method )
		{
			auto& Entry = CheckConnection( L );
			const int Count = lua_gettop( L ) - 1;
			{
				auto Value = Entry.Lock( );
				auto Slot = LuaSlot( Value );
				auto Reference = Callback( Slot );
				if ( Entry.Disconnected || !Reference || !Reference->Thread || Reference->Thread->global != L->global || Reference->ValueReference <= 0 )
					return 0;
				lua_getref( L, Reference->ValueReference );
				const uintptr_t SlotAddress = reinterpret_cast<uintptr_t>( Slot.get( ) );
				if ( Read<bool>( SlotAddress + Update::Roblox::Signals::Once ) )
				{
					if ( Read<bool>( SlotAddress + Update::Roblox::Signals::Consumed ) )
						return 0;
					*reinterpret_cast<bool*>( SlotAddress + Update::Roblox::Signals::Consumed ) = true;
					Entry.Disconnected = true;
					uintptr_t Address = reinterpret_cast<uintptr_t>( Value.get( ) );
					Update::Roblox::Functions::SignalDisconnect( &Address );
				}
			}
			if ( !lua_isfunction( L, -1 ) )
				return 0;
			lua_replace( L, 1 );
			lua_xpush( L->global->mainthread, L, LUA_GLOBALSINDEX );
			lua_getfield( L, -1, "task" );
			lua_getfield( L, -1, Method );
			lua_remove( L, -2 );
			lua_remove( L, -2 );
			lua_insert( L, 1 );
			lua_call( L, Count + 1, 0 );
			return 0;
		}

		int Fire( lua_State* L )
		{
			return Invoke( L, "spawn" );
		}

		int Defer( lua_State* L )
		{
			return Invoke( L, "defer" );
		}

		int Index( lua_State* L )
		{
			auto& Entry = CheckConnection( L );
			luaL_checktype( L, 2, LUA_TSTRING );
			size_t Length = 0;
			const char* Name = lua_tolstring( L, 2, &Length );
			if ( std::memchr( Name, 0, Length ) )
				luaL_argerror( L, 2, "invalid Connection member" );
			if ( std::strcmp( Name, "Enabled" ) == 0 )
				lua_pushboolean( L, Entry.Enabled( ) );
			else if ( std::strcmp( Name, "LuaConnection" ) == 0 )
				lua_pushboolean( L, Entry.LuaConnection );
			else if ( std::strcmp( Name, "ForeignState" ) == 0 || std::strcmp( Name, "Function" ) == 0 || std::strcmp( Name, "Thread" ) == 0 )
			{
				auto Value = Entry.Lock( );
				auto Slot = LuaSlot( Value );
				auto Reference = ThreadReference( Slot );
				const bool Foreign = !Reference || !Reference->Thread || Reference->Thread->global != L->global;
				if ( std::strcmp( Name, "ForeignState" ) == 0 )
					lua_pushboolean( L, Foreign );
				else if ( Foreign || Entry.Disconnected )
					lua_pushnil( L );
				else if ( std::strcmp( Name, "Function" ) == 0 )
				{
					Reference = Callback( Slot );
					if ( !Reference || !Reference->Thread || Reference->Thread->global != L->global || Reference->ValueReference <= 0 )
						lua_pushnil( L );
					else
						lua_getref( L, Reference->ValueReference );
					if ( !lua_isfunction( L, -1 ) )
					{
						lua_pop( L, 1 );
						lua_pushnil( L );
					}
				}
				else
				{
					lua_rawcheckstack( Reference->Thread, 1 );
					lua_pushthread( Reference->Thread );
					lua_xmove( Reference->Thread, L, 1 );
				}
			}
			else
			{
				lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Methods );
				lua_pushvalue( L, 2 );
				lua_rawget( L, -2 );
				lua_remove( L, -2 );
				if ( !lua_isfunction( L, -1 ) || Name[ 0 ] == '_' )
					luaL_argerror( L, 2, "invalid Connection member" );
			}
			return 1;
		}

		void PushConnection( lua_State* L, const ConnectionRef& Entry )
		{
			void* Storage = lua_newuserdatadtor( L, sizeof( ConnectionRef ), []( void* Data )
												 {
													 static_cast<ConnectionRef*>( Data )->~shared_ptr( );
												 } );
			new ( Storage ) ConnectionRef( Entry );
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Methods );
			if ( !lua_istable( L, -1 ) )
			{
				lua_pop( L, 1 );
				lua_newtable( L );
				const luaL_Reg Methods[]{
					{ "__index", Index },
					{ "Fire", Fire },
					{ "Defer", Defer },
					{ "Disconnect", Disconnect },
					{ "Disable", Disable },
					{ "Enable", Enable },
					{ nullptr, nullptr } };
				luaL_register( L, nullptr, Methods );
				lua_pushliteral( L, "Connection" );
				lua_setfield( L, -2, "__type" );
				lua_pushliteral( L, "Connection" );
				lua_setfield( L, -2, "__metatable" );
				lua_setreadonly( L, -1, true );
				lua_pushvalue( L, -1 );
				lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Methods );
			}
			lua_setmetatable( L, -2 );
		}

		struct Iteration
		{
			uintptr_t Graph = 0;
			Node* Head = nullptr;

			~Iteration( )
			{
				if ( Graph )
					Update::Roblox::Functions::SignalEnd( this );
			}
		};

		struct Parameter
		{
			const std::string* Name;
			uintptr_t Type;
			uintptr_t Attributes;
			uintptr_t Class;
			unsigned char Default[ 80 ];
		};

		struct Parameters
		{
			Parameter* Begin;
			Parameter* End;
			Parameter* Capacity;
		};

		struct Arguments
		{
			RBX::Variant* Begin;
			RBX::Variant* End;
			RBX::Variant* Capacity;
		};

		static_assert( sizeof( Parameter ) == 112 );

		template <typename T>
		const RBX::VariantOperations g_Operations{
			[]( const void* Source, void* Target )
			{
				new ( Target ) T( *static_cast<const T*>( Source ) );
			},
			[]( void* Source, void* Target )
			{
				new ( Target ) T( std::move( *static_cast<T*>( Source ) ) );
			},
			[]( void* Value )
			{
				// MSVC fails to resolve the pseudo-destructor name for alias types
				// (e.g. std::shared_ptr<uintptr_t>); destroy_at does the same work.
				std::destroy_at( static_cast<T*>( Value ) );
			} };

		template <typename T>
		void SetValue( RBX::Variant& Value, uintptr_t Type, T Data )
		{
			Value.Reset( );
			new ( Value.Data ) T( std::move( Data ) );
			Value.Operations = &g_Operations<T>;
			Value.Type = Type;
		}

		void ReadArgument( lua_State* L, int Index, const Parameter& Parameter, RBX::Variant& Value )
		{
			const uint32_t Kind = Read<uint32_t>( Parameter.Type + Update::Roblox::Reflection::Property::TypeId );
			if ( Kind >= 2 && Kind <= 5 )
			{
				luaL_checktype( L, Index, LUA_TNUMBER );
				const double Number = lua_tonumber( L, Index );
				if ( Kind != 2 && !std::isfinite( Number ) )
					luaL_argerror( L, Index, "expected a finite signal argument" );
				if ( Kind == 2 )
				{
					const double Rounded = Number < 0 ? Number - 0.5 : Number + 0.5;
					SetValue( Value, Parameter.Type, _mm_cvttsd_si32( _mm_set_sd( Rounded ) ) );
				}
				else if ( Kind == 3 )
				{
					if ( Number < -9223372036854775808.0 || Number >= 9223372036854775808.0 || std::trunc( Number ) != Number )
						luaL_argerror( L, Index, "expected a signed 64-bit signal argument" );
					SetValue( Value, Parameter.Type, static_cast<int64_t>( Number ) );
				}
				else if ( Kind == 4 )
				{
					if ( std::abs( Number ) > std::numeric_limits<float>::max( ) )
						luaL_argerror( L, Index, "signal argument exceeds float range" );
					SetValue( Value, Parameter.Type, static_cast<float>( Number ) );
				}
				else
					SetValue( Value, Parameter.Type, Number );
				return;
			}
			if ( Kind == 8 )
			{
				std::shared_ptr<uintptr_t> Instance;
				if ( !lua_isnil( L, Index ) )
				{
					const auto* Name = Parameter.Class ? Read<const std::string*>( Parameter.Class + sizeof( uintptr_t ) ) : nullptr;
					Instance = Instances::Helpers::CheckInstance( L, Index, Name ? Name->c_str( ) : nullptr );
				}
				SetValue( Value, Parameter.Type, std::move( Instance ) );
				return;
			}
			if ( !Update::Roblox::Functions::ReadVariant( L, Index, &Value, false ) || Value.Type != Parameter.Type )
				luaL_argerror( L, Index, "argument does not match the signal signature" );
		}
	}

	int GetConnections( lua_State* L )
	{
		lua_rawcheckstack( L, 12 );
		Signal& Signal = CheckSignal( L );
		auto Source = Signal.Event->Source.lock( );
		lua_newtable( L );
		const int Result = lua_gettop( L );
		if ( !Source )
			return 1;
		lua_getfield( L, 1, "Connect" );
		lua_pushvalue( L, 1 );
		lua_pushcfunction( L, []( lua_State* )
						   {
							   return 0;
						   },
						   nullptr );
		lua_call( L, 2, 1 );
		const int Probe = lua_gettop( L );
		if ( lua_type( L, Probe ) != LUA_TUSERDATA || lua_objlen( L, Probe ) != sizeof( NativeConnection ) )
			luaL_error( L, "signal connection is unavailable" );
		auto* Native = static_cast<NativeConnection*>( lua_touserdata( L, Probe ) );
		struct ProbeGuard
		{
			Node* Value;

			~ProbeGuard( )
			{
				uintptr_t Address = reinterpret_cast<uintptr_t>( Value );
				Update::Roblox::Functions::SignalDisconnect( &Address );
			}
		} Cleanup{ Native->Value };
		std::vector<ConnectionRef> Entries;
		if ( Native->Value && Native->Value->Graph )
		{
			Iteration Walk;
			Update::Roblox::Functions::SignalBegin( Native->Value->Graph, &Walk );
			auto& Map = Connections( L );
			Guard Lock;
			for ( Node* Value = Walk.Head; Value; Value = Value->Next )
			{
				if ( Value == Native->Value || !Value->Graph )
					continue;
				auto& Entry = Map[ Value ];
				if ( !Entry )
					Entry = std::make_shared<Connection>( Value, Walk.Graph, Source );
				Entries.push_back( Entry );
			}
			for ( const auto& [ Value, Entry ] : Map )
				if ( Entry->Graph == Walk.Graph && Entry->Suspended && !Entry->Disconnected )
					Entries.push_back( Entry );
		}

		int Count = 0;
		for ( const auto& Entry : Entries )
		{
			PushConnection( L, Entry );
			lua_rawseti( L, Result, ++Count );
		}
		lua_pushvalue( L, Result );
		return 1;
	}

	int FireSignal( lua_State* L )
	{
		CheckSignal( L );
		const int Count = lua_gettop( L );
		lua_rawcheckstack( L, Count + 8 );
		lua_pushcfunction( L, GetConnections, "getconnections" );
		lua_pushvalue( L, 1 );
		lua_call( L, 1, 1 );
		const int List = lua_gettop( L );
		const int Total = lua_objlen( L, List );
		for ( int Index = Total; Index >= 1; --Index )
		{
			lua_rawgeti( L, List, Index );
			auto& Entry = CheckConnection( L, -1 );
			if ( !Entry.Enabled( ) )
			{
				lua_pop( L, 1 );
				continue;
			}
			lua_pushcfunction( L, Fire, "Fire" );
			lua_insert( L, -2 );
			for ( int Argument = 2; Argument <= Count; ++Argument )
				lua_pushvalue( L, Argument );
			lua_call( L, Count, 0 );
		}
		return 0;
	}

	int ReplicateSignal( lua_State* L )
	{
		lua_rawcheckstack( L, 8 );
		Signal& Signal = CheckSignal( L );
		const uintptr_t Descriptor = Signal.Event->Descriptor;
		if ( !Descriptor )
			luaL_argerror( L, 1, "signal has no replicable event descriptor" );
		const auto Signature = Read<Parameters>( Descriptor + Update::Roblox::Signals::Parameters );
		const uintptr_t Start = reinterpret_cast<uintptr_t>( Signature.Begin );
		const uintptr_t End = reinterpret_cast<uintptr_t>( Signature.End );
		if ( End < Start || End > reinterpret_cast<uintptr_t>( Signature.Capacity ) || ( End - Start ) % sizeof( Parameter ) || ( End - Start ) / sizeof( Parameter ) > 256 )
			luaL_error( L, "invalid signal signature" );
		const size_t Count = ( End - Start ) / sizeof( Parameter );
		if ( lua_gettop( L ) - 1 != Count )
			luaL_error( L, "signal expects %d arguments, got %d", static_cast<int>( Count ), lua_gettop( L ) - 1 );
		try
		{
			auto Source = Signal.Event->Source.lock( );
			if ( !Source )
				luaL_argerror( L, 1, "signal source is no longer available" );
			auto Values = std::make_unique<RBX::Variant[]>( Count ? Count : 1 );
			for ( size_t Index = 0; Index < Count; ++Index )
				ReadArgument( L, static_cast<int>( Index ) + 2, Signature.Begin[ Index ], Values[ Index ] );
			Arguments Payload{ Values.get( ), Values.get( ) + Count, Values.get( ) + Count };
			uintptr_t Target[ 3 ]{ };
			using Publish = void ( * )( uintptr_t, uintptr_t, const Arguments*, const void* );
			const uintptr_t Address = reinterpret_cast<uintptr_t>( Source.get( ) );
			auto Function = Read<Publish>( Read<uintptr_t>( Address ) + Update::Roblox::Signals::Publish );
			if ( !Function )
				luaL_error( L, "signal replication is unavailable" );
			Function( Address, Descriptor, &Payload, Target );
			return 0;
		}
		catch ( const std::exception& Error )
		{
			lua_pushstring( L, Error.what( ) );
		}
		lua_error( L );
	}
}

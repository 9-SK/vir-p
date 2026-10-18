#include "Helpers.h"
#include "core/Globals.h"
#include "core/Roblox/Update.h"
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <new>

namespace Instances::Helpers
{
	namespace
	{
		struct InstanceContext
		{
			lua_State* State = nullptr;
			LuaTable* Metatable = nullptr;
			int Tag = 0;
			int Table = 0;
			int Count = 0;
		};

		char g_HiddenUI;

		struct Member
		{
			uintptr_t Descriptor;
			uint32_t Kind;
			uint32_t Padding;
		};

		struct MemberMap
		{
			uint32_t Count;
			uint32_t Capacity;
			uintptr_t Slots;
			uintptr_t Entries;
		};

		struct Descriptor
		{
			uintptr_t Vtable;
			uintptr_t Name;
		};

		struct NativeString
		{
			char Buffer[ 16 ];
			size_t Length;
			size_t Capacity;
		};

		struct CallbackStorage
		{
			uintptr_t Buffer[ 7 ];
			uintptr_t Implementation;
		};

		struct LiveReference
		{
			uintptr_t Links[ 5 ];
			lua_State* Thread;
			int ThreadReference;
			int ValueReference;
		};

		struct WeakReference
		{
			uintptr_t Vtable;
			uintptr_t Flags;
			std::weak_ptr<LiveReference> Reference;
		};

		template <typename T>
		bool Read( uintptr_t Address, T& Value )
		{
			return Address && Address <= std::numeric_limits<uintptr_t>::max( ) - sizeof( T ) && ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), &Value, sizeof( T ), nullptr );
		}

		bool HasName( uintptr_t Address, const char* Name, size_t Length )
		{
			Descriptor Entry{ };
			NativeString String{ };

			if ( !Read( Address, Entry ) || !Read( Entry.Name, String ) || String.Length != Length || String.Length > String.Capacity || Length > 255 )
				return false;

			if ( String.Capacity < sizeof( String.Buffer ) )
				return Length < sizeof( String.Buffer ) && std::memcmp( String.Buffer, Name, Length ) == 0;

			uintptr_t Data = 0;
			std::memcpy( &Data, String.Buffer, sizeof( Data ) );
			char Buffer[ 256 ]{ };
			return Data && ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Data ), Buffer, Length, nullptr ) && std::memcmp( Buffer, Name, Length ) == 0;
		}

		std::shared_ptr<LiveReference> FindCallback( uintptr_t Instance, uintptr_t Descriptor )
		{
			uint8_t Asynchronous = 0;
			int32_t Member = 0;
			uintptr_t Setter = 0;

			if ( !Read( Descriptor + Update::Roblox::Reflection::Callback::Asynchronous, Asynchronous ) )
				return { };

			if ( Asynchronous )
			{
				if ( !Read( Descriptor + Update::Roblox::Reflection::Callback::Storage, Member ) )
					return { };
			}
			else if ( !Read( Descriptor + Update::Roblox::Reflection::Callback::Storage, Setter ) || !Setter || !Read( Setter + sizeof( uintptr_t ), Member ) )
				return { };

			if ( Member <= 0 || Member > 1048576 )
				return { };

			CallbackStorage Storage{ };
			uintptr_t Callback = 0;
			uintptr_t Vtable = 0;

			if ( !Read( Instance + Member, Storage ) || !Storage.Implementation || !Read( Storage.Implementation + sizeof( uintptr_t ) * ( Asynchronous ? 3 : 1 ), Callback ) || !Read( Callback, Storage ) || !Read( Storage.Implementation, Vtable ) )
				return { };

			uintptr_t Reference = 0;

			if ( Asynchronous && Vtable == Update::Roblox::Reflection::Callback::LuaAsync )
				Reference = Storage.Implementation + sizeof( uintptr_t );
			else if ( !Asynchronous && Vtable == Update::Roblox::Reflection::Callback::LuaSync )
			{
				if ( !Read( Storage.Implementation + sizeof( uintptr_t ), Reference ) )
					return { };
			}
			else
				return { };

			if ( !Reference )
				return { };

			return reinterpret_cast<const WeakReference*>( Reference )->Reference.lock( );
		}

		void PushGlobal( lua_State* L, const char* Name )
		{
			lua_xpush( L->global->mainthread, L, LUA_GLOBALSINDEX );
			lua_getfield( L, -1, Name );
			lua_remove( L, -2 );
		}

		void PushService( lua_State* L, const char* Name )
		{
			PushGlobal( L, "game" );
			lua_getfield( L, -1, "GetService" );
			lua_insert( L, -2 );
			lua_pushstring( L, Name );
			lua_call( L, 2, 1 );
		}

		uintptr_t PushLocalPlayer( lua_State* L )
		{
			PushService( L, "Players" );
			lua_getfield( L, -1, "LocalPlayer" );
			lua_remove( L, -2 );

			if ( lua_isnil( L, -1 ) )
				luaL_error( L, "LocalPlayer is unavailable" );

			return reinterpret_cast<uintptr_t>( CheckInstance( L, -1, "Player" ).get( ) );
		}

		bool CollectInstances( void* Context, lua_Page*, GCObject* Object )
		{
			auto* Instances = static_cast<InstanceContext*>( Context );
			lua_State* L = Instances->State;

			if ( Object->gch.tt != LUA_TUSERDATA || isdead( L->global, Object ) )
				return false;

			Udata* Userdata = gco2u( Object );

			if ( Userdata->tag != Instances->Tag || Userdata->metatable != Instances->Metatable || Userdata->len != sizeof( std::shared_ptr<uintptr_t> ) )
				return false;

			const auto* Reference = reinterpret_cast<const std::shared_ptr<uintptr_t>*>( Userdata->data );

			if ( !*Reference )
				return false;

			++Instances->Count;

			if ( Instances->Table )
			{
				setuvalue( L, L->top, Userdata );
				L->top++;
				lua_rawseti( L, Instances->Table, Instances->Count );
			}

			return false;
		}
	}

	const std::shared_ptr<uintptr_t>& CheckInstance( lua_State* L, int Index, const char* ClassName )
	{
		Index = lua_absindex( L, Index );
		luaL_checktype( L, Index, LUA_TUSERDATA );
		lua_rawcheckstack( L, 4 );
		PushGlobal( L, "game" );

		if ( lua_type( L, -1 ) != LUA_TUSERDATA )
			luaL_error( L, "DataModel is unavailable" );

		Udata* Reference = uvalue( luaA_toobject( L, -1 ) );
		Udata* Value = uvalue( luaA_toobject( L, Index ) );
		bool Valid = Value->tag == Reference->tag && Value->metatable == Reference->metatable && Value->len == sizeof( std::shared_ptr<uintptr_t> );
		lua_pop( L, 1 );

		if ( !Valid )
			luaL_argerror( L, Index, "expected an Instance" );

		const auto& Instance = *reinterpret_cast<const std::shared_ptr<uintptr_t>*>( Value->data );

		if ( !Instance )
			luaL_argerror( L, Index, "invalid Instance" );

		if ( ClassName )
		{
			lua_getfield( L, Index, "IsA" );
			lua_pushvalue( L, Index );
			lua_pushstring( L, ClassName );
			lua_call( L, 2, 1 );
			bool Matches = lua_toboolean( L, -1 );
			lua_pop( L, 1 );

			if ( !Matches )
				luaL_argerror( L, Index, ClassName );
		}

		return Instance;
	}

	bool IsClass( uintptr_t Instance, const char* Name )
	{
		uintptr_t Class = 0;
		return Instance && Read( Instance + Update::Roblox::Instance::ClassDescriptor, Class ) && HasName( Class, Name, std::strlen( Name ) );
	}

	uintptr_t FindMember( lua_State* L, uintptr_t Instance, const char* Name, size_t Length, uint32_t Kind )
	{
		uintptr_t Class = 0;
		MemberMap Map{ };

		if ( !Read( Instance + Update::Roblox::Instance::ClassDescriptor, Class ) || !Read( Class + Update::Roblox::Reflection::Members, Map ) || Map.Count > Map.Capacity || Map.Count > 65536 )
			luaL_error( L, "class members are unavailable" );

		for ( uint32_t Index = 0; Index < Map.Count; ++Index )
		{
			Member Entry{ };

			if ( !Read( Map.Entries + Index * sizeof( Member ), Entry ) )
				luaL_error( L, "class member is unavailable" );

			if ( Entry.Kind == Kind && HasName( Entry.Descriptor, Name, Length ) )
				return Entry.Descriptor;
		}

		return 0;
	}

	int PushCallback( lua_State* L )
	{
		const auto& Instance = CheckInstance( L, 1 );
		luaL_checktype( L, 2, LUA_TSTRING );
		size_t Length = 0;
		const char* Name = lua_tolstring( L, 2, &Length );
		uintptr_t Class = 0;
		MemberMap Map{ };

		if ( !Read( reinterpret_cast<uintptr_t>( Instance.get( ) ) + Update::Roblox::Instance::ClassDescriptor, Class ) || !Read( Class + Update::Roblox::Reflection::Members, Map ) || Map.Count > Map.Capacity || Map.Count > 65536 )
			luaL_error( L, "class members are unavailable" );

		for ( uint32_t Index = 0; Index < Map.Count; ++Index )
		{
			Member Entry{ };

			if ( !Read( Map.Entries + Index * sizeof( Member ), Entry ) )
				luaL_error( L, "class member is unavailable" );

			if ( !HasName( Entry.Descriptor, Name, Length ) )
				continue;

			if ( Entry.Kind != 4 )
				luaL_argerror( L, 2, "expected a callback property" );

			auto Reference = FindCallback( reinterpret_cast<uintptr_t>( Instance.get( ) ), Entry.Descriptor );

			if ( Reference && Reference->Thread && Reference->Thread->global == L->global && Reference->ValueReference > 0 )
				lua_getref( L, Reference->ValueReference );
			else
				lua_pushnil( L );

			return 1;
		}

		luaL_argerror( L, 2, "unknown callback property" );
	}

	int PushInstances( lua_State* L, bool NilOnly )
	{
		lua_rawcheckstack( L, 8 );
		PushGlobal( L, "game" );
		CheckInstance( L, -1 );
		InstanceContext Instances;
		Instances.State = L;
		Instances.Metatable = uvalue( luaA_toobject( L, -1 ) )->metatable;
		Instances.Tag = lua_userdatatag( L, -1 );

		lua_getfield( L, -1, "GetDescendants" );
		lua_pushvalue( L, -2 );
		lua_call( L, 1, 1 );

		if ( !lua_istable( L, -1 ) )
			luaL_error( L, "GetDescendants did not return a table" );

		luaM_visitgco( L, &Instances, CollectInstances );
		lua_createtable( L, Instances.Count, 0 );
		Instances.Table = lua_gettop( L );
		Instances.Count = 0;
		luaC_threadbarrier( L );
		luaM_visitgco( L, &Instances, CollectInstances );

		lua_createtable( L, Instances.Count, 0 );
		int Result = lua_gettop( L );
		lua_newtable( L );
		int Seen = lua_gettop( L );
		int Count = 0;

		for ( int Index = 1; Index <= Instances.Count; ++Index )
		{
			lua_rawgeti( L, Instances.Table, Index );
			const auto* Reference = static_cast<const std::shared_ptr<uintptr_t>*>( lua_touserdata( L, -1 ) );
			lua_pushlightuserdata( L, Reference->get( ) );
			lua_rawget( L, Seen );
			bool Include = lua_isnil( L, -1 );
			lua_pop( L, 1 );

			if ( Include && NilOnly )
			{
				lua_getfield( L, -1, "Parent" );
				Include = lua_isnil( L, -1 );
				lua_pop( L, 1 );
			}

			if ( Include )
			{
				Update::Roblox::Functions::PushInstance( L, Reference );

				if ( !lua_isnil( L, -1 ) )
				{
					lua_rawseti( L, Result, ++Count );
					lua_pushlightuserdata( L, Reference->get( ) );
					lua_pushboolean( L, true );
					lua_rawset( L, Seen );
				}
				else
					lua_pop( L, 1 );
			}

			lua_pop( L, 1 );
		}

		lua_pushvalue( L, Result );
		return 1;
	}

	int PushClone( lua_State* L, int Index )
	{
		Index = lua_absindex( L, Index );
		const auto& Reference = CheckInstance( L, Index );
		int Tag = lua_userdatatag( L, Index );

		if ( !lua_getuserdatadtor( L, Tag ) )
			luaL_error( L, "Instance destructor is unavailable" );

		void* Storage = lua_newuserdatatagged( L, sizeof( Reference ), Tag );
		new ( Storage ) std::shared_ptr<uintptr_t>( Reference );
		lua_getmetatable( L, Index );
		lua_setmetatable( L, -2 );
		return 1;
	}

	int PushHiddenUI( lua_State* L )
	{
		lua_rawcheckstack( L, 8 );
		PushService( L, "CoreGui" );
		int CoreGui = lua_gettop( L );
		lua_rawgetp( L, LUA_REGISTRYINDEX, &g_HiddenUI );

		if ( lua_istable( L, -1 ) )
		{
			lua_rawgeti( L, -1, 1 );
			bool Current = lua_tonumber( L, -1 ) == static_cast<double>( g_Env->SessionGeneration.load( ) );
			lua_pop( L, 1 );
			lua_rawgeti( L, -1, 2 );

			if ( Current && lua_isuserdata( L, -1 ) )
			{
				lua_getfield( L, -1, "Parent" );
				bool Attached = lua_rawequal( L, -1, CoreGui );
				lua_pop( L, 1 );

				if ( Attached )
					return 1;
			}
		}

		lua_settop( L, CoreGui );
		PushGlobal( L, "Instance" );
		lua_getfield( L, -1, "new" );
		lua_remove( L, -2 );
		lua_pushliteral( L, "Folder" );
		lua_call( L, 1, 1 );
		int Container = lua_gettop( L );
		PushService( L, "HttpService" );
		lua_getfield( L, -1, "GenerateGUID" );
		lua_insert( L, -2 );
		lua_pushboolean( L, false );
		lua_call( L, 2, 1 );
		lua_setfield( L, Container, "Name" );
		lua_pushboolean( L, true );
		lua_setfield( L, Container, "RobloxLocked" );
		lua_pushvalue( L, CoreGui );
		lua_setfield( L, Container, "Parent" );

		lua_createtable( L, 2, 0 );
		lua_pushnumber( L, static_cast<double>( g_Env->SessionGeneration.load( ) ) );
		lua_rawseti( L, -2, 1 );
		lua_pushvalue( L, Container );
		lua_rawseti( L, -2, 2 );
		lua_rawsetp( L, LUA_REGISTRYINDEX, &g_HiddenUI );
		lua_pushvalue( L, Container );
		return 1;
	}

	int FireClick( lua_State* L )
	{
		uintptr_t Detector = reinterpret_cast<uintptr_t>( CheckInstance( L, 1, "ClickDetector" ).get( ) );
		double Distance = std::numeric_limits<double>::infinity( );

		if ( !lua_isnoneornil( L, 2 ) )
		{
			luaL_checktype( L, 2, LUA_TNUMBER );
			Distance = lua_tonumber( L, 2 );

			if ( std::isnan( Distance ) || Distance < 0 )
				luaL_argerror( L, 2, "expected a nonnegative distance" );
		}

		const char* Event = "MouseClick";

		if ( !lua_isnoneornil( L, 3 ) )
		{
			luaL_checktype( L, 3, LUA_TSTRING );
			Event = lua_tostring( L, 3 );

			if ( std::strlen( Event ) != lua_objlen( L, 3 ) )
				luaL_argerror( L, 3, "invalid ClickDetector event" );
		}

		int Kind = -1;

		if ( std::strcmp( Event, "MouseClick" ) == 0 )
			Kind = 0;
		else if ( std::strcmp( Event, "RightMouseClick" ) == 0 )
			Kind = 1;
		else if ( std::strcmp( Event, "MouseHoverEnter" ) == 0 )
			Kind = 2;
		else if ( std::strcmp( Event, "MouseHoverLeave" ) == 0 )
			Kind = 3;
		else
			luaL_argerror( L, 3, "invalid ClickDetector event" );

		uintptr_t Player = PushLocalPlayer( L );
		float SimulatedDistance = std::isinf( Distance ) ? -std::numeric_limits<float>::infinity( ) : static_cast<float>( Distance );

		switch ( Kind )
		{
		case 0:
			Update::Roblox::Functions::MouseClick( Detector, SimulatedDistance, Player );
			break;
		case 1:
			Update::Roblox::Functions::RightMouseClick( Detector, SimulatedDistance, Player );
			break;
		case 2:
			Update::Roblox::Functions::MouseHoverEnter( Detector, Player );
			break;
		case 3:
			Update::Roblox::Functions::MouseHoverLeave( Detector, Player );
			break;
		}

		return 0;
	}

	int FirePrompt( lua_State* L )
	{
		uintptr_t Prompt = reinterpret_cast<uintptr_t>( CheckInstance( L, 1, "ProximityPrompt" ).get( ) );
		PushLocalPlayer( L );
		Update::Roblox::Functions::TriggerPrompt( Prompt );
		return 0;
	}

	int FireTouch( lua_State* L )
	{
		const auto& First = CheckInstance( L, 1, "BasePart" );
		const auto& Second = CheckInstance( L, 2, "BasePart" );
		bool End = false;

		if ( lua_type( L, 3 ) == LUA_TBOOLEAN )
			End = !lua_toboolean( L, 3 );
		else
		{
			luaL_checktype( L, 3, LUA_TNUMBER );
			double Toggle = lua_tonumber( L, 3 );

			if ( Toggle != 0 && Toggle != 1 )
				luaL_argerror( L, 3, "expected true, false, 0 or 1" );

			End = Toggle == 1;
		}

		if ( First.get( ) != Second.get( ) )
			Update::Roblox::Functions::TouchPair( &First, &Second, End );

		return 0;
	}
}

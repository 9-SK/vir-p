#include "Helper.h"
#include "client/Environment/Libraries/Instances/Helpers/Helpers.h"
#include "core/Roblox/Update.h"
#include "core/Roblox/Variant.h"
#include <VM/src/lstate.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace RBX
{
	struct SecurityContext
	{
		RBX::Identity Identity;
		void* ScriptContext;
		uint64_t Reserved;
		lua_State* State;
		bool Parallel;
		uint64_t Capabilities;
		uint64_t ( *ResolveCapabilities )( uint64_t, lua_State* );
	};

	static_assert( sizeof( SecurityContext ) == 64 );
	static_assert( offsetof( SecurityContext, State ) == 32 );
	static_assert( offsetof( SecurityContext, Capabilities ) == 48 );
	static_assert( offsetof( SecurityContext, ResolveCapabilities ) == 56 );
}

namespace Reflection::Helpers
{
	namespace
	{
		constexpr uint64_t g_InstanceCapabilities = 0x3FFFFFFFFFFF00;
		char g_Properties;

		enum class PropertyType : uint32_t
		{
			SystemAddress = 29,
			BinaryString = 30,
			Enum = 33,
			SharedString = 63
		};

		struct NativeString
		{
			char Buffer[ 16 ];
			size_t Length;
			size_t Capacity;
		};

		struct SharedString
		{
			uintptr_t Pointer = 0;

			~SharedString( )
			{
				if ( Pointer )
					Update::Roblox::Functions::DestroySharedString( &Pointer );
			}
		};

		struct Property
		{
			uintptr_t Descriptor = 0;
			uintptr_t Type = 0;
			uint32_t Flags = 0;
			PropertyType Kind{ };

			bool Scriptable( ) const
			{
				return ( Flags & 0x10 ) != 0;
			}
		};

		struct InstanceProperties
		{
			std::weak_ptr<uintptr_t> Owner;
			std::unordered_map<uintptr_t, bool> Values;
		};

		using PropertyMap = std::unordered_map<uintptr_t, InstanceProperties>;

		template <typename T>
		T Read( uintptr_t Address )
		{
			T Value{ };
			if ( !Address || Address > std::numeric_limits<uintptr_t>::max( ) - sizeof( T ) || !ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), &Value, sizeof( T ), nullptr ) )
				throw std::runtime_error( "property metadata is unavailable" );
			return Value;
		}

		Property FindProperty( lua_State* L, uintptr_t Instance )
		{
			luaL_checktype( L, 2, LUA_TSTRING );
			size_t Length = 0;
			const char* Name = lua_tolstring( L, 2, &Length );
			Property Result;
			Result.Descriptor = Instances::Helpers::FindMember( L, Instance, Name, Length, 0 );
			if ( Result.Descriptor )
			{
				Result.Type = Read<uintptr_t>( Result.Descriptor + Update::Roblox::Reflection::Property::Type );
				Result.Flags = Read<uint32_t>( Result.Descriptor + Update::Roblox::Reflection::Property::Flags );
				Result.Kind = Read<PropertyType>( Result.Type + Update::Roblox::Reflection::Property::TypeId );
			}
			return Result;
		}

		PropertyMap* GetProperties( lua_State* L, bool Create = false )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Properties );
			auto* Properties = static_cast<PropertyMap*>( lua_touserdata( L, -1 ) );
			lua_pop( L, 1 );
			if ( !Properties && Create )
			{
				void* Storage = lua_newuserdatadtor( L, sizeof( PropertyMap ), []( void* Data )
				{
					static_cast<PropertyMap*>( Data )->~PropertyMap( );
				} );
				Properties = new ( Storage ) PropertyMap{ };
				lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Properties );
			}
			return Properties;
		}

		bool GetScriptable( lua_State* L, uintptr_t Instance, const Property& Member )
		{
			PropertyMap* Properties = GetProperties( L );
			if ( Properties )
			{
				auto Entry = Properties->find( Instance );
				if ( Entry != Properties->end( ) && !Entry->second.Owner.expired( ) )
				{
					auto Value = Entry->second.Values.find( Member.Descriptor );
					if ( Value != Entry->second.Values.end( ) )
						return Value->second;
				}
			}
			return Member.Scriptable( );
		}

		void PushString( lua_State* L, const NativeString& Value )
		{
			if ( Value.Length > Value.Capacity || ( Value.Capacity < sizeof( Value.Buffer ) && Value.Length >= sizeof( Value.Buffer ) ) )
				luaL_error( L, "invalid native property string" );
			const char* Data = Value.Buffer;
			if ( Value.Capacity >= sizeof( Value.Buffer ) )
				std::memcpy( &Data, Value.Buffer, sizeof( Data ) );
			if ( !Data )
				luaL_error( L, "property string is unavailable" );
			lua_pushlstring( L, Data, Value.Length );
		}

		void PushEnum( lua_State* L, uintptr_t Instance, const Property& Member )
		{
			using Getter = uintptr_t ( * )( uintptr_t, uintptr_t );
			auto Function = Read<Getter>( Read<uintptr_t>( Member.Descriptor ) + Update::Roblox::Reflection::Enum::Get );
			const uintptr_t Item = Function ? Function( Member.Descriptor, Instance ) : 0;
			if ( !Item )
				luaL_error( L, "property contains an invalid enum value" );
			Update::Roblox::Functions::PushEnum( L, Item );
		}

		void WriteEnum( lua_State* L, uintptr_t Instance, const Property& Member )
		{
			const uintptr_t Vtable = Read<uintptr_t>( Member.Descriptor );
			bool Written = false;
			switch ( lua_type( L, 3 ) )
			{
			case LUA_TUSERDATA:
			{
				PushEnum( L, Instance, Member );
				const bool Matches = lua_type( L, -1 ) == LUA_TUSERDATA && lua_userdatatag( L, 3 ) == lua_userdatatag( L, -1 ) && lua_objlen( L, 3 ) == sizeof( uintptr_t );
				lua_pop( L, 1 );
				if ( !Matches )
					luaL_argerror( L, 3, "expected an EnumItem" );
				uintptr_t Item = 0;
				std::memcpy( &Item, lua_touserdata( L, 3 ), sizeof( Item ) );
				using Setter = bool ( * )( uintptr_t, uintptr_t, uintptr_t );
				auto Function = Read<Setter>( Vtable + Update::Roblox::Reflection::Enum::Set );
				Written = Item && Function && Function( Member.Descriptor, Instance, Item );
				break;
			}
			case LUA_TNUMBER:
			{
				const double Number = lua_tonumber( L, 3 );
				if ( !std::isfinite( Number ) || Number != std::trunc( Number ) || Number < INT32_MIN || Number > INT32_MAX )
					luaL_argerror( L, 3, "expected an enum value" );
				using Setter = bool ( * )( uintptr_t, uintptr_t, int32_t );
				auto Function = Read<Setter>( Vtable + Update::Roblox::Reflection::Enum::SetValue );
				Written = Function && Function( Member.Descriptor, Instance, static_cast<int32_t>( Number ) );
				break;
			}
			case LUA_TSTRING:
			{
				size_t Length = 0;
				const char* Name = lua_tolstring( L, 3, &Length );
				if ( std::memchr( Name, 0, Length ) )
					luaL_argerror( L, 3, "invalid enum name" );
				RBX::Variant Value;
				if ( !Update::Roblox::Functions::ReadVariant( L, 3, &Value, false ) )
					luaL_argerror( L, 3, "invalid enum name" );
				using Setter = bool ( * )( uintptr_t, uintptr_t, const void* );
				auto Function = Read<Setter>( Vtable + Update::Roblox::Reflection::Enum::SetName );
				Written = Function && Function( Member.Descriptor, Instance, Value.Data );
				break;
			}
			default:
				luaL_argerror( L, 3, "expected an EnumItem, number, or string" );
			}
			if ( !Written )
				luaL_argerror( L, 3, "invalid value for this enum property" );
		}

		void ReadProperty( lua_State* L, uintptr_t Instance, const Property& Member )
		{
			if ( Member.Kind == PropertyType::Enum )
			{
				PushEnum( L, Instance, Member );
				return;
			}
			using Getter = void ( * )( uintptr_t, uintptr_t, RBX::Variant* );
			auto Function = Read<Getter>( Read<uintptr_t>( Member.Descriptor ) + Update::Roblox::Reflection::Property::Get );
			if ( !Function )
				luaL_error( L, "property getter is unavailable" );
			RBX::Variant Value;
			Function( Member.Descriptor, Instance, &Value );
			if ( !Value.Type )
				luaL_error( L, "property getter returned no type" );
			switch ( Read<PropertyType>( Value.Type + Update::Roblox::Reflection::Property::TypeId ) )
			{
			case PropertyType::SystemAddress:
			{
				int32_t Address = 0;
				std::memcpy( &Address, Value.Data, sizeof( Address ) );
				lua_pushinteger( L, Address );
				break;
			}
			case PropertyType::BinaryString:
				PushString( L, *reinterpret_cast<const NativeString*>( Value.Data ) );
				break;
			case PropertyType::SharedString:
			{
				uintptr_t Pointer = 0;
				std::memcpy( &Pointer, Value.Data, sizeof( Pointer ) );
				if ( !Pointer )
					luaL_error( L, "shared property string is unavailable" );
				PushString( L, Read<NativeString>( Pointer + Update::Roblox::Reflection::SharedString::Data ) );
				break;
			}
			default:
				if ( Update::Roblox::Functions::PushVariant( &Value, L ) != 1 )
					luaL_error( L, "property conversion did not return one value" );
				break;
			}
		}

		const RBX::VariantOperations g_AddressOperations
		{
			[]( const void* Source, void* Target )
			{
				std::memcpy( Target, Source, sizeof( int32_t ) );
			},
			[]( void* Source, void* Target )
			{
				std::memcpy( Target, Source, sizeof( int32_t ) );
			},
			[]( void* )
			{
			}
		};

		void WriteProperty( lua_State* L, uintptr_t Instance, const Property& Member )
		{
			luaL_checkany( L, 3 );
			if ( Member.Kind == PropertyType::Enum )
			{
				WriteEnum( L, Instance, Member );
				return;
			}
			RBX::Variant Value;
			if ( Member.Kind == PropertyType::SystemAddress )
			{
				luaL_checktype( L, 3, LUA_TNUMBER );
				const double Number = lua_tonumber( L, 3 );
				if ( !std::isfinite( Number ) || Number != std::trunc( Number ) || Number < INT32_MIN || Number > INT32_MAX )
					luaL_argerror( L, 3, "expected a signed 32-bit address" );
				const int32_t Address = static_cast<int32_t>( Number );
				Value.Type = Member.Type;
				Value.Operations = &g_AddressOperations;
				std::memcpy( Value.Data, &Address, sizeof( Address ) );
			}
			else
			{
				if ( Member.Kind == PropertyType::BinaryString || Member.Kind == PropertyType::SharedString )
					luaL_checktype( L, 3, LUA_TSTRING );
				if ( !Update::Roblox::Functions::ReadVariant( L, 3, &Value, false ) )
					luaL_argerror( L, 3, "unsupported property value" );
				if ( Member.Kind == PropertyType::BinaryString )
					Value.Type = Member.Type;
				else if ( Member.Kind == PropertyType::SharedString )
				{
					SharedString Shared;
					Update::Roblox::Functions::CreateSharedString( &Shared.Pointer, Value.Data );
					Value.Reset( );
					Value.Type = Member.Type;
					Update::Roblox::Functions::AssignSharedString( &Value.Operations, &Shared.Pointer );
				}
			}
			using Setter = void ( * )( uintptr_t, uintptr_t, const RBX::Variant* );
			auto Function = Read<Setter>( Read<uintptr_t>( Member.Descriptor ) + Update::Roblox::Reflection::Property::Set );
			if ( !Function )
				luaL_error( L, "property setter is unavailable" );
			Function( Member.Descriptor, Instance, &Value );
		}

		void InvokeProperty( lua_State* L, uintptr_t Instance, const Property& Member, bool Write )
		{
			try
			{
				lua_rawcheckstack( L, 8 );
				if ( Write )
					WriteProperty( L, Instance, Member );
				else
					ReadProperty( L, Instance, Member );
				return;
			}
			catch ( const std::exception& Error )
			{
				lua_pushstring( L, Error.what( ) );
			}
			lua_error( L );
		}
	}

	int GetHiddenProperty( lua_State* L )
	{
		const auto& Instance = Instances::Helpers::CheckInstance( L, 1 );
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Instance.get( ) );
		const Property Member = FindProperty( L, Address );
		if ( !Member.Descriptor )
			luaL_argerror( L, 2, "property does not exist" );
		InvokeProperty( L, Address, Member, false );
		lua_pushboolean( L, !Member.Scriptable( ) );
		return 2;
	}

	int SetHiddenProperty( lua_State* L )
	{
		const auto& Instance = Instances::Helpers::CheckInstance( L, 1 );
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Instance.get( ) );
		const Property Member = FindProperty( L, Address );
		if ( !Member.Descriptor )
			luaL_argerror( L, 2, "property does not exist" );
		InvokeProperty( L, Address, Member, true );
		lua_pushboolean( L, !Member.Scriptable( ) );
		return 1;
	}

	int IsScriptable( lua_State* L )
	{
		const auto& Instance = Instances::Helpers::CheckInstance( L, 1 );
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Instance.get( ) );
		const Property Member = FindProperty( L, Address );
		if ( Member.Descriptor )
			lua_pushboolean( L, GetScriptable( L, Address, Member ) );
		else
			lua_pushnil( L );
		return 1;
	}

	int SetScriptable( lua_State* L )
	{
		const auto& Instance = Instances::Helpers::CheckInstance( L, 1 );
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Instance.get( ) );
		const Property Member = FindProperty( L, Address );
		luaL_checktype( L, 3, LUA_TBOOLEAN );
		if ( !Member.Descriptor )
		{
			lua_pushnil( L );
			return 1;
		}
		const bool Previous = GetScriptable( L, Address, Member );
		const bool Enabled = lua_toboolean( L, 3 ) != 0;
		PropertyMap* Properties = GetProperties( L, true );
		for ( auto Entry = Properties->begin( ); Entry != Properties->end( ); )
		{
			if ( Entry->second.Owner.expired( ) )
				Entry = Properties->erase( Entry );
			else
				++Entry;
		}
		if ( Enabled == Member.Scriptable( ) )
		{
			auto Entry = Properties->find( Address );
			if ( Entry != Properties->end( ) )
			{
				Entry->second.Values.erase( Member.Descriptor );
				if ( Entry->second.Values.empty( ) )
					Properties->erase( Entry );
			}
		}
		else
		{
			auto& Entry = ( *Properties )[ Address ];
			Entry.Owner = Instance;
			Entry.Values[ Member.Descriptor ] = Enabled;
		}
		lua_pushboolean( L, Previous );
		return 1;
	}

	bool AccessProperty( lua_State* L, bool Write )
	{
		PropertyMap* Properties = GetProperties( L );
		if ( !Properties || Properties->empty( ) || lua_type( L, 1 ) != LUA_TUSERDATA || lua_type( L, 2 ) != LUA_TSTRING )
			return false;
		const auto& Instance = Instances::Helpers::CheckInstance( L, 1 );
		const uintptr_t Address = reinterpret_cast<uintptr_t>( Instance.get( ) );
		auto Entry = Properties->find( Address );
		if ( Entry == Properties->end( ) )
			return false;
		if ( Entry->second.Owner.expired( ) )
		{
			Properties->erase( Entry );
			return false;
		}
		const Property Member = FindProperty( L, Address );
		auto Value = Entry->second.Values.find( Member.Descriptor );
		if ( Value == Entry->second.Values.end( ) )
			return false;
		if ( !Value->second )
			luaL_error( L, "property is not scriptable" );
		InvokeProperty( L, Address, Member, Write );
		return true;
	}

	int GetThreadIdentity( lua_State* L )
	{
		if ( !L->userdata )
			luaL_error( L, "thread identity is unavailable" );
		lua_pushnumber( L, L->userdata->identity.value );
		return 1;
	}

	int SetThreadIdentity( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TNUMBER );
		const double Level = lua_tonumber( L, 1 );
		if ( !L->userdata )
			luaL_error( L, "thread identity is unavailable" );

		RBX::Identity Identity = L->userdata->identity;
		double Converted = std::isfinite( Level ) ? std::fmod( std::trunc( Level ), 4294967296.0 ) : 0;
		Identity.value = static_cast<int64_t>( Converted < 0 ? Converted + 4294967296.0 : Converted );
		const uint64_t Capabilities = g_InstanceCapabilities | Update::Roblox::Functions::IdentityCapabilities( &Identity );
		RBX::SecurityContext* Context = Update::Roblox::Functions::CurrentSecurityContext( L );
		if ( !Context )
			luaL_error( L, "security context is unavailable" );

		L->userdata->identity.value = Identity.value;
		L->userdata->capabilities = Capabilities;
		Context->Identity.value = Identity.value;
		Context->Capabilities = Capabilities;
		return 0;
	}
}

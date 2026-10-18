#include "Drawing.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include <cmath>
#include <cstring>
#include <lua.h>
#include <lualib.h>
#include <new>
#include <string_view>
#include <variant>

/* ye cool */
namespace Drawing
{
	namespace
	{
		using namespace Helpers;
		std::shared_ptr<Renderer> g_Renderer;
		char g_Objects;
		char g_Metatable;
		char g_Vector;
		char g_Color;

		struct Handle
		{
			std::shared_ptr<Object> Value;
			std::weak_ptr<Renderer> Owner;
		};

		using Member = std::variant<bool Object::*, int Object::*, float Object::*, Vector Object::*, Color Object::*, std::string Object::*>;
		struct Property
		{
			const char* Name;
			unsigned Types;
			Member Field;
		};

		constexpr unsigned Mask( Kind Type )
		{
			return 1u << static_cast<unsigned>( Type );
		}

		constexpr unsigned Shapes = Mask( Kind::Circle ) | Mask( Kind::Square ) | Mask( Kind::Quad ) | Mask( Kind::Triangle );
		const Property g_Properties[] =
			{
				{ "Visible", 127, &Object::Visible },
				{ "ZIndex", 127, &Object::ZIndex },
				{ "Transparency", 127, &Object::Transparency },
				{ "Color", 127, &Object::Tint },
				{ "Thickness", Shapes | Mask( Kind::Line ), &Object::Thickness },
				{ "Filled", Shapes, &Object::Filled },
				{ "Radius", Mask( Kind::Circle ), &Object::Radius },
				{ "NumSides", Mask( Kind::Circle ), &Object::NumSides },
				{ "Position", Mask( Kind::Text ) | Mask( Kind::Image ) | Mask( Kind::Circle ) | Mask( Kind::Square ), &Object::Position },
				{ "Size", Mask( Kind::Image ) | Mask( Kind::Square ), &Object::Size },
				{ "Size", Mask( Kind::Text ), &Object::TextSize },
				{ "Text", Mask( Kind::Text ), &Object::Text },
				{ "Font", Mask( Kind::Text ), &Object::Font },
				{ "Center", Mask( Kind::Text ), &Object::Center },
				{ "Outline", Mask( Kind::Text ), &Object::Outline },
				{ "OutlineColor", Mask( Kind::Text ), &Object::OutlineColor },
				{ "Rounding", Mask( Kind::Image ), &Object::Rounding } };

		void PushObjects( lua_State* L )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Objects );
			if ( lua_istable( L, -1 ) )
				return;
			lua_pop( L, 1 );
			lua_newtable( L );
			lua_newtable( L );
			lua_pushliteral( L, "k" );
			lua_setfield( L, -2, "__mode" );
			lua_setmetatable( L, -2 );
			lua_pushvalue( L, -1 );
			lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Objects );
		}

		Handle* Find( lua_State* L, int Index )
		{
			if ( lua_type( L, Index ) != LUA_TUSERDATA )
				return nullptr;
			Index = lua_absindex( L, Index );
			PushObjects( L );
			lua_pushvalue( L, Index );
			lua_rawget( L, -2 );
			bool Valid = lua_toboolean( L, -1 );
			lua_pop( L, 2 );
			return Valid ? static_cast<Handle*>( lua_touserdata( L, Index ) ) : nullptr;
		}

		Handle* Check( lua_State* L )
		{
			auto* Value = Find( L, 1 );
			if ( !Value )
				luaL_argerror( L, 1, "expected a Drawing object" );
			return Value;
		}

		Object Snapshot( lua_State* L )
		{
			auto* Value = Check( L );
			auto Owner = Value->Owner.lock( );
			if ( Owner )
			{
				std::lock_guard<std::mutex> Lock( Owner->Mutex( ) );
				return *Value->Value;
			}
			return *Value->Value;
		}

		std::string_view Key( lua_State* L )
		{
			luaL_checktype( L, 2, LUA_TSTRING );
			size_t Length = 0;
			const char* Value = lua_tolstring( L, 2, &Length );
			return { Value, Length };
		}

		const Property* Lookup( Kind Type, std::string_view Name )
		{
			for ( const auto& Value : g_Properties )
				if ( ( Value.Types & Mask( Type ) ) && Name == Value.Name )
					return &Value;
			return nullptr;
		}

		int PointIndex( Kind Type, std::string_view Name )
		{
			if ( Type == Kind::Line )
				return Name == "From" ? 0 : Name == "To" ? 1
														 : -1;
			if ( ( Type == Kind::Triangle || Type == Kind::Quad ) && Name.size( ) == 6 && Name.substr( 0, 5 ) == "Point" )
			{
				int Index = Name[ 5 ] - 'A';
				if ( Index >= 0 && Index < ( Type == Kind::Quad ? 4 : 3 ) )
					return Index;
			}
			return -1;
		}

		void Push( lua_State* L, bool Value )
		{
			lua_pushboolean( L, Value );
		}
		void Push( lua_State* L, int Value )
		{
			lua_pushinteger( L, Value );
		}
		void Push( lua_State* L, float Value )
		{
			lua_pushnumber( L, Value );
		}
		void Push( lua_State* L, const std::string& Value )
		{
			lua_pushlstring( L, Value.data( ), Value.size( ) );
		}

		void Push( lua_State* L, Vector Value )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Vector );
			lua_pushnumber( L, Value.X );
			lua_pushnumber( L, Value.Y );
			lua_call( L, 2, 1 );
		}

		void Push( lua_State* L, Color Value )
		{
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Color );
			lua_pushnumber( L, Value.R );
			lua_pushnumber( L, Value.G );
			lua_pushnumber( L, Value.B );
			lua_call( L, 3, 1 );
		}

		float Number( lua_State* L, int Index )
		{
			double Value = luaL_checknumber( L, Index );
			if ( !std::isfinite( Value ) || std::abs( Value ) > 10000000 )
				luaL_argerror( L, Index, "expected a finite number within drawing limits" );
			return static_cast<float>( Value );
		}

		float Component( lua_State* L, const char* Name )
		{
			lua_getfield( L, 3, Name );
			float Value = Number( L, -1 );
			lua_pop( L, 1 );
			return Value;
		}

		void Read( lua_State* L, bool& Value )
		{
			luaL_checktype( L, 3, LUA_TBOOLEAN );
			Value = lua_toboolean( L, 3 );
		}

		void Read( lua_State* L, float& Value )
		{
			Value = Number( L, 3 );
		}
		void Read( lua_State* L, int& Value )
		{
			float NumberValue = Number( L, 3 );
			if ( std::trunc( NumberValue ) != NumberValue )
				luaL_argerror( L, 3, "expected an integer" );
			Value = static_cast<int>( NumberValue );
		}

		void Read( lua_State* L, Vector& Value )
		{
			if ( std::strcmp( luaL_typename( L, 3 ), "Vector2" ) != 0 )
				luaL_argerror( L, 3, "expected Vector2" );
			Value = { Component( L, "X" ), Component( L, "Y" ) };
		}

		void Read( lua_State* L, Color& Value )
		{
			if ( std::strcmp( luaL_typename( L, 3 ), "Color3" ) != 0 )
				luaL_argerror( L, 3, "expected Color3" );
			Value = { Component( L, "R" ), Component( L, "G" ), Component( L, "B" ) };
		}

		void Read( lua_State* L, std::string& Value )
		{
			luaL_checktype( L, 3, LUA_TSTRING );
			size_t Length = 0;
			const char* Data = lua_tolstring( L, 3, &Length );
			if ( Length > 32 * 1024 * 1024 )
				luaL_argerror( L, 3, "drawing string exceeds size limit" );
			Value.assign( Data, Length );
		}

		int Destroy( lua_State* L )
		{
			auto* Value = Check( L );
			if ( auto Owner = Value->Owner.lock( ) )
				Owner->Remove( Value->Value );
			return 0;
		}

		int Get( lua_State* L )
		{
			Object Value = Snapshot( L );
			auto Name = Key( L );
			if ( Name == "__OBJECT_EXISTS" )
				Push( L, Value.Exists );
			else if ( Name == "Destroy" || Name == "Remove" )
				lua_pushcfunction( L, Destroy, "Destroy" );
			else if ( !Value.Exists )
				luaL_error( L, "Drawing object has been destroyed" );
			else if ( const auto* Property = Lookup( Value.Type, Name ) )
				std::visit( [ & ]( auto Field )
							{
								Push( L, Value.*Field );
							},
							Property->Field );
			else if ( int Index = PointIndex( Value.Type, Name ); Index >= 0 )
				Push( L, Value.Points[ Index ] );
			else if ( Name == "TextBounds" && Value.Type == Kind::Text )
				Push( L, g_Renderer->Measure( Value ) );
			else if ( Name == "Data" && Value.Type == Kind::Image )
				Push( L, Value.Data ? *Value.Data : std::string( ) );
			else
				luaL_error( L, "invalid Drawing property" );
			return 1;
		}

		int Set( lua_State* L )
		{
			Object Value = Snapshot( L );
			auto Name = Key( L );
			if ( !Value.Exists )
				luaL_error( L, "Drawing object has been destroyed" );
			if ( const auto* Property = Lookup( Value.Type, Name ) )
				std::visit( [ & ]( auto Field )
							{
								Read( L, Value.*Field );
							},
							Property->Field );
			else if ( int Index = PointIndex( Value.Type, Name ); Index >= 0 )
				Read( L, Value.Points[ Index ] );
			else if ( Name == "Data" && Value.Type == Kind::Image )
			{
				std::string Bytes;
				Read( L, Bytes );
				Value.Data = std::make_shared<const std::string>( std::move( Bytes ) );
			}
			else
				luaL_error( L, "invalid or read-only Drawing property" );
			if ( Value.Transparency < 0 || Value.Transparency > 1 || Value.Font < 0 || Value.Font > 3 || Value.NumSides < 3 || Value.NumSides > 4096 || Value.Thickness < 0 || Value.Radius < 0 || Value.TextSize <= 0 || Value.TextSize > 4096 || Value.Rounding < 0 || Value.Size.X < 0 || Value.Size.Y < 0 )
				luaL_argerror( L, 3, "Drawing property is out of range" );
			auto* Target = Check( L );
			if ( auto Owner = Target->Owner.lock( ) )
				Owner->Update( Target->Value, std::move( Value ) );
			return 0;
		}

		int New( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TSTRING );
			size_t Length = 0;
			const char* Name = lua_tolstring( L, 1, &Length );
			const char* Types[] = { "Line", "Text", "Image", "Circle", "Square", "Quad", "Triangle" };
			int Type = -1;
			for ( int Index = 0; Index < 7; ++Index )
				if ( std::string_view( Name, Length ) == Types[ Index ] )
					Type = Index;
			if ( Type < 0 )
				luaL_argerror( L, 1, "invalid Drawing type" );
			auto* Value = new ( lua_newuserdatadtor( L, sizeof( Handle ), []( void* Data )
													 {
														 static_cast<Handle*>( Data )->~Handle( );
													 } ) ) Handle{ g_Renderer->Create( static_cast<Kind>( Type ) ), g_Renderer };
			lua_rawgetp( L, LUA_REGISTRYINDEX, &g_Metatable );
			lua_setmetatable( L, -2 );
			PushObjects( L );
			lua_pushvalue( L, -2 );
			lua_pushboolean( L, true );
			lua_rawset( L, -3 );
			lua_pop( L, 1 );
			return 1;
		}

		int IsObject( lua_State* L )
		{
			auto* Value = Find( L, 1 );
			bool Exists = false;
			if ( Value )
				if ( auto Owner = Value->Owner.lock( ) )
				{
					std::lock_guard<std::mutex> Lock( Owner->Mutex( ) );
					Exists = Value->Value->Exists;
				}
			lua_pushboolean( L, Exists );
			return 1;
		}

		int Clear( lua_State* )
		{
			g_Renderer->Clear( );
			return 0;
		}

		int SetCaptureExcluded( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TBOOLEAN );
			g_Renderer->SetCaptureExcluded( lua_toboolean( L, 1 ) );
			return 0;
		}

		int IsCaptureExcluded( lua_State* L )
		{
			lua_pushboolean( L, g_Renderer->IsCaptureExcluded( ) );
			return 1;
		}
	}

	void Initialize( )
	{
		Shutdown( );
		g_Renderer = std::make_shared<Helpers::Renderer>( );
		g_Renderer->Start( );
		lua_State* L = g_Alpen->State( );
		for ( const auto& Entry : { std::pair{ "Vector2", &g_Vector }, std::pair{ "Color3", &g_Color } } )
		{
			lua_getglobal( L, Entry.first );
			lua_getfield( L, -1, "new" );
			lua_rawsetp( L, LUA_REGISTRYINDEX, Entry.second );
			lua_pop( L, 1 );
		}
		lua_newtable( L );
		lua_pushcfunction( L, Get, "__index" );
		lua_setfield( L, -2, "__index" );
		lua_pushcfunction( L, Set, "__newindex" );
		lua_setfield( L, -2, "__newindex" );
		lua_pushliteral( L, "Drawing" );
		lua_setfield( L, -2, "__type" );
		lua_pushliteral( L, "Drawing" );
		lua_setfield( L, -2, "__metatable" );
		lua_rawsetp( L, LUA_REGISTRYINDEX, &g_Metatable );
		lua_newtable( L );
		lua_pushcfunction( L, New, "new" );
		lua_setfield( L, -2, "new" );
		lua_pushcfunction( L, SetCaptureExcluded, "setcaptureexcluded" );
		lua_setfield( L, -2, "setcaptureexcluded" );
		lua_pushcfunction( L, IsCaptureExcluded, "iscaptureexcluded" );
		lua_setfield( L, -2, "iscaptureexcluded" );
		lua_newtable( L );
		const char* Fonts[] = { "UI", "System", "Plex", "Monospace" };
		for ( int Index = 0; Index < 4; ++Index )
		{
			lua_pushinteger( L, Index );
			lua_setfield( L, -2, Fonts[ Index ] );
		}
		lua_setreadonly( L, -1, true );
		lua_setfield( L, -2, "Fonts" );
		lua_setglobal( L, "Drawing" );
		Env::RegisterGlobal( "isrenderobj", IsObject );
		Env::RegisterGlobal( "getrenderproperty", Get );
		Env::RegisterGlobal( "setrenderproperty", Set );
		Env::RegisterGlobal( "cleardrawcache", Clear );
	}

	void Shutdown( )
	{
		if ( g_Renderer )
			g_Renderer->Stop( );
		g_Renderer.reset( );
	}
}

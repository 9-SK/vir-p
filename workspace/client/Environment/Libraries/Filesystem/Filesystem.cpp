#include "Filesystem.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"
#include "client/Environment/Libraries/Closures/Helpers/Helpers.h"
#include "core/Execution/Execution.h"
#include "core/Scheduler/Helper.h"
#include <exception>
#include <functional>
#include <lua.h>
#include <lualib.h>
#include <string>

namespace Filesystem
{
	namespace
	{
		std::string String( lua_State* L, int Index )
		{
			luaL_checktype( L, Index, LUA_TSTRING );
			size_t Length = 0;
			const char* Value = lua_tolstring( L, Index, &Length );
			return std::string( Value, Length );
		}

		int Dispatch( lua_State* L, const std::function<Yielded( )>& Work )
		{
			if ( lua_isyieldable( L ) )
				return Yielding::YieldExec( L, Work );
			Yielded Complete;
			std::string Error;
			try
			{
				Complete = Work( );
			}
			catch ( const std::exception& Exception )
			{
				Error = Exception.what( );
			}
			if ( !Error.empty( ) )
				luaL_error( L, "%s", Error.c_str( ) );
			return Complete( L );
		}

		int Read( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 return [ Data = Helpers::Workspace( )->Read( Path ) ]( lua_State* State )
								 {
									 lua_pushlstring( State, Data.data( ), Data.size( ) );
									 return 1;
								 };
							 } );
		}

		template <bool Append>
		int Write( lua_State* L )
		{
			luaL_checktype( L, 1, LUA_TSTRING );
			luaL_checktype( L, 2, LUA_TSTRING );
			return Dispatch( L, [ Path = String( L, 1 ), Data = String( L, 2 ) ]( ) -> Yielded
							 {
								 Helpers::Workspace( )->Write( Path, Data, Append );
								 return []( lua_State* )
								 {
									 return 0;
								 };
							 } );
		}

		template <bool Directory>
		int Exists( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 return [ Value = Helpers::Workspace( )->Exists( Path, Directory ) ]( lua_State* State )
								 {
									 lua_pushboolean( State, Value );
									 return 1;
								 };
							 } );
		}

		template <bool Directory>
		int Remove( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 Helpers::Workspace( )->Remove( Path, Directory );
								 return []( lua_State* )
								 {
									 return 0;
								 };
							 } );
		}

		int MakeFolder( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 Helpers::Workspace( )->MakeFolder( Path );
								 return []( lua_State* )
								 {
									 return 0;
								 };
							 } );
		}

		int List( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 return [ Entries = Helpers::Workspace( )->List( Path ) ]( lua_State* State )
								 {
									 lua_createtable( State, static_cast<int>( Entries.size( ) ), 0 );
									 int Index = 1;
									 for ( const auto& Entry : Entries )
									 {
										 lua_pushlstring( State, Entry.data( ), Entry.size( ) );
										 lua_rawseti( State, -2, Index++ );
									 }
									 return 1;
								 };
							 } );
		}

		int Load( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 const auto Source = Helpers::Workspace( )->Read( Path );
								 return [ Bytecode = Alpen::Execution::Compile( Source ), Name = "@" + Path ]( lua_State* State )
								 {
									 const int Count = Closures::Helpers::LoadBytecode( State, Bytecode, Name, { } );
									 if ( Count != 1 )
										 lua_error( State );
									 return Count;
								 };
							 } );
		}

		int CustomAsset( lua_State* L )
		{
			return Dispatch( L, [ Path = String( L, 1 ) ]( ) -> Yielded
							 {
								 return [ Id = Helpers::CustomAsset( Helpers::Workspace( ), Path ) ]( lua_State* State )
								 {
									 lua_pushlstring( State, Id.data( ), Id.size( ) );
									 return 1;
								 };
							 } );
		}
	}

	void Initialize( )
	{
		Env::RegisterGlobal( "readfile", Read );
		Env::RegisterGlobal( "writefile", Write<false> );
		Env::RegisterGlobal( "appendfile", Write<true> );
		Env::RegisterGlobal( "delfile", Remove<false> );
		Env::RegisterGlobal( "delfolder", Remove<true> );
		Env::RegisterGlobal( "isfile", Exists<false> );
		Env::RegisterGlobal( "isfolder", Exists<true> );
		Env::RegisterGlobal( "makefolder", MakeFolder );
		Env::RegisterGlobal( "listfiles", List );
		Env::RegisterGlobal( "loadfile", Load );
		Env::RegisterGlobal( "getcustomasset", CustomAsset );
	}
}

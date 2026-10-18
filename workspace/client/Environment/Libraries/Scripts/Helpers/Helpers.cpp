#include "Helpers.h"
#include "client/Environment/Libraries/Instances/Helpers/Helpers.h"
#include "core/Roblox/Update.h"
#include "core/Scheduler/Helper.h"
#include <Luau/Bytecode.h>
#include <Luau/BytecodeUtils.h>
#include <VM/src/lapi.h>
#include <VM/src/lgc.h>
#include <VM/src/lmem.h>
#include <VM/src/lobject.h>
#include <VM/src/lstate.h>
#include <VM/src/ltable.h>
#include <algorithm>
#include <bcrypt.h>
#include <cstring>
#include <limits>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace Scripts::Helpers
{
	constexpr size_t MaximumBytecodeSize = 64 * 1024 * 1024;

	struct BytecodeString
	{
		unsigned char Buffer[ 16 ];
		size_t Length;
		size_t Capacity;
	};

	struct ThreadContext
	{
		lua_State* State = nullptr;
		std::vector<std::shared_ptr<uintptr_t>> Scripts;
		std::unordered_set<uintptr_t*> Seen;
	};

	struct CallerContext
	{
		lua_State* State;
		std::vector<LuaTable*> Environments;
		size_t Rank = std::numeric_limits<size_t>::max( );
		std::shared_ptr<uintptr_t> Script;
	};

	struct ModuleState
	{
		uint8_t Status;
		uint8_t Error;
		uint16_t Flags;
		int Export;
		uintptr_t Thread;
		uintptr_t Global;
		int Result;
		int WeakResult;
		uintptr_t Waiters;
	};

	struct ModuleMap
	{
		float LoadFactor;
		uint32_t Padding;
		uintptr_t Head;
		size_t Count;
	};

	struct ModuleNode
	{
		uintptr_t Next;
		uintptr_t Previous;
		uintptr_t Global;
		ModuleState State;
	};

	struct ModuleTree
	{
		uintptr_t Head;
		size_t Count;
	};

	struct ModuleLinks
	{
		uintptr_t Left;
		uintptr_t Parent;
		uintptr_t Right;
		uint8_t Color;
		uint8_t Sentinel;
		unsigned char Padding[ 6 ];
	};

	static_assert( sizeof( ModuleState ) == 40 );
	static_assert( sizeof( ModuleNode ) == 64 );

	template <typename T>
	bool Read( uintptr_t Address, T& Value )
	{
		return Address && Address <= std::numeric_limits<uintptr_t>::max( ) - sizeof( T ) && ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), &Value, sizeof( T ), nullptr );
	}

	bool IsLoadedModule( lua_State* L, uintptr_t Instance )
	{
		if ( !Instance || Instance > std::numeric_limits<uintptr_t>::max( ) - Update::Roblox::Scripts::ModuleScript::SharedStates )
			return false;

		uintptr_t Owner = 0;
		ModuleState State{ };

		if ( !Read( Instance + Update::Roblox::Scripts::ModuleScript::GlobalState, Owner ) )
			luaL_error( L, "module state is unavailable" );

		if ( Owner & ~uintptr_t( 7 ) )
		{
			if ( !Read( Instance + Update::Roblox::Scripts::ModuleScript::State, State ) )
				luaL_error( L, "module state is unavailable" );

			if ( State.Status == 3 )
				return true;
		}

		uintptr_t Address = 0;

		if ( !Read( Instance + Update::Roblox::Scripts::ModuleScript::SharedStates, Address ) )
			luaL_error( L, "module states are unavailable" );

		Address &= ~uintptr_t( 7 );

		if ( !Address )
			return false;

		ModuleMap Map{ };
		uintptr_t Node = 0;

		if ( !Read( Address, Map ) || Map.Count > 1048576 || !Read( Map.Head, Node ) )
			luaL_error( L, "invalid module states" );

		for ( size_t Index = 0; Index < Map.Count && Node != Map.Head; ++Index )
		{
			ModuleNode Entry{ };

			if ( !Read( Node, Entry ) )
				luaL_error( L, "invalid module state entry" );

			if ( Entry.Global && Entry.State.Status == 3 )
				return true;

			Node = Entry.Next;
		}

		return false;
	}

	bool CollectThreads( void* Context, lua_Page*, GCObject* Object )
	{
		ThreadContext* Threads = static_cast<ThreadContext*>( Context );

		if ( Object->gch.tt != LUA_TTHREAD || isdead( Threads->State->global, Object ) )
			return false;

		lua_State* Thread = gco2th( Object );

		if ( Thread->global != Threads->State->global || !Thread->userdata )
			return false;

		std::shared_ptr<uintptr_t> Script = Thread->userdata->script.lock( );

		if ( Script && Threads->Seen.insert( Script.get( ) ).second )
			Threads->Scripts.push_back( std::move( Script ) );

		return false;
	}

	int PushRunningScripts( lua_State* L )
	{
		lua_rawcheckstack( L, 5 );
		ThreadContext Threads;
		Threads.State = L;
		luaM_visitgco( L, &Threads, CollectThreads );
		lua_createtable( L, static_cast<int>( Threads.Scripts.size( ) ), 0 );
		int Result = lua_gettop( L );
		int Count = 0;

		for ( const auto& Script : Threads.Scripts )
		{
			Update::Roblox::Functions::PushInstance( L, &Script );

			if ( lua_isuserdata( L, -1 ) )
			{
				lua_getfield( L, -1, "ClassName" );
				bool Include = IsScript( lua_tostring( L, -1 ) );
				lua_pop( L, 1 );

				if ( Include )
				{
					lua_rawseti( L, Result, ++Count );
					continue;
				}
			}

			lua_pop( L, 1 );
		}

		return 1;
	}

	int PushLoadedModules( lua_State* L )
	{
		if ( !L->userdata || !L->userdata->shared || !L->userdata->shared->scriptContext )
			luaL_error( L, "script context is unavailable" );

		lua_rawcheckstack( L, 5 );
		uintptr_t Context = reinterpret_cast<uintptr_t>( L->userdata->shared->scriptContext );
		ModuleTree Tree{ };
		ModuleLinks Head{ };

		if ( Context > std::numeric_limits<uintptr_t>::max( ) - Update::Roblox::ScriptContext::Modules || !Read( Context + Update::Roblox::ScriptContext::Modules, Tree ) || Tree.Count > 1048576 || !Read( Tree.Head, Head ) || !Head.Sentinel )
			luaL_error( L, "invalid module registry" );

		std::vector<uintptr_t> Pending;
		std::unordered_set<uintptr_t> Seen;
		std::vector<std::shared_ptr<uintptr_t>> Modules;
		Pending.push_back( Head.Parent );

		while ( !Pending.empty( ) )
		{
			uintptr_t Address = Pending.back( );
			Pending.pop_back( );

			if ( Address == Tree.Head )
				continue;

			ModuleLinks Node{ };

			if ( !Seen.insert( Address ).second || Seen.size( ) > Tree.Count || !Read( Address, Node ) || Node.Sentinel )
				luaL_error( L, "invalid module registry entry" );

			const auto* Reference = reinterpret_cast<const std::weak_ptr<uintptr_t>*>( Address + sizeof( ModuleLinks ) );
			std::shared_ptr<uintptr_t> Module = Reference->lock( );

			if ( Module && Instances::Helpers::IsClass( reinterpret_cast<uintptr_t>( Module.get( ) ), "ModuleScript" ) && IsLoadedModule( L, reinterpret_cast<uintptr_t>( Module.get( ) ) ) )
				Modules.push_back( std::move( Module ) );

			Pending.push_back( Node.Left );
			Pending.push_back( Node.Right );
		}

		ThreadContext Threads;
		Threads.State = L;
		luaM_visitgco( L, &Threads, CollectThreads );

		for ( auto& Script : Threads.Scripts )
		{
			uintptr_t Instance = reinterpret_cast<uintptr_t>( Script.get( ) );

			if ( Instances::Helpers::IsClass( Instance, "ModuleScript" ) && IsLoadedModule( L, Instance ) )
				Modules.push_back( std::move( Script ) );
		}

		lua_createtable( L, static_cast<int>( Modules.size( ) ), 0 );
		int Result = lua_gettop( L );
		int Count = 0;
		std::unordered_set<uintptr_t*> Included;

		for ( const auto& Module : Modules )
		{
			if ( !Included.insert( Module.get( ) ).second )
				continue;

			Update::Roblox::Functions::PushInstance( L, &Module );

			if ( lua_isnil( L, -1 ) )
				lua_pop( L, 1 );
			else
			{
				lua_rawseti( L, Result, ++Count );
			}
		}

		Instances::Helpers::PushInstances( L );
		int Candidates = lua_gettop( L );
		int Total = lua_objlen( L, Candidates );

		for ( int Index = 1; Index <= Total; ++Index )
		{
			lua_rawgeti( L, Candidates, Index );
			const auto& Reference = Instances::Helpers::CheckInstance( L, -1 );
			bool Module = Instances::Helpers::IsClass( reinterpret_cast<uintptr_t>( Reference.get( ) ), "ModuleScript" );

			if ( Module )
			{
				if ( !Included.contains( Reference.get( ) ) && IsLoadedModule( L, reinterpret_cast<uintptr_t>( Reference.get( ) ) ) )
				{
					Included.insert( Reference.get( ) );
					lua_rawseti( L, Result, ++Count );
					continue;
				}
			}

			lua_pop( L, 1 );
		}

		lua_settop( L, Result );

		return 1;
	}

	std::string ReadBytecode( lua_State* L )
	{
		luaL_checktype( L, 1, LUA_TUSERDATA );

		if ( std::strcmp( luaL_typename( L, 1 ), "Instance" ) != 0 )
			luaL_argerror( L, 1, "expected a Script, LocalScript or ModuleScript" );

		lua_getfield( L, 1, "ClassName" );
		const char* ClassName = lua_tostring( L, -1 );

		if ( !IsScript( ClassName ) )
			luaL_argerror( L, 1, "expected a Script, LocalScript or ModuleScript" );

		uintptr_t ByteCode = Update::Roblox::Scripts::Script::ByteCode;

		if ( std::strcmp( ClassName, "ModuleScript" ) == 0 )
			ByteCode = Update::Roblox::Scripts::ModuleScript::ByteCode;
		else if ( std::strcmp( ClassName, "LocalScript" ) == 0 )
			ByteCode = Update::Roblox::Scripts::LocalScript::ByteCode;

		lua_pop( L, 1 );
		Udata* Userdata = uvalue( luaA_toobject( L, 1 ) );

		if ( Userdata->len < sizeof( uintptr_t ) )
			luaL_argerror( L, 1, "invalid script instance" );

		uintptr_t Instance = 0;
		std::memcpy( &Instance, Userdata->data, sizeof( Instance ) );

		if ( !Instance )
			luaL_argerror( L, 1, "invalid script instance" );

		uintptr_t Storage = 0;

		if ( Instance > std::numeric_limits<uintptr_t>::max( ) - ByteCode || !ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Instance + ByteCode ), &Storage, sizeof( Storage ), nullptr ) )
			luaL_error( L, "script bytecode is unavailable" );

		if ( !Storage )
			return { };

		BytecodeString Bytecode{ };

		if ( Storage > std::numeric_limits<uintptr_t>::max( ) - Update::Roblox::Scripts::ByteCodeString || !ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Storage + Update::Roblox::Scripts::ByteCodeString ), &Bytecode, sizeof( Bytecode ), nullptr ) || Bytecode.Length > Bytecode.Capacity || Bytecode.Length > MaximumBytecodeSize || ( Bytecode.Capacity < sizeof( Bytecode.Buffer ) && Bytecode.Length >= sizeof( Bytecode.Buffer ) ) )
			luaL_error( L, "invalid script bytecode storage" );

		if ( !Bytecode.Length )
			return { };

		std::string Bytes;

		try
		{
			Bytes.resize( Bytecode.Length );
		}
		catch ( const std::bad_alloc& )
		{
			luaL_error( L, "not enough memory for script bytecode" );
		}

		if ( Bytecode.Capacity >= sizeof( Bytecode.Buffer ) )
		{
			uintptr_t Address = 0;
			std::memcpy( &Address, Bytecode.Buffer, sizeof( Address ) );
			if ( !Address || Address > std::numeric_limits<uintptr_t>::max( ) - Bytecode.Length || !ReadProcessMemory( GetCurrentProcess( ), reinterpret_cast<const void*>( Address ), Bytes.data( ), Bytes.size( ), nullptr ) )
				luaL_error( L, "script bytecode is unavailable" );
		}
		else
			std::memcpy( Bytes.data( ), Bytecode.Buffer, Bytes.size( ) );

		return Bytes;
	}

	/* needs update every upd */
	std::string DecodeBytecode( std::string Bytes )
	{
		if ( Bytes.size( ) <= 8 || Bytes.size( ) > MaximumBytecodeSize )
			throw std::runtime_error( "invalid script bytecode container" );

		uint64_t Marker = 0;
		std::memcpy( &Marker, Bytes.data( ), sizeof( Marker ) );

		if ( Marker == 0xe009325b4a107a52 )
		{
			Bytes.erase( 0, 8 );
			return Bytes;
		}

		constexpr unsigned char Magic[] = { 'R', 'S', 'B', '1' };
		unsigned char Key[ 4 ];

		for ( size_t Index = 0; Index < sizeof( Key ); ++Index )
			Key[ Index ] = static_cast<unsigned char>( ( static_cast<unsigned char>( Bytes[ Index ] ) ^ Magic[ Index ] ) - Index * 41 );

		for ( size_t Index = 0; Index < Bytes.size( ); ++Index )
			Bytes[ Index ] ^= static_cast<unsigned char>( Key[ Index & 3 ] + Index * 41 );

		uint32_t ExpectedHash = 0;
		std::memcpy( &ExpectedHash, Key, sizeof( ExpectedHash ) );

		if ( Update::Roblox::Functions::BytecodeHash( Bytes.data( ), static_cast<int>( Bytes.size( ) ), 42 ) != ExpectedHash )
			throw std::runtime_error( "script bytecode checksum mismatch" );

		uint32_t Length = 0;
		std::memcpy( &Length, Bytes.data( ) + 4, sizeof( Length ) );

		if ( !Length )
		{
			Bytes.erase( 0, 8 );
			return Bytes;
		}

		if ( Length > MaximumBytecodeSize )
			throw std::runtime_error( "script bytecode exceeds the size limit" );

		uint32_t Frame = 0;

		if ( Bytes.size( ) <= 12 )
			throw std::runtime_error( "truncated script bytecode frame" );

		std::memcpy( &Frame, Bytes.data( ) + 8, sizeof( Frame ) );

		if ( Frame != 0xfd2fb528 )
			throw std::runtime_error( "invalid script bytecode compression" );

		std::string Result( Length, '\0' );

		if ( Update::Roblox::Functions::BytecodeDecompress( Result.data( ), Result.size( ), Bytes.data( ) + 8, Bytes.size( ) - 8 ) != Result.size( ) )
			throw std::runtime_error( "script bytecode decompression failed" );

		return Result;
	}

	void RemapBytecode( lua_State* L, uint32_t* Code, size_t Count )
	{
		const auto* Opcodes = reinterpret_cast<const uint8_t*>( Update::LuaU::OpCodeLookupTable );

		for ( size_t Index = 0; Index < Count; )
		{
			uint8_t Encoded = LUAU_INSN_OP( Code[ Index ] );
			uint8_t Opcode = static_cast<uint8_t>( Encoded * 203 );

			if ( Opcode >= LOP__COUNT )
				luaL_error( L, "invalid script opcode" );

			size_t Length = Luau::getOpLength( static_cast<LuauOpcode>( Opcode ) );

			if ( Length > Count - Index )
				luaL_error( L, "truncated script instruction" );

			Code[ Index ] = ( Code[ Index ] & ~uint32_t( 255 ) ) | Opcodes[ Encoded ];
			Index += Length;
		}
	}

	int LoadClosure( lua_State* L, const std::string& Bytes, const std::string& Name, const std::shared_ptr<uintptr_t>& Script )
	{
		lua_rawcheckstack( L, 5 );
		lua_newtable( L );
		int Environment = lua_gettop( L );
		Update::Roblox::Functions::PushInstance( L, &Script );
		lua_setfield( L, Environment, "script" );
		lua_newtable( L );
		lua_pushvalue( L, LUA_GLOBALSINDEX );
		lua_setfield( L, -2, "__index" );
		lua_setmetatable( L, Environment );
		lua_setsafeenv( L, Environment, false );

		if ( luau_load( L, Name.c_str( ), Bytes.data( ), static_cast<size_t>( Bytes.size( ) ), Environment ) != LUA_OK )
			lua_error( L );

		Closure* Function = clvalue( luaA_toobject( L, -1 ) );
		ProtoCapsEqual( Function->l.p, &g_Env->MaxAvailableInstanceCapabilities );
		return 1;
	}

	int PushDecoded( lua_State* L, bool Closure )
	{
		std::string Bytes = ReadBytecode( L );

		if ( Bytes.empty( ) )
		{
			lua_pushnil( L );
			return 1;
		}

		std::string Name;
		std::shared_ptr<uintptr_t> Script;

		if ( Closure )
		{
			lua_getfield( L, 1, "GetFullName" );
			lua_pushvalue( L, 1 );
			lua_call( L, 1, 1 );
			Name = "@";
			Name += luaL_checkstring( L, -1 );
			lua_pop( L, 1 );
			Udata* Userdata = uvalue( luaA_toobject( L, 1 ) );

			if ( Userdata->len != sizeof( Script ) )
				luaL_argerror( L, 1, "invalid script reference" );

			Script = *static_cast<std::shared_ptr<uintptr_t>*>( lua_touserdata( L, 1 ) );
		}

		auto Decode = [ Bytes = std::make_shared<std::string>( std::move( Bytes ) ), Name = std::move( Name ), Script = std::move( Script ), Closure ]( ) -> Yielded
		{
			std::string Result;
			std::string Error;

			try
			{
				Result = DecodeBytecode( std::move( *Bytes ) );
			}
			catch ( const std::exception& Exception )
			{
				Error = Exception.what( );
			}

			return [ Result = std::move( Result ), Error = std::move( Error ), Name, Script, Closure ]( lua_State* State )
			{
				if ( !Error.empty( ) )
					luaL_error( State, "%s", Error.c_str( ) );

				if ( Closure )
					return LoadClosure( State, Result, Name, Script );
				else
					lua_pushlstring( State, Result.data( ), Result.size( ) );
				return 1;
			};
		};

		if ( lua_isyieldable( L ) )
			return Yielding::YieldExec( L, Decode );

		return Decode( )( L );
	}

	int PushBytecode( lua_State* L )
	{
		return PushDecoded( L, false );
	}

	int PushClosure( lua_State* L )
	{
		return PushDecoded( L, true );
	}

	int PushHash( lua_State* L )
	{
		std::string Bytes = ReadBytecode( L );

		if ( Bytes.empty( ) )
		{
			lua_pushnil( L );
			return 1;
		}

		unsigned char Digest[ 48 ]{ };

		BCRYPT_ALG_HANDLE Algorithm = nullptr;
		BCRYPT_HASH_HANDLE HashHandle = nullptr;
		NTSTATUS Status = BCryptOpenAlgorithmProvider( &Algorithm, BCRYPT_SHA384_ALGORITHM, nullptr, 0 );

		if ( Status >= 0 )
			Status = BCryptCreateHash( Algorithm, &HashHandle, nullptr, 0, nullptr, 0, 0 );

		if ( Status >= 0 )
			Status = BCryptHashData( HashHandle, reinterpret_cast<unsigned char*>( Bytes.data( ) ), static_cast<ULONG>( Bytes.size( ) ), 0 );

		if ( Status >= 0 )
			Status = BCryptFinishHash( HashHandle, Digest, sizeof( Digest ), 0 );

		if ( HashHandle )
			BCryptDestroyHash( HashHandle );

		if ( Algorithm )
			BCryptCloseAlgorithmProvider( Algorithm, 0 );

		if ( Status < 0 )
			luaL_error( L, "SHA-384 hashing failed" );

		constexpr char Digits[] = "0123456789abcdef";
		char Hash[ 96 ];

		for ( size_t Index = 0; Index < sizeof( Digest ); ++Index )
		{
			Hash[ Index * 2 ] = Digits[ Digest[ Index ] >> 4 ];
			Hash[ Index * 2 + 1 ] = Digits[ Digest[ Index ] & 15 ];
		}

		lua_pushlstring( L, Hash, sizeof( Hash ) );
		return 1;
	}

	bool IsScript( const char* ClassName )
	{
		return ClassName && ( std::strcmp( ClassName, "Script" ) == 0 || std::strcmp( ClassName, "LocalScript" ) == 0 || std::strcmp( ClassName, "ModuleScript" ) == 0 );
	}

	bool IsCoreScript( lua_State* L, int Index ) /* no offset */
	{
		lua_pushvalue( L, Index );

		while ( lua_isuserdata( L, -1 ) )
		{
			lua_getfield( L, -1, "ClassName" );
			const char* ClassName = lua_tostring( L, -1 );
			bool Core = ClassName && ( std::strcmp( ClassName, "CoreScript" ) == 0 || std::strcmp( ClassName, "CoreGui" ) == 0 || std::strcmp( ClassName, "CorePackages" ) == 0 || std::strcmp( ClassName, "ScriptContext" ) == 0 );
			lua_pop( L, 1 );

			if ( Core )
			{
				lua_pop( L, 1 );
				return true;
			}

			lua_getfield( L, -1, "Parent" );
			lua_remove( L, -2 );
		}

		lua_pop( L, 1 );
		return false;
	}

	struct SearchContext
	{
		global_State* Global = nullptr;
		uintptr_t Instance = 0;
		LuaTable* Environment = nullptr;
	};

	bool FindThread( void* Context, lua_Page*, GCObject* Object )
	{
		SearchContext* Search = static_cast<SearchContext*>( Context );

		if ( Search->Environment || Object->gch.tt != LUA_TTHREAD || isdead( Search->Global, Object ) )
			return false;

		lua_State* Thread = gco2th( Object );

		if ( Thread->global != Search->Global || !Thread->userdata || !Thread->gt )
			return false;

		std::shared_ptr<uintptr_t> Script = Thread->userdata->script.lock( );

		if ( !Script || reinterpret_cast<uintptr_t>( Script.get( ) ) != Search->Instance )
			return false;

		Search->Environment = Thread->gt;
		return false;
	}

	LuaTable* FindEnvironment( lua_State* State, uintptr_t Instance )
	{
		if ( !State )
			return nullptr;

		SearchContext Search;
		Search.Global = State->global;
		Search.Instance = Instance;

		luaM_visitgco( State, &Search, FindThread );
		return Search.Environment;
	}

	void PushScript( lua_State* L, lua_State* Thread )
	{
		if ( !Thread || Thread->global != L->global || !Thread->userdata )
		{
			lua_pushnil( L );
			return;
		}

		std::shared_ptr<uintptr_t> Script = Thread->userdata->script.lock( );

		if ( !Script )
		{
			lua_pushnil( L );
			return;
		}

		lua_rawcheckstack( L, 4 );
		Update::Roblox::Functions::PushInstance( L, &Script );
	}

	int PushCallingScript( lua_State* L, lua_State* Thread )
	{
		if ( !Thread )
			Thread = L;
		lua_rawcheckstack( L, 4 );
		lua_pushliteral( L, "script" );
		TString* Key = tsvalue( luaA_toobject( L, -1 ) );
		CallerContext Context{ L };
		bool Found = false;

		for ( ptrdiff_t Depth = Thread->ci - Thread->base_ci; Depth >= 0; --Depth )
		{
			CallInfo* Frame = Thread->base_ci + Depth;

			if ( !ttisfunction( Frame->func ) )
				continue;

			Closure* Function = clvalue( Frame->func );
			LuaTable* Environment = Function->env;

			if ( Frame == Thread->ci || !Environment || std::find( Context.Environments.begin( ), Context.Environments.end( ), Environment ) != Context.Environments.end( ) )
				continue;

			Context.Environments.push_back( Environment );
			const TValue* Value = luaH_getstr( Environment, Key );

			if ( !ttisuserdata( Value ) )
				continue;

			luaC_threadbarrier( L );
			setobj2s( L, L->top, Value );
			L->top++;

			if ( std::strcmp( luaL_typename( L, -1 ), "Instance" ) == 0 && uvalue( Value )->len == sizeof( std::shared_ptr<uintptr_t> ) )
			{
				const auto& Script = *reinterpret_cast<const std::shared_ptr<uintptr_t>*>( uvalue( Value )->data );
				uintptr_t Instance = reinterpret_cast<uintptr_t>( Script.get( ) );

				if ( Instances::Helpers::IsClass( Instance, "LocalScript" ) || Instances::Helpers::IsClass( Instance, "Script" ) || Instances::Helpers::IsClass( Instance, "ModuleScript" ) )
				{
					Found = true;
					Context.Rank = Context.Environments.size( ) - 1;
					break;
				}
			}

			lua_pop( L, 1 );
		}

		if ( Context.Rank != 0 && !Context.Environments.empty( ) )
		{
			luaM_visitgco( L, &Context, []( void* Data, lua_Page*, GCObject* Object )
						   {
							   auto* Context = static_cast<CallerContext*>( Data );

							   if ( Object->gch.tt != LUA_TTHREAD || isdead( Context->State->global, Object ) )
								   return false;

							   lua_State* Thread = gco2th( Object );

							   if ( !Thread->userdata )
								   return false;

							   auto Position = std::find( Context->Environments.begin( ), Context->Environments.end( ), Thread->gt );
							   size_t Rank = Position - Context->Environments.begin( );

							   if ( Position == Context->Environments.end( ) || Rank >= Context->Rank )
								   return false;

							   if ( auto Script = Thread->userdata->script.lock( ) )
							   {
								   Context->Script = std::move( Script );
								   Context->Rank = Rank;
							   }

							   return false;
						   } );
		}

		if ( Context.Script )
		{
			if ( Found )
				lua_pop( L, 1 );

			Update::Roblox::Functions::PushInstance( L, &Context.Script );
			return 1;
		}

		if ( Found )
			return 1;

		PushScript( L, Thread );
		return 1;
	}

	int PushInstances( lua_State* L )
	{
		Instances::Helpers::PushInstances( L );
		int All = lua_gettop( L );
		int Total = lua_objlen( L, All );
		lua_newtable( L );
		int Result = lua_gettop( L );
		int Count = 0;

		for ( int Index = 1; Index <= Total; ++Index )
		{
			lua_rawgeti( L, All, Index );
			lua_getfield( L, -1, "ClassName" );
			bool Include = IsScript( lua_tostring( L, -1 ) );
			lua_pop( L, 1 );

			if ( Include && !IsCoreScript( L, -1 ) )
				lua_rawseti( L, Result, ++Count );
			else
				lua_pop( L, 1 );
		}

		return 1;
	}
}

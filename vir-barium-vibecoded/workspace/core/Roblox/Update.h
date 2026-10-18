#pragma once
// viridian - engine offsets for the 0.743 reference runtime.
// client: 0.743.0.7431134 / version-11f155f3450f4c8e (deploy 2026-10-15)
//
// PORT STATE (2026-10-18). The base runtime targets 0.741; these entries are
// the ones carried to 0.743 and how each was confirmed:
//
//   TaskScheduler::Pointer  0x8d71348  VERIFIED live (WHSJ job located by exact
//                                        type name, vtable swapped, step ran)
//   DataModel::FakeDataModel 0x8dc8000  VERIFIED live (injector probe: fake->dm
//                                        walk answered, sentinel 31)
//   RealDataModel 0x1f8, GameLoaded 0x5d0 (sentinel 31), ChildrenStart 0x78,
//   JobsStart 0xc8, JobsEnd 0xd0, JobTypeName 0xf8, Job ScriptContext 0x1c0,
//   SC2Resume 0x8f0                        UNCHANGED across the two generations
//
// Entries still carrying the 0.741 value are marked STALE. They are only
// reached when a script actually calls the matching library, and every call
// site is guarded, so an unported value fails soft instead of corrupting.
// DataModel::ScriptContext (0x440) is the one live unknown on the init path -
// boot.log prints what it reads so it can be corrected from a real run.
#include <cstdint>
#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Encryptions.h"
#include <Windows.h>

#define volt( Address ) ( Address + reinterpret_cast<uintptr_t>( GetModuleHandleA( nullptr ) ) )

struct lua_State;

namespace RBX
{
	struct Identity;
	struct SecurityContext;
	struct Variant;
}

namespace Update
{

	namespace LuaU
	{

		const uintptr_t LuaO_nilobject = volt( 0x667F848 ); // was 0x6504708
		const uintptr_t LuaH_dummynode = volt( 0x667F2E8 ); // was 0x6500E48
		const uintptr_t LuaVMLoad = volt( 0x41A3610 ); // was 0x4188AF0
		const uintptr_t OpCodeLookupTable = volt( 0x71433E0 ); // was 0x6F7FC40
		const uintptr_t LuauExecute = volt( 0x265F2B0 ); // was 0x25CB0D0
		const uintptr_t SCResume = volt( 0x4258330 ); // was 0x423C6F0
	}

	namespace Roblox
	{

		const uintptr_t Print = volt( 0x1CE58F0 ); // was 0x1C65A30
		const uintptr_t LuaDThrow = volt( 0x262DDC0 ); // was 0x259B470
		constexpr uintptr_t SC2Resume = 0x8f0;
		constexpr uintptr_t RequireBypass = 0xA81; // was 0x975

		constexpr uintptr_t ChildrenStart = 0x78;
		constexpr uintptr_t GameLoaded = 0x5d0;

		namespace DataModel
		{

			inline const uintptr_t FakeDataModel = volt( 0x8DC8000 ); // was 0x8BCFD50 - VERIFIED live
			inline constexpr uintptr_t RealDataModel = 0x1f8;
			inline constexpr uintptr_t ScriptContext = 0x440; // UNVERIFIED on 0.743 - boot.log reports the read
		}

		namespace Security
		{

			inline const uintptr_t CurrentContext = volt( 0x4185e60 ); // STALE 0.741

			inline const uintptr_t IdentityCapabilities = volt( 0x1D241C0 ); // was 0x1CA23E0
		}

		namespace Network
		{

			inline constexpr uint32_t TimeStamp = 0xb43cfb4c;
			inline const uintptr_t SerializePing = volt( 0x4815630 ); // STALE 0.741
			inline const uintptr_t SendRaw = volt( 0x27b0310 ); // STALE 0.741
			inline const uintptr_t SendStream = volt( 0x27b05b0 ); // STALE 0.741
			inline const uintptr_t SendShared = volt( 0x27b0800 ); // STALE 0.741
			inline constexpr uint8_t SerializeEntry[] = { 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57 };
			inline constexpr uint8_t RawEntry[] = { 0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xa8, 0, 0, 0 };
			inline constexpr uint8_t StreamEntry[] = { 0x40, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xb0, 0, 0, 0 };
			inline constexpr uint8_t SharedEntry[] = { 0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xa8, 0, 0, 0, 0 };

			namespace Stream
			{

				inline constexpr uintptr_t Data = 0x10;
				inline constexpr uintptr_t Size = 0x30;
				inline constexpr uintptr_t Capacity = 0x34;
				inline constexpr uintptr_t Unaligned = 0x3c;
			}

			namespace AbstractStream
			{

				inline constexpr uintptr_t Data = 0x08;
				inline constexpr uintptr_t Size = 0x10;
			}
		}

		namespace Flags
		{

			inline const uintptr_t UseRbxTransportClient = volt( 0x8694e30 ); // STALE 0.741
			inline const uintptr_t TransportRegistration = volt( 0x3a6baab ); // STALE 0.741
		}

		namespace Instance
		{

			inline constexpr uintptr_t ClassDescriptor = 0x18;

			inline const uintptr_t PushInstance = volt( 0x7a3390 ); // STALE 0.741 - unrecovered on 0.743

			inline const uintptr_t Cache = volt( 0x417a040 ); // STALE 0.741
		}

		namespace Reflection
		{

			inline constexpr uintptr_t Members = 0x1d8;

			namespace Property
			{

				inline constexpr uintptr_t Type = 0x70;
				inline constexpr uintptr_t Flags = 0x94;
				inline constexpr uintptr_t TypeId = 0x30;

				inline constexpr uintptr_t Get = 0x80;
				inline constexpr uintptr_t Set = 0x90;
			}

			namespace Variant
			{

				inline const uintptr_t Push = volt( 0x40f4e00 ); // STALE 0.741

				inline const uintptr_t Read = volt( 0x40ed300 ); // STALE 0.741
			}

			namespace Enum
			{

				inline constexpr uintptr_t Get = 0x110;
				inline constexpr uintptr_t Set = 0x118;
				inline constexpr uintptr_t SetValue = 0x108;
				inline constexpr uintptr_t SetName = 0xc8;
				inline const uintptr_t Push = volt( 0x935350 ); // STALE 0.741
			}

			namespace SharedString
			{

				inline const uintptr_t Create = volt( 0x4977d80 ); // STALE 0.741

				inline const uintptr_t Assign = volt( 0x9a2790 ); // STALE 0.741
				inline const uintptr_t Destroy = volt( 0x997fc0 ); // STALE 0.741

				inline constexpr uintptr_t Data = 0x18;
			}

			namespace Callback
			{

				inline constexpr uintptr_t Asynchronous = 0x80;
				inline constexpr uintptr_t Storage = 0x88;

				inline const uintptr_t LuaAsync = volt( 0x6dd7c28 ); // STALE 0.741
				inline const uintptr_t LuaSync = volt( 0x6dd7cb0 ); // STALE 0.741
			}
		}

		namespace Actors
		{

			inline constexpr uintptr_t VmOwner = 0x1b0;
			inline constexpr uintptr_t Pool = 0x558;
			inline constexpr uintptr_t PoolStride = 0x60;
			inline constexpr uintptr_t Guard = 0x618;
			inline constexpr uintptr_t VmState = 0x1f8;
			inline constexpr uintptr_t ActorVm = 0x230;
			inline constexpr uintptr_t Execution = 0x108;
			inline constexpr uintptr_t Parallel = 0x545;
			inline constexpr uintptr_t Queue = 0xcc0;
			inline constexpr uintptr_t LiveThread = 0x28;
			inline constexpr uintptr_t Parent = 0x68;
			inline constexpr uintptr_t ParentLocked = 0x9a;
			inline constexpr uintptr_t ParentLockedMask = 1;
			inline const uintptr_t CreateThreadRef = volt( 0x414d940 ); // STALE 0.741
			inline const uintptr_t LockThreadRef = volt( 0xc8fe50 ); // STALE 0.741
			inline const uintptr_t DestroyThreadRef = volt( 0x7fcc00 ); // STALE 0.741
			inline const uintptr_t QueueParallel = volt( 0x42b1ec0 ); // STALE 0.741
			inline const uintptr_t GetVm = volt( 0x41921c0 ); // STALE 0.741
		}

		namespace Signals
		{

			inline const uintptr_t Tag = volt( 0x8bb8000 ); /* xref -> SGN */ // STALE 0.741

			inline const uintptr_t Disconnect = volt( 0x496cdc0 ); // STALE 0.741
			inline const uintptr_t IsConnected = volt( 0x496cf50 ); // STALE 0.741

			inline const uintptr_t Begin = volt( 0x496d640 ); // STALE 0.741
			inline const uintptr_t End = volt( 0x732bb0 ); // STALE 0.741
			inline const uintptr_t ReleaseNode = volt( 0x7200e0 ); // STALE 0.741
			inline const uintptr_t ReleaseGraph = volt( 0x720050 ); // STALE 0.741

			inline const uintptr_t FreeNode = volt( 0x496e030 ); // STALE 0.741
			inline const uintptr_t FreeGraph = volt( 0x496db20 ); // STALE 0.741

			inline const uintptr_t Insert = volt( 0x496d450 ); // STALE 0.741
			inline const uintptr_t Guard = volt( 0x496cd10 ); // STALE 0.741
			inline constexpr uintptr_t Lock = 0x89f0;
			inline constexpr uintptr_t Unlock = 0x8b30;

			inline const uintptr_t DestroyLua = volt( 0x7b3150 ); /* ret 0 */ // STALE 0.741
			inline const uintptr_t LuaSlot = volt( 0x6dd7a00 ); // STALE 0.741
			inline const uintptr_t LuaSlotProtected = volt( 0x6dd78a0 ); // protec-> end-> 40-bts  // STALE 0.741
			namespace Touch
			{

				inline const uintptr_t Destroy = volt( 0xe00970 ); // STALE 0.741

				inline const uintptr_t VectorCallback = volt( 0x6c60d98 ); // STALE 0.741
				inline const uintptr_t SpanCallback = volt( 0x6c60cb0 ); // STALE 0.741
			}

			inline const uintptr_t WaitSlot = volt( 0x6dd7660 ); // STALE 0.741
			inline constexpr uintptr_t WaitingThread = 0x38;
			inline constexpr uintptr_t Callback = 0x68;
			inline constexpr uintptr_t CallbackKind = 0x50;

			inline constexpr uintptr_t CachedThread = 0x90;

			inline constexpr uintptr_t Cancelled = 0x20;
			inline constexpr uintptr_t Once = 0xa5;
			inline constexpr uintptr_t Consumed = 0xa6;

			inline constexpr uintptr_t Parameters = 0x50;
			inline constexpr uintptr_t Publish = 0x18;
		}

		namespace ClickDetector
		{

			inline const uintptr_t MouseClick = volt( 0x3BD1060 ); // was 0x3BC8C60
			inline const uintptr_t RightMouseClick = volt( 0x3BD1200 ); // was 0x3BC8E00

			inline const uintptr_t MouseHoverEnter = volt( 0x3bca250 ); // STALE 0.741
			inline const uintptr_t MouseHoverLeave = volt( 0x3bca3f0 ); // STALE 0.741
		}

		namespace ProximityPrompt
		{

			inline const uintptr_t Trigger = volt( 0x30D1720 ); // was 0x30E9D90
		}

		namespace BasePart
		{

			inline const uintptr_t TouchPair = volt( 0x33fe930 ); // STALE 0.741
		}

		namespace Bytecode
		{

			inline const uintptr_t Hash = volt( 0x2f34e60 ); // STALE 0.741
			inline const uintptr_t Decompress = volt( 0x21cc420 ); // STALE 0.741
		}

		namespace Scripts
		{

			inline constexpr uintptr_t ByteCodeString = 0x18;

			namespace LocalScript
			{

				inline constexpr uintptr_t ByteCode = 0x180;
				inline constexpr uintptr_t GUID = 0xc0;
				inline constexpr uintptr_t Hash = 0x190;
			}

			namespace ModuleScript
			{

				inline constexpr uintptr_t GlobalState = 0x158;
				inline constexpr uintptr_t State = 0x160;
				inline constexpr uintptr_t SharedStates = 0x188;
				inline constexpr uintptr_t ByteCode = 0x128;
				inline constexpr uintptr_t GUID = 0xc0;
				inline constexpr uintptr_t Hash = 0x350;
				inline constexpr uintptr_t IsCoreScript = 0x0;
			}

			namespace Script
			{

				inline constexpr uintptr_t ByteCode = 0x180;
				inline constexpr uintptr_t GUID = 0xc0;
				inline constexpr uintptr_t Hash = 0x190;
			}
		}

		namespace ScriptContext
		{

			inline constexpr uintptr_t Modules = 0x9e8;
		}

		namespace TaskScheduler
		{

			const uintptr_t Pointer = volt( 0x8D71348 ); // was 0x8B79128 - VERIFIED live
			constexpr uintptr_t JobsStart = 0xc8;
			constexpr uintptr_t JobsEnd = 0xd0;
			constexpr uintptr_t JobTypeName = 0xF8;
			constexpr uintptr_t ScriptContext = 0x1c0;
		}

		namespace Functions
		{

			inline auto SignalDisconnect = reinterpret_cast<void ( * )( uintptr_t* )>( Signals::Disconnect );
			inline auto SignalConnected = reinterpret_cast<bool ( * )( uintptr_t* )>( Signals::IsConnected );
			inline auto SignalBegin = reinterpret_cast<void ( * )( uintptr_t, void* )>( Signals::Begin );
			inline auto SignalEnd = reinterpret_cast<void ( * )( void* )>( Signals::End );
			inline auto ReleaseSignalNode = reinterpret_cast<void ( * )( uintptr_t* )>( Signals::ReleaseNode );
			inline auto ReleaseSignalGraph = reinterpret_cast<void ( * )( uintptr_t* )>( Signals::ReleaseGraph );
			inline auto FreeSignalNode = reinterpret_cast<void ( * )( uintptr_t )>( Signals::FreeNode );
			inline auto FreeSignalGraph = reinterpret_cast<void ( * )( uintptr_t )>( Signals::FreeGraph );
			inline auto InsertSignalNode = reinterpret_cast<void ( * )( uintptr_t, uintptr_t )>( Signals::Insert );
			inline auto SignalGuard = reinterpret_cast<uintptr_t ( * )( )>( Signals::Guard );
			inline auto LockSignal = reinterpret_cast<void ( * )( uintptr_t )>( Signals::Lock );
			inline auto UnlockSignal = reinterpret_cast<void ( * )( uintptr_t )>( Signals::Unlock );
			inline auto PushVariant = reinterpret_cast<int ( __fastcall* )( const RBX::Variant*, lua_State* )>( Reflection::Variant::Push );
			inline auto ReadVariant = reinterpret_cast<bool ( __fastcall* )( lua_State*, int, RBX::Variant*, bool )>( Reflection::Variant::Read );
			inline auto PushEnum = reinterpret_cast<void ( __fastcall* )( lua_State*, uintptr_t )>( Reflection::Enum::Push );
			inline auto CreateSharedString = reinterpret_cast<void* ( __fastcall* )( void*, void* )>( Reflection::SharedString::Create );
			inline auto AssignSharedString = reinterpret_cast<void* ( __fastcall* )( void*, void* )>( Reflection::SharedString::Assign );
			inline auto DestroySharedString = reinterpret_cast<void ( __fastcall* )( void* )>( Reflection::SharedString::Destroy );
			inline auto CurrentSecurityContext = reinterpret_cast<RBX::SecurityContext* ( __fastcall* )( lua_State* )>( Security::CurrentContext );
			inline auto IdentityCapabilities = reinterpret_cast<uint64_t ( __fastcall* )( const RBX::Identity* )>( Security::IdentityCapabilities );
			inline auto MouseClick = reinterpret_cast<void( __fastcall* )( uintptr_t, float, uintptr_t )>( ClickDetector::MouseClick );
			inline auto RightMouseClick = reinterpret_cast<void( __fastcall* )( uintptr_t, float, uintptr_t )>( ClickDetector::RightMouseClick );
			inline auto MouseHoverEnter = reinterpret_cast<void( __fastcall* )( uintptr_t, uintptr_t )>( ClickDetector::MouseHoverEnter );
			inline auto MouseHoverLeave = reinterpret_cast<void( __fastcall* )( uintptr_t, uintptr_t )>( ClickDetector::MouseHoverLeave );
			inline auto TriggerPrompt = reinterpret_cast<void( __fastcall* )( uintptr_t )>( ProximityPrompt::Trigger );
			inline auto TouchPair = reinterpret_cast<void( __fastcall* )( const std::shared_ptr<uintptr_t>*, const std::shared_ptr<uintptr_t>*, bool )>( BasePart::TouchPair );
			inline auto PushInstance = reinterpret_cast<uintptr_t( __fastcall* )( lua_State*, const std::shared_ptr<uintptr_t>* )>( Instance::PushInstance );
			inline auto BytecodeHash = reinterpret_cast<uint32_t( __fastcall* )( const void*, int, uint32_t )>( Bytecode::Hash );
			inline auto BytecodeDecompress = reinterpret_cast<size_t( __fastcall* )( void*, size_t, const void*, size_t )>( Bytecode::Decompress );
			inline auto Print = ( uintptr_t ( * )( int, const char*, ... ) ) Roblox::Print;
			inline auto Luau_Execute = ( void( __fastcall* )( lua_State* ) ) LuaU::LuauExecute;
		}
	}
}

#define PROTO_LOCVARS_ENC VMValue4
#define PROTO_LINEINFO_ENC VMValue2
#define PROTO_ABSLINEINFO_ENC VMValue1
#define PROTO_SOURCE_ENC VMValue2
#define PROTO_USERDATA_ENC VMValue3
#define PROTO_DEBUGNAME_ENC VMValue2
#define PROTO_UPVALUES_ENC VMValue3
#define PROTO_TYPEINFO_ENC VMValue4
#define PROTO_DEBUGINSN_ENC VMValue2
#define CLOSURE_CONT_ENC VMValue2
#define TSTRING_HASH_ENC VMValue4
#define LSTATE_STACKSIZE_ENC VMValue3
#define UDATA_META_ENC VMValue4
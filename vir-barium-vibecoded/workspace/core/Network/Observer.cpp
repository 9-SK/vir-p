#include "Observer.h"
#include "Memory.h"
#include "core/Roblox/Update.h"
#include "utils/Packets/Logger.h"
#include <MinHook.h>
#include <algorithm>
#include <mutex>

namespace Network
{
	namespace
	{
		using Serialize = bool( __fastcall* )( void*, void* );
		using SendRaw = uint32_t( __fastcall* )( void*, const void*, int, int, int, uint8_t, void*, bool, uint32_t );
		using SendStream = uint32_t( __fastcall* )( void*, void*, int, int, uint8_t, void*, bool, uint32_t, void* );
		using SendShared = uint32_t( __fastcall* )( void*, void*, int, int, uint8_t, void*, bool, uint32_t, void* );
		Serialize g_Serialize = nullptr;
		SendRaw g_SendRaw = nullptr;
		SendStream g_SendStream = nullptr;
		SendShared g_SendShared = nullptr;
		std::mutex g_Control;
		bool g_Installed = false;
		bool g_Created[ 4 ]{ };
		bool g_Enabled[ 4 ]{ };
		std::string g_Error;

		bool Snapshot( Packets::Packet& Packet, const void* Data, uint32_t Size ) noexcept
		{
			if ( !Packet.Generation || !Data || !Size )
				return false;
			Packet.Time = GetTickCount64( );
			Packet.Thread = GetCurrentThreadId( );
			Packet.Length = Size;
			Packet.Captured = std::min<uint32_t>( Size, uint32_t( Packet.Data.size( ) ) );
			if ( !Memory::Copy( Packet.Data.data( ), Data, Packet.Captured ) )
			{
				Packets::g_Logger->Reject( );
				return false;
			}
			return true;
		}

		bool StreamBytes( void* Stream, const void*& Data, uint32_t& Size ) noexcept
		{
			__try
			{
				uintptr_t Table = 0;
				uintptr_t GetData = 0;
				uintptr_t GetSize = 0;
				if ( !Memory::Read( reinterpret_cast<uintptr_t>( Stream ), Table ) ||
					 !Memory::Read( Table + Update::Roblox::Network::AbstractStream::Data, GetData ) ||
					 !Memory::Read( Table + Update::Roblox::Network::AbstractStream::Size, GetSize ) ||
					 !Memory::Executable( GetData ) || !Memory::Executable( GetSize ) )
					return false;
				Data = reinterpret_cast<const void*( __fastcall* ) ( void* )>( GetData )( Stream );
				Size = reinterpret_cast<uint32_t( __fastcall* )( void* )>( GetSize )( Stream );
				return true;
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
		}

		bool Position( void* Stream, uint32_t& Size ) noexcept
		{
			const auto Address = reinterpret_cast<uintptr_t>( Stream );
			uint32_t Capacity = 0;
			uint8_t Unaligned = 0;
			return Stream && Memory::Read( Address + Update::Roblox::Network::Stream::Size, Size ) &&
				   Memory::Read( Address + Update::Roblox::Network::Stream::Capacity, Capacity ) &&
				   Memory::Read( Address + Update::Roblox::Network::Stream::Unaligned, Unaligned ) && !Unaligned && Size <= Capacity;
		}

		bool __fastcall SerializeHook( void* Item, void* Stream )
		{
			const uint64_t Generation = Packets::g_Logger->Generation( );
			uint32_t Begin = 0;
			const bool Observe = Generation && Position( Stream, Begin );
			const bool Result = g_Serialize( Item, Stream );
			if ( Observe && Result )
			{
				uint32_t End = 0;
				uintptr_t Data = 0;
				if ( Position( Stream, End ) && End > Begin && End - Begin <= 64 &&
					 Memory::Read( reinterpret_cast<uintptr_t>( Stream ) + Update::Roblox::Network::Stream::Data, Data ) && Data && Data <= UINTPTR_MAX - Begin )
				{
					Packets::Packet Packet;
					Packet.Source = Packets::Stage::PingItem;
					Packet.Generation = Generation;
					if ( Snapshot( Packet, reinterpret_cast<void*>( Data + Begin ), End - Begin ) )
						Packets::g_Logger->Submit( Packet );
				}
				else
					Packets::g_Logger->Reject( );
			}
			return Result;
		}

		void Details( Packets::Packet& Packet, int Priority, int Reliability, uint8_t Channel, bool Broadcast ) noexcept
		{
			Packet.Generation = Packets::g_Logger->Generation( );
			Packet.Priority = Priority;
			Packet.Reliability = Reliability;
			Packet.Channel = Channel;
			Packet.Broadcast = Broadcast;
		}

		uint32_t __fastcall RawHook( void* Peer, const void* Data, int Size, int Priority, int Reliability, uint8_t Channel, void* Address, bool Broadcast, uint32_t Receipt )
		{
			if ( !Packets::g_Logger->Generation( ) )
				return g_SendRaw( Peer, Data, Size, Priority, Reliability, Channel, Address, Broadcast, Receipt );
			Packets::Packet Packet;
			Details( Packet, Priority, Reliability, Channel, Broadcast );
			const bool Observed = Size > 0 && Snapshot( Packet, Data, uint32_t( Size ) );
			const uint32_t Result = g_SendRaw( Peer, Data, Size, Priority, Reliability, Channel, Address, Broadcast, Receipt );
			if ( Observed )
			{
				Packet.Receipt = Result;
				Packets::g_Logger->Submit( Packet );
			}
			return Result;
		}

		uint32_t __fastcall StreamHook( void* Peer, void* Stream, int Priority, int Reliability, uint8_t Channel, void* Address, bool Broadcast, uint32_t Receipt, void* Context )
		{
			if ( !Packets::g_Logger->Generation( ) )
				return g_SendStream( Peer, Stream, Priority, Reliability, Channel, Address, Broadcast, Receipt, Context );
			Packets::Packet Packet;
			Details( Packet, Priority, Reliability, Channel, Broadcast );
			const void* Data = nullptr;
			uint32_t Size = 0;
			const bool Observed = Packet.Generation && StreamBytes( Stream, Data, Size ) && Snapshot( Packet, Data, Size );
			const uint32_t Result = g_SendStream( Peer, Stream, Priority, Reliability, Channel, Address, Broadcast, Receipt, Context );
			if ( Observed )
			{
				Packet.Receipt = Result;
				Packets::g_Logger->Submit( Packet );
			}
			return Result;
		}

		uint32_t __fastcall SharedHook( void* Peer, void* Shared, int Priority, int Reliability, uint8_t Channel, void* Address, bool Broadcast, uint32_t Receipt, void* Context )
		{
			if ( !Packets::g_Logger->Generation( ) )
				return g_SendShared( Peer, Shared, Priority, Reliability, Channel, Address, Broadcast, Receipt, Context );
			Packets::Packet Packet;
			Details( Packet, Priority, Reliability, Channel, Broadcast );
			void* Stream = nullptr;
			const void* Data = nullptr;
			uint32_t Size = 0;
			const bool Observed = Packet.Generation && Memory::Read( reinterpret_cast<uintptr_t>( Shared ), Stream ) &&
								  StreamBytes( Stream, Data, Size ) && Snapshot( Packet, Data, Size );
			const uint32_t Result = g_SendShared( Peer, Shared, Priority, Reliability, Channel, Address, Broadcast, Receipt, Context );
			if ( Observed )
			{
				Packet.Receipt = Result;
				Packets::g_Logger->Submit( Packet );
			}
			return Result;
		}

		bool Install( )
		{
			if ( g_Installed )
				return true;
			if ( !Memory::Profile( ) )
			{
				g_Error = "network profile PE timestamp does not match the loaded client";
				return false;
			}
			using namespace Update::Roblox::Network;
			struct Hook
			{
				uintptr_t Address;
				std::span<const uint8_t> Entry;
				void* Callback;
				void** Original;
				const char* Name;
			};
			const Hook Hooks[] =
				{
					{ SerializePing, SerializeEntry, reinterpret_cast<void*>( SerializeHook ), reinterpret_cast<void**>( &g_Serialize ), "PingItem serializer" },
					{ Update::Roblox::Network::SendRaw, RawEntry, reinterpret_cast<void*>( RawHook ), reinterpret_cast<void**>( &g_SendRaw ), "RakPeer raw Send" },
					{ Update::Roblox::Network::SendStream, StreamEntry, reinterpret_cast<void*>( StreamHook ), reinterpret_cast<void**>( &g_SendStream ), "RakPeer stream Send" },
					{ Update::Roblox::Network::SendShared, SharedEntry, reinterpret_cast<void*>( SharedHook ), reinterpret_cast<void**>( &g_SendShared ), "RakPeer shared Send" } };
			for ( size_t Index = 0; Index < std::size( Hooks ); ++Index )
			{
				const auto& Hook = Hooks[ Index ];
				if ( !g_Enabled[ Index ] && !Memory::Match( Hook.Address, Hook.Entry ) )
				{
					g_Error = std::string( Hook.Name ) + " entry does not match the reviewed client";
					return false;
				}
			}
			MH_STATUS Result = MH_Initialize( );
			if ( Result != MH_OK && Result != MH_ERROR_ALREADY_INITIALIZED )
			{
				g_Error = MH_StatusToString( Result );
				return false;
			}
			for ( size_t Index = 0; Index < std::size( Hooks ); ++Index )
			{
				const auto& Hook = Hooks[ Index ];
				if ( !g_Created[ Index ] )
				{
					Result = MH_CreateHook( reinterpret_cast<void*>( Hook.Address ), Hook.Callback, Hook.Original );
					if ( Result != MH_OK )
						break;
					g_Created[ Index ] = true;
				}
				if ( !g_Enabled[ Index ] )
				{
					Result = MH_EnableHook( reinterpret_cast<void*>( Hook.Address ) );
					if ( Result != MH_OK )
						break;
					g_Enabled[ Index ] = true;
				}
			}
			if ( Result != MH_OK )
			{
				g_Error = MH_StatusToString( Result );
				return false;
			}
			g_Installed = true;
			g_Error.clear( );
			return true;
		}
	}

	bool Observer::Start( )
	{
		std::lock_guard Lock( g_Control );
		return Install( ) && Packets::g_Logger->Start( );
	}

	void Observer::Stop( )
	{
		std::lock_guard Lock( g_Control );
		Packets::g_Logger->Stop( );
	}

	bool Observer::Installed( )
	{
		std::lock_guard Lock( g_Control );
		return g_Installed;
	}

	std::string Observer::Error( )
	{
		std::lock_guard Lock( g_Control );
		return g_Error;
	}
}

#pragma once

#include "Packet.h"
#include <Windows.h>
#include <atomic>
#include <memory>
#include <string>

namespace Packets
{
	struct Status
	{
		bool Enabled = false;
		uint64_t Generation = 0;
		uint64_t Captured = 0;
		uint64_t Read = 0;
		uint64_t Dropped = 0;
		uint64_t Rejected = 0;
		size_t Pending = 0;
	};

	class Logger
	{
	  public:
		bool Start( );
		void Stop( );
		uint64_t Generation( ) const noexcept;
		void Submit( const Packet& Value ) noexcept;
		void Reject( ) noexcept;
		bool Read( Packet& Value, uint64_t Generation ) noexcept;
		static std::string Format( const Packet& Value );
		Status Inspect( );

	  private:
		SRWLOCK m_QueueLock = SRWLOCK_INIT;
		std::array<Packet, 256> m_Queue{ };
		size_t m_Head = 0;
		size_t m_Count = 0;
		std::atomic<bool> m_Enabled{ false };
		std::atomic<uint64_t> m_Generation{ 0 };
		std::atomic<uint64_t> m_Captured{ 0 };
		uint64_t m_Read = 0;
		std::atomic<uint64_t> m_Dropped{ 0 };
		std::atomic<uint64_t> m_Rejected{ 0 };
	};

	extern const std::shared_ptr<Logger> g_Logger;
}

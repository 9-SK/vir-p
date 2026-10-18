#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Packets
{
	inline constexpr size_t MaximumBytes = 4096;

	enum class Stage : uint8_t
	{
		Send,
		PingItem
	};

	struct Packet
	{
		Stage Source = Stage::Send;
		uint64_t Generation = 0;
		uint64_t Time = 0;
		uint32_t Thread = 0;
		uint32_t Length = 0;
		uint32_t Captured = 0;
		uint32_t Receipt = 0;
		int Priority = 0;
		int Reliability = 0;
		uint8_t Channel = 0;
		bool Broadcast = false;
		std::array<uint8_t, MaximumBytes> Data{ };
	};
}

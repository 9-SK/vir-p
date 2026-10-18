#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Packets::Protocol
{
	struct PingItem
	{
		uint8_t Item = 0;
		uint8_t Type = 0;
		uint64_t Time = 0;
		uint32_t FirstSerializeTime = 0;
		std::array<float, 3> Fps{ };
		uint32_t SendStats = 0;
		uint32_t ExtraStats = 0;
		uint8_t Mode = 0;
		bool HasFps = false;
		bool HasMarkers = false;
		uint32_t Markers = 0;
		bool HasMemory = false;
		float Memory = 0;
		bool HasTrace = false;
		uint8_t Trace = 0;
		size_t Bytes = 0;
	};

	class PingDecoder
	{
	  public:
		static bool Decode( std::span<const uint8_t> Bytes, PingItem& Result ) noexcept;
		static std::string Format( const PingItem& Value );
	};
}

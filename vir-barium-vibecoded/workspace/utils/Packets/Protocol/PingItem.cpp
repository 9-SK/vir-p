#include "PingItem.h"
#include <bit>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace Packets::Protocol
{
	namespace
	{
		class Reader
		{
		  public:
			explicit Reader( std::span<const uint8_t> Bytes ) : m_Bytes( Bytes )
			{
			}

			template <typename T>
			bool Read( T& Value ) noexcept
			{
				if ( m_Position > m_Bytes.size( ) || sizeof( T ) > m_Bytes.size( ) - m_Position )
					return false;
				uint64_t Bits = 0;
				for ( size_t Index = 0; Index < sizeof( T ); ++Index )
					Bits |= uint64_t( m_Bytes[ m_Position++ ] ) << ( Index * 8 );
				if constexpr ( std::is_same_v<T, float> )
					Value = std::bit_cast<float>( static_cast<uint32_t>( Bits ) );
				else
					Value = static_cast<T>( Bits );
				return true;
			}

			size_t Position( ) const noexcept
			{
				return m_Position;
			}

		  private:
			std::span<const uint8_t> m_Bytes;
			size_t m_Position = 0;
		};
	}

	bool PingDecoder::Decode( std::span<const uint8_t> Bytes, PingItem& Result ) noexcept
	{
		Reader Input( Bytes );
		PingItem Value;
		if ( !Input.Read( Value.Item ) || ( Value.Item != 5 && Value.Item != 0x86 ) ||
			 !Input.Read( Value.Type ) || ( Value.Type & ~0x1e ) != 0 ||
			 !Input.Read( Value.Time ) || !Input.Read( Value.FirstSerializeTime ) )
			return false;
		Value.HasFps = ( Value.Type & 2 ) != 0;
		if ( Value.HasFps )
		{
			for ( float& Fps : Value.Fps )
			{
				if ( !Input.Read( Fps ) )
					return false;
			}
		}
		if ( !Input.Read( Value.SendStats ) || !Input.Read( Value.ExtraStats ) || !Input.Read( Value.Mode ) || Value.Mode >= 2 )
			return false;
		Value.HasMarkers = ( Value.Type & 4 ) != 0;
		if ( Value.HasMarkers )
		{
			uint32_t Encoded = 0;
			if ( !Input.Read( Encoded ) )
				return false;
			Value.Markers = Encoded ^ static_cast<uint32_t>( Value.Time );
		}
		Value.HasMemory = ( Value.Type & 0x10 ) != 0;
		if ( Value.HasMemory && !Input.Read( Value.Memory ) )
			return false;
		Value.HasTrace = ( Value.Type & 8 ) != 0;
		if ( Value.HasTrace && ( !Input.Read( Value.Trace ) || Value.Trace > 1 ) )
			return false;
		Value.Bytes = Input.Position( );
		Result = Value;
		return true;
	}

	std::string PingDecoder::Format( const PingItem& Value )
	{
		std::ostringstream Output;
		Output << "PingItem item=" << unsigned( Value.Item ) << " type=" << unsigned( Value.Type )
			   << " time=" << Value.Time << " first_serialize_ms=" << Value.FirstSerializeTime;
		if ( Value.HasFps )
			Output << " render_fps=" << Value.Fps[ 0 ] << " physics_fps=" << Value.Fps[ 1 ] << " heartbeat_fps=" << Value.Fps[ 2 ];
		Output << " send_stats=0x" << std::hex << Value.SendStats << " extra_stats=0x" << Value.ExtraStats << std::dec << " mode=" << unsigned( Value.Mode );
		if ( Value.HasMarkers )
			Output << " vm_markers=0x" << std::hex << Value.Markers << std::dec;
		if ( Value.HasMemory )
			Output << " memory_mb=" << Value.Memory;
		if ( Value.HasTrace )
			Output << " trace_present=" << unsigned( Value.Trace );
		Output << " server_status=unknown";
		return Output.str( );
	}
}

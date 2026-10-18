#include "Logger.h"
#include "Protocol/PingItem.h"
#include <iomanip>
#include <sstream>

namespace Packets
{
	const std::shared_ptr<Logger> g_Logger = std::make_shared<Logger>( );

	std::string Logger::Format( const Packet& Value )
	{
		std::ostringstream Output;
		Output << "time_ms=" << Value.Time << " thread=" << Value.Thread << " generation=" << Value.Generation;
		std::span<const uint8_t> Data( Value.Data.data( ), Value.Captured );
		Protocol::PingItem Ping;
		if ( Value.Source == Stage::PingItem )
		{
			Output << " stage=serialized ";
			if ( Protocol::PingDecoder::Decode( Data, Ping ) && Ping.Bytes == Data.size( ) )
				Output << Protocol::PingDecoder::Format( Ping );
			else
				Output << "PingItem unsupported_layout";
		}
		else
		{
			Output << " stage=send packet_id=" << ( Data.empty( ) ? -1 : int( Data[ 0 ] ) )
				   << " receipt=" << Value.Receipt << " priority=" << Value.Priority << " reliability=" << Value.Reliability
				   << " channel=" << unsigned( Value.Channel ) << " broadcast=" << Value.Broadcast;
			if ( Data.size( ) > 1 && Data[ 0 ] == 131 && Protocol::PingDecoder::Decode( Data.subspan( 1 ), Ping ) )
				Output << " first_item={" << Protocol::PingDecoder::Format( Ping ) << "}";
		}
		Output << " bytes=" << Value.Length << " captured=" << Value.Captured << " hex=" << std::hex << std::setfill( '0' );
		for ( const uint8_t Byte : Data )
			Output << std::setw( 2 ) << unsigned( Byte );
		return Output.str( );
	}

	bool Logger::Start( )
	{
		AcquireSRWLockExclusive( &m_QueueLock );
		if ( !m_Enabled.load( ) )
		{
			m_Generation.fetch_add( 1 );
			m_Head = m_Count = 0;
			m_Captured = 0;
			m_Read = 0;
			m_Dropped = 0;
			m_Rejected = 0;
			m_Enabled.store( true, std::memory_order_release );
		}
		ReleaseSRWLockExclusive( &m_QueueLock );
		return true;
	}
	void Logger::Stop( )
	{
		AcquireSRWLockExclusive( &m_QueueLock );
		m_Enabled.store( false, std::memory_order_release );
		m_Generation.fetch_add( 1 );
		m_Head = m_Count = 0;
		ReleaseSRWLockExclusive( &m_QueueLock );
	}

	uint64_t Logger::Generation( ) const noexcept
	{
		return m_Enabled.load( std::memory_order_acquire ) ? m_Generation.load( std::memory_order_acquire ) : 0;
	}

	void Logger::Submit( const Packet& Value ) noexcept
	{
		if ( !Value.Generation || Value.Captured > Value.Data.size( ) || Value.Captured > Value.Length )
			return;
		if ( !TryAcquireSRWLockExclusive( &m_QueueLock ) )
		{
			++m_Dropped;
			return;
		}
		if ( m_Enabled.load( ) && Value.Generation == m_Generation.load( ) )
		{
			if ( m_Count == m_Queue.size( ) )
				++m_Dropped;
			else
			{
				m_Queue[ ( m_Head + m_Count ) % m_Queue.size( ) ] = Value;
				++m_Count;
				++m_Captured;
			}
		}
		ReleaseSRWLockExclusive( &m_QueueLock );
	}

	void Logger::Reject( ) noexcept
	{
		++m_Rejected;
	}

	bool Logger::Read( Packet& Value, uint64_t Generation ) noexcept
	{
		AcquireSRWLockExclusive( &m_QueueLock );
		const bool Found = m_Count != 0 && m_Enabled.load( ) && m_Generation.load( ) == Generation;
		if ( Found )
		{
			Value = m_Queue[ m_Head ];
			m_Head = ( m_Head + 1 ) % m_Queue.size( );
			--m_Count;
			++m_Read;
		}
		ReleaseSRWLockExclusive( &m_QueueLock );
		return Found;
	}

	Status Logger::Inspect( )
	{
		AcquireSRWLockShared( &m_QueueLock );
		const Status Value{ m_Enabled.load( ), m_Generation.load( ), m_Captured.load( ), m_Read, m_Dropped.load( ), m_Rejected.load( ), m_Count };
		ReleaseSRWLockShared( &m_QueueLock );
		return Value;
	}
}

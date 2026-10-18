#pragma once

#include <string>

namespace Network
{
	class Transport
	{
	  public:
		static bool UseRaknet( std::string& Error );
		static int ReadFlag( ) noexcept;
	};
}

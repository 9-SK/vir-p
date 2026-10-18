#pragma once

#include <string>

namespace Network
{
	class Observer
	{
	  public:
		static bool Start( );
		static void Stop( );
		static bool Installed( );
		static std::string Error( );
	};
}

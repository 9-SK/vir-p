#pragma once
#include <cpr/cpr.h>

namespace Http
{
	cpr::Response Send( const std::string& Url, const std::string& Method, const std::string& Body, const cpr::Header& Headers, const cpr::Cookies& Cookies );
	void Initialize( );
}

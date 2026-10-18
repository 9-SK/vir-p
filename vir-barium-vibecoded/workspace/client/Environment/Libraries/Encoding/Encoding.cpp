#include "Encoding.h"
#include "Helpers/Helper.h"
#include "client/Environment/Env.h"

namespace Encoding
{
	void Initialize( )
	{
		Env::RegisterGlobal( "base64encode", Helpers::EncodeBase64 );
		Env::RegisterGlobal( "base64decode", Helpers::DecodeBase64 );
		Env::RegisterGlobal( "lz4compress", Helpers::CompressLZ4 );
		Env::RegisterGlobal( "lz4decompress", Helpers::DecompressLZ4 );
	}
}

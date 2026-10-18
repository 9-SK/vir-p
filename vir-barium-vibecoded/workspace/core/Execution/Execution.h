#pragma once
#include "core/Roblox/Update.h"
#include <Luau/Bytecode.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/Compiler.h>
#include <VM/src/lapi.h>
#include <VM/src/lobject.h>
#include <lua.h>
#include <lualib.h>
#include <string>

class BytecodeEncoder : public Luau::BytecodeEncoder
{
	inline void
	encode( uint32_t* data, size_t count ) override
	{
		for ( auto i = 0; i < count; )
		{
			uint8_t Opcode = LUAU_INSN_OP( data[ i ] );
			const auto LookupTable = reinterpret_cast<BYTE*>( Update::LuaU::OpCodeLookupTable );
			uint8_t FinalOpcode = Opcode * 227;
			FinalOpcode = LookupTable[ FinalOpcode ];

			data[ i ] = ( FinalOpcode ) | ( data[ i ] & ~0xFF );
			i += Luau::getOpLength( static_cast<LuauOpcode>( Opcode ) );
		}
	}
};

namespace Alpen
{
	namespace Execution
	{
		[[nodiscard]] std::string Compile( std::string Source );
		void Execute( lua_State* L, std::string Script );
	}
}

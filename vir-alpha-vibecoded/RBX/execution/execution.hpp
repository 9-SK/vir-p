#pragma once

#include <ldo.h>
#include <string>
#include <lualib.h>
#include <lstate.h>
#include <Luau/Compiler.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/BytecodeBuilder.h>

void ElevateThread(lua_State* L, uint64_t identity, bool isInstant);

namespace Execution
{
	std::string CompileScript(std::string Source);
	void ExecuteScript(lua_State* L, std::string Script);
}



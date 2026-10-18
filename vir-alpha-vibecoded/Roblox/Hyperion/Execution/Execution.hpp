#pragma once

#include <ldo.h>
#include <string>
#include <lualib.h>
#include <lstate.h>
#include <Luau/Compiler.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/BytecodeBuilder.h>

namespace Execution
{
	std::string CompileScript(std::string Source);
	// Returns true only when the script compiled, loaded and was handed to
	// task.defer without an error; false on any stage failure.
	bool ExecuteScript(lua_State* L, std::string Script);
}
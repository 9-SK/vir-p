#include <Utilitys/Globals.hpp>
#include <RBX/environment/yielding/yielding.hpp>

void Yielding::RunYield()
{
	for (int i = 0; i < 64; ++i)
	{
		std::function<void()> YieldingRequest;
		{
			std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
			if (Shared::YieldQueue.empty())
				return;
			YieldingRequest = std::move(Shared::YieldQueue.front());
			Shared::YieldQueue.pop();
		}
		YieldingRequest();
	}
}

int Yielding::YieldExecution(lua_State* L, const std::function<Yielded()>& YieldingClosure)
{
	const uint64_t Generation = Shared::StateGeneration.load();
	lua_pushthread(L);
	int YieldedThreadRef = lua_ref(L, -1);
	lua_pop(L, 1);

	std::thread([=]
	{
		// Last-resort containment: YieldingClosure does network/file I/O on
		// this detached thread. Anything escaping it (C++ or, under /EHa,
		// SEH) would hit std::terminate and kill the client with zero log.
		// Surface it as a Lua-level nil+message instead; extra values are
		// harmless to callers expecting fewer.
		Yielded ResumeFunction;
		try
		{
			ResumeFunction = YieldingClosure();
		}
		catch (...)
		{
			ResumeFunction = [](lua_State* L) -> int
				{
					lua_pushnil(L);
					lua_pushstring(L, "request failed: background exception");
					return 2;
				};
		}
		if (Generation != Shared::StateGeneration.load())
			return;

		{
			std::lock_guard<std::mutex> lock(Shared::YieldQueueMutex);
			Shared::YieldQueue.emplace([=]() -> void
			{
				if (Generation != Shared::StateGeneration.load())
					return;
				YieldingLuaThread YieldingLuaThreadObject = { .L = L };
				YieldingLuaThread* YieldingLuaThread = &YieldingLuaThreadObject;

				YieldState YieldState = { 0 };
				auto ScriptContext = reinterpret_cast<uintptr_t>(L->userdata->shared->scriptContext);
				Roblox::ScriptContextResume(ScriptContext + Offsets::ExtraSpace::ScriptContextToResume, &YieldState, &YieldingLuaThread, ResumeFunction(L), NULL, NULL);

				lua_unref(L, YieldedThreadRef);
			});
		}
	}).detach();

	return lua_yield(L, NULL);
}

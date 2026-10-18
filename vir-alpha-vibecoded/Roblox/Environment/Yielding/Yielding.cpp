#include <Roblox/Environment/Yielding/Yielding.hpp>

static void SehInvoke(const std::function<void()>& fn)
{
	__try
	{
		fn();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

void Yielding::RunYield()
{
	

	std::function<void()> YieldingRequest;
	{
		std::lock_guard<std::mutex> lock(Shared::YieldMutex);
		if (Shared::YieldQueue.empty())
			return;

		YieldingRequest = Shared::YieldQueue.front();
		Shared::YieldQueue.pop();
	}

	SehInvoke(YieldingRequest);
}

int Yielding::YieldExecution(lua_State* L, const std::function<Yielded()>& YieldingClosure)
{


	lua_pushthread(L);
	int YieldedThreadRef = lua_ref(L, -1);
	lua_pop(L, 1);

	std::thread([=]
		{
			Yielded ResumeFunction = YieldingClosure();

			std::lock_guard<std::mutex> lock(Shared::YieldMutex);
			Shared::YieldQueue.emplace([=]() -> void
				{
					YieldingLuaThread YieldingLuaThreadObject = { .L = L };
					YieldingLuaThread* YieldingThreadPointer = &YieldingLuaThreadObject;

					YieldState ResumeState = { 0 };

					uintptr_t ScriptContextAddress = (uintptr_t)(L->userdata->shared->scriptContext);

					Hyperion::ScriptContextResume(
						ScriptContextAddress + Offsets::ExtraSpace::ScriptContextToResume,
						&ResumeState,
						&YieldingThreadPointer,
						ResumeFunction(L),
						NULL,
						NULL);

					lua_unref(L, YieldedThreadRef);
				});
		}).detach();

	return lua_yield(L, NULL);
}

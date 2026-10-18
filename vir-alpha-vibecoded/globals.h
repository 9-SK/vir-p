#pragma once

#include <Utilitys/Globals.hpp>
#include <RBX/update.hpp>

// Bridge header: maps the new graphics code's `globals.*` to the existing project's `Shared::*`
struct globals_bridge_t
{
    lua_State*& exploit_thread = Shared::ExploitThread;
    std::vector<Shared::ExecutionRequest>& execution_queue = Shared::ExecutionRequests;
};

inline globals_bridge_t globals;

namespace module::update::task_scheduler
{
    inline uintptr_t pointer = Offsets::Scheduler::TaskScheduler;
}

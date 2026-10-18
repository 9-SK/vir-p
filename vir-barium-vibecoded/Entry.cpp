/* With Love By Dyn <3> */
// viridian shell (vir-p-alpha). The engine-facing core is the 0.743-ported
// reference runtime; everything the control surface needs lives here: boot
// telemetry plus the file channel it drives the module through.
#include "utils/BootLog.hpp"
#include "utils/FileWatcher.h"
#include "handler/TPHandler.h"
#include "core/Scheduler/Scheduler.h"
#include "client/Environment/Libraries/Raknet/Raknet.h"
#include <Windows.h>
#include <thread>

namespace
{
    void Boot()
    {
        BootLog::WriteBootPid();
        BootLog::WriteLine("boot", "vir-p-alpha online");

        std::thread(FileWatcher::Initialize).detach();
        BootLog::WriteLine("boot", "execute.lua watcher live");

        BootLog::WriteLine("boot", "idle");
    }
}

BOOL APIENTRY
DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        std::thread(Boot).detach();

        std::thread([] {
            Raknet::Bootstrap();
            TPHandler::Initialize();
        }).detach();
    }

    return TRUE;
}
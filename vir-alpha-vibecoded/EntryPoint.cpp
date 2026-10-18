#include <Roblox/Hyperion/Scheduler/Scheduler.hpp>
#include <Roblox/Hyperion/Scheduler/Communication/Communication.hpp>
#include <Roblox/Hyperion/Communication/BootLog.hpp>
#include <Roblox/Hyperion/Communication/StatusRing.hpp>
#include <Roblox/Hyperion/Communication/ExecWatcher.hpp>
#include <thread>
#include <cstdio>
#include <cstring>
#include <Roblox/Renderer/RenderHook.hpp>

namespace
{
    // Manual mapping leaves this module out of the loader's module list, so
    // the unwinder cannot find our .pdata. Any exception that unwinds through
    // our frames - including the engine's own C++ exceptions in frames below
    // our hooked step - breaks (STATUS_UNWIND c0000027) and takes the client
    // with it. Register the table the way the real loader would.
    uintptr_t s_UnwindBase = 0;
    DWORD s_UnwindCount = 0;
    LONG s_UnwindOk = 0;

    using RtlAddFunctionTable_t = BOOLEAN(__stdcall*)(PRUNTIME_FUNCTION, DWORD, DWORD64);

    static void RegisterUnwindTables(uintptr_t Base)
    {
        // Only trust the passed base if it really looks like our image.
        bool LooksLikeImage = false;
        __try
        {
            const auto Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(Base);
            LooksLikeImage = Base != 0 && Dos->e_magic == IMAGE_DOS_SIGNATURE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LooksLikeImage = false;
        }

        if (!LooksLikeImage)
        {
            // Walk down from our own code in 64KB steps until the image
            // header shows up; validate MZ + NT signature per step.
            const uintptr_t Seed = reinterpret_cast<uintptr_t>(&RegisterUnwindTables);
            for (uintptr_t Probe = Seed & ~0xFFFFull; Probe >= 0x10000; Probe -= 0x10000)
            {
                __try
                {
                    const auto Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(Probe);
                    if (Dos->e_magic != IMAGE_DOS_SIGNATURE)
                        continue;
                    const auto Nt = reinterpret_cast<PIMAGE_NT_HEADERS>(Probe + Dos->e_lfanew);
                    if (Nt->Signature != IMAGE_NT_SIGNATURE)
                        continue;
                    if (Nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                        continue;
                    Base = Probe;
                    LooksLikeImage = true;
                    break;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    continue;
                }
            }
        }

        if (!LooksLikeImage)
            return;

        s_UnwindBase = Base;
        Shared::ModuleBase = Base;

        const auto Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(Base);
        const auto Nt = reinterpret_cast<PIMAGE_NT_HEADERS>(Base + Dos->e_lfanew);
        auto Sec = IMAGE_FIRST_SECTION(Nt);
        for (WORD i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Sec)
        {
            if (memcmp(Sec->Name, ".pdata", 6) != 0 || Sec->Misc.VirtualSize == 0)
                continue;

            s_UnwindCount = Sec->Misc.VirtualSize / sizeof(RUNTIME_FUNCTION);

            const auto Add = reinterpret_cast<RtlAddFunctionTable_t>(reinterpret_cast<void*>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "RtlAddFunctionTable")));
            InterlockedExchange(&s_UnwindOk,
                Add && Add(reinterpret_cast<PRUNTIME_FUNCTION>(Base + Sec->VirtualAddress), s_UnwindCount, Base) ? 1 : -1);
            break;
        }
    }
}

void Thread(HMODULE hModule)
{
    StatusRing::Init();
    StatusRing::MarkInjecting();
    BootLog::WriteBootPid();

    {
        char Msg[128]{};
        snprintf(Msg, sizeof(Msg), "unwind tables base=%llx count=%lu ok=%ld",
            static_cast<unsigned long long>(s_UnwindBase),
            static_cast<unsigned long>(s_UnwindCount),
            static_cast<long>(s_UnwindOk));
        BootLog::WriteLine("boot", Msg);
    }

    BootLog::WritePhase("ring ready");

    ExecWatcher::Start();
    BootLog::WritePhase("watcher started");

    std::thread(Communication::Initialize).detach();
    BootLog::WritePhase("tcp server starting");

    std::thread(Scheduler::TeleportHandler).detach();
    BootLog::WritePhase("teleport handler started");

    BootLog::WritePhase("idle");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // Before any of our threads run, so every frame we ever put on the
        // stack is unwindable from the first exception onward.
        RegisterUnwindTables(reinterpret_cast<uintptr_t>(hModule));

        std::thread(Thread, hModule).detach();
    }

    return TRUE;
}

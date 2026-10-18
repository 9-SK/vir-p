#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <Utilitys/RingStatus.hpp>
#include <Utilitys/Communication/Communication.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <Utilitys/TPHandler.hpp>
#include <RBX/environment/libs/rconsole/rconsole.hpp>
#include <Renderer/graphics.h>
#include <RBX/environment/libs/raknet/raknet.hpp>
#include <CrashHandler.hpp>

#include <filesystem>
#include <cstdio>
#include <string>
#include <thread>
#include <windows.h>

static void BootLogLine(const char* tag, const char* msg)
{
    char bb[MAX_PATH]{};
    DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
    if (!bn || bn >= MAX_PATH)
        return;
    std::string bp = std::string(bb) + "\\Viridian\\boot.log";
    FILE* bf = nullptr;
    if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf)
        return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    fprintf(bf, "%02u:%02u:%02u [%s] %s\n", st.wHour, st.wMinute, st.wSecond, tag, msg);
    fclose(bf);
    ViridianRing::RefreshLogTail();
}

// Runs on its own detached thread: MainThread never blocks on the renderer.
// No C++ unwindables here (C2712): the __try body only calls into graphics.
static void graphics_init_thread()
{
    __try
    {
        module::core::graphics::graphics->initialize();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        BootLogLine("gfx", "SEH in graphics init, continuing without renderer hook");
    }
}

static void execute_file_watcher()
{
    char base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;

    std::filesystem::path dir = std::filesystem::path(base) / "Viridian";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / "execute.lua";

    // Change-only baseline: snapshot any pre-existing execute.lua (size+mtime)
    // so a stale file from a previous session can never execute. Only new
    // writes after boot reach RequestExecution.
    FILETIME lastWrite{};
    uint64_t lastSize = 0;
    bool haveLast = false;
    {
        WIN32_FILE_ATTRIBUTE_DATA baseFad{};
        if (GetFileAttributesExA(path.string().c_str(), GetFileExInfoStandard, &baseFad))
        {
            lastWrite = baseFad.ftLastWriteTime;
            lastSize = (static_cast<uint64_t>(baseFad.nFileSizeHigh) << 32) | baseFad.nFileSizeLow;
            haveLast = true;
        }
    }

    while (true)
    {
        Sleep(250);

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExA(path.string().c_str(), GetFileExInfoStandard, &fad))
            continue;

        if (haveLast &&
            fad.ftLastWriteTime.dwLowDateTime == lastWrite.dwLowDateTime &&
            fad.ftLastWriteTime.dwHighDateTime == lastWrite.dwHighDateTime &&
            (((static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow) == lastSize))
            continue;

        lastWrite = fad.ftLastWriteTime;
        lastSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        haveLast = true;
        Sleep(250); // write-settle delay

        HANDLE h = CreateFileA(path.string().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        DWORD size = GetFileSize(h, nullptr);
        if (size == 0 || size == INVALID_FILE_SIZE || size > 8 * 1024 * 1024)
        {
            CloseHandle(h);
            continue;
        }

        std::string src;
        src.resize(size);
        DWORD rd = 0;
        BOOL ok = ReadFile(h, src.data(), size, &rd, nullptr);
        CloseHandle(h);
        if (!ok || rd == 0)
            continue;
        src.resize(rd);

        if (src.size() >= 3 &&
            (unsigned char)src[0] == 0xEF &&
            (unsigned char)src[1] == 0xBB &&
            (unsigned char)src[2] == 0xBF)
            src.erase(0, 3);

        {
            char bb[MAX_PATH]{};
            DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
            if (bn && bn < MAX_PATH)
            {
                std::string bp = std::string(bb) + "\\Viridian\\boot.log";
                FILE* bf = nullptr;
                if (fopen_s(&bf, bp.c_str(), "a") == 0 && bf)
                {
                    SYSTEMTIME st{}; GetLocalTime(&st);
                    fprintf(bf, "%02u:%02u:%02u [watcher] queued execute.lua size=%zu\n", st.wHour, st.wMinute, st.wSecond, src.size());
                    fclose(bf);
                    ViridianRing::RefreshLogTail();
                }
            }
        }
        task_scheduler::RequestExecution(std::move(src));
    }
}

void raknetwatcher()
{
    uintptr_t RakNet = 0;
    // Bounded tight wait (1200 x 50ms = 60s): a silent infinite spin here
    // leaves zero trace in boot.log when the job never appears (menu/loading).
    for (int i = 0; i < 1200 && !RakNet; ++i)
    {
        RakNet = task_scheduler::GetJobByTypeName("Net Peer Send");
        if (!RakNet)
            Sleep(50);
    }

    if (!RakNet)
    {
        BootLogLine("net", "raknetwatcher timeout (60s) waiting for Net Peer Send, retrying in background");
        // Slow background retry: one cheap scan per 5s, still off the init
        // path, so a late-appearing job (menu -> in-game) is still hooked.
        while (!RakNet)
        {
            Sleep(5000);
            RakNet = task_scheduler::GetJobByTypeName("Net Peer Send");
        }
        BootLogLine("net", "raknetwatcher found Net Peer Send (late)");
    }

    Raknet::InitializeHook(RakNet);

    while (true)
    {
        Sleep(500);
        if (!task_scheduler::GetJobByTypeName("Net Peer Send"))
        {
            std::thread(&raknetwatcher).detach();
            break;
        }
    }
}

void MainThread(HMODULE)
{
    {
        // Process-lifetime single-instance guard: the handle is intentionally
        // held open until process death (closing it would defeat the check
        // for a second load in this process).
        HANDLE h = CreateMutexW(nullptr, FALSE, L"{8F3B2A41-6C7D-4E9F-A2B5-1D4C7E8F9A0B}");
        if (h && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(h);
            return;
        }
    }

    // FIRST init act, unconditional: publish the ring (status=injecting) so
    // the UI sees liveness within milliseconds of DllMain. This must NOT sit
    // behind the boot.log fopen gate: a failed log open must never hide the
    // ring. Every wait below this point runs on a detached thread; MainThread
    // itself never blocks on game objects, locks, or I/O.
    ViridianRing::Init();
    ViridianRing::MarkInjecting();

    {
        char bb[MAX_PATH]{};
        DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
        if (bn && bn < MAX_PATH)
        {
            std::string bp = std::string(bb) + "\\Viridian\\boot.log";
            FILE* bf = nullptr;
            if (fopen_s(&bf, bp.c_str(), "a") == 0 && bf)
            {
                SYSTEMTIME st{}; GetLocalTime(&st);
                fprintf(bf, "%04u-%02u-%02u %02u:%02u:%02u BOOT pid=%lu\n",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
                fclose(bf);
                ViridianRing::RefreshLogTail();
            }
        }
    }

    char local[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH) && local[0] != '\0')
        Shared::workspace_folder = std::filesystem::path(local) / "Viridian" / "workspace";
    else
        Shared::workspace_folder = std::filesystem::path("workspace");

    {
        std::error_code ec;
        std::filesystem::create_directories(Shared::workspace_folder, ec);
    }

    Communication::Initialize();
    BootLogLine("boot", "phase=net spawned");
    TpHandler::Initialize();
    BootLogLine("boot", "phase=tp spawned");
    std::thread(execute_file_watcher).detach();
    std::thread(raknetwatcher).detach();
    BootLogLine("boot", "phase=watchers spawned");
    // Renderer hook on its own detached thread: the scheduler/RenderJob wait
    // lives in graphics->initialize() and must never stall MainThread (menu /
    // loading / suspended scheduler threads). The stub-joining thread returns
    // promptly after spawning; nothing here is joined or waited on.
    std::thread(graphics_init_thread).detach();
    BootLogLine("boot", "phase=gfx spawned");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // â”€â”€ Anti-debug: immediate check + watchdog thread â”€â”€
   

        DisableThreadLibraryCalls(hModule);

        CrashHandler::Initialize();

        std::thread(MainThread, hModule).detach();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // ── Death path: do (almost) nothing, deliberately. ──
        // A previous incident left a ZOMBIE RobloxPlayerBeta.exe (no UI) plus
        // orphaned conhost.exe holding the single-instance lock, so every
        // later launch died in ~1s until the zombie was killed manually. Any
        // blocking work here (thread joins, waits, locks, FreeConsole, file
        // I/O, UnmapViewOfFile/CloseHandle on the ring) risks hanging
        // teardown and recreating that zombie. So:
        //  - no joins/waits: every DLL thread (MainThread,
        //    execute_file_watcher, raknetwatcher, TpHandler, TcpServer, yield
        //    continuations, websocket recv) is detached fire-and-forget and
        //    dies with the process.
        //  - no locks, no file I/O, no ring teardown: ViridianRing's mapping
        //    (Detail::s_map/s_view in Utilitys/RingStatus.hpp) is deliberately
        //    NEVER unmapped or closed; the OS reclaims it on process death.
        //  - no FreeConsole (can block on a dead conhost): console handles
        //    are merely forgotten, lock-free, via RConsole::OnProcessDetach().
        // lpReserved != nullptr means static (process) detach; either way the
        // same minimal path runs.
        (void)lpReserved;
        RConsole::OnProcessDetach();
    }

    return TRUE;
}

#pragma once
// File-drop channel: %LOCALAPPDATA%\Viridian\execute.lua.
// The desktop application writes it atomically (temp file + rename) via the
// legacy `execute_script` path; this watcher is the no-socket transport and
// stays as the fallback alongside the loopback one. Polled VERY fast: one
// GetFileAttributesEx every kPollMs, so a drop lands in the game within a
// frame or two of the rename.
//
// Rules:
//  - Baseline (size+mtime) at start: a stale file from a previous session
//    never auto-runs.
//  - Change-only: the same file never runs twice.
//  - UTF-8 BOM stripped. 8MB cap. Empty/oversized files are ignored.
#include <Windows.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdint>
#include "BootLog.hpp"
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>

namespace ExecWatcher
{
namespace Detail
{
    // Poll cadence. The stat call is a few microseconds, so 2ms costs ~0.1%
    // of one thread while giving near-instant pickup on the atomic rename.
    inline constexpr DWORD kPollMs = 2;
    // Settle wait before reading: the app's temp+rename write is already
    // complete, this only covers non-atomic writers (manual saves).
    inline constexpr DWORD kSettleMs = 8;
    inline constexpr uint32_t kMaxScript = 8u * 1024u * 1024u;

    inline std::atomic<bool> s_running{ false };

    inline void WatchLoop()
    {
        char base[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char path[MAX_PATH + 32]{};
        snprintf(path, sizeof(path), "%s\\Viridian\\execute.lua", base);

        FILETIME lastWrite{};
        uint64_t lastSize = 0;
        bool haveLast = false;
        {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
            {
                lastWrite = fad.ftLastWriteTime;
                lastSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                haveLast = true;
                BootLog::WriteLine("exec", "stale execute.lua baselined, not run");
            }
        }

        while (s_running.load(std::memory_order_acquire))
        {
            Sleep(kPollMs);

            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
            {
                haveLast = false;
                continue;
            }
            const uint64_t curSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            if (haveLast &&
                fad.ftLastWriteTime.dwLowDateTime == lastWrite.dwLowDateTime &&
                fad.ftLastWriteTime.dwHighDateTime == lastWrite.dwHighDateTime &&
                curSize == lastSize)
                continue;

            // Settle check, then baseline BEFORE queueing so a failing read
            // can never loop on the same file.
            Sleep(kSettleMs);
            WIN32_FILE_ATTRIBUTE_DATA settled{};
            if (!GetFileAttributesExA(path, GetFileExInfoStandard, &settled))
            {
                haveLast = false;
                continue;
            }
            lastWrite = settled.ftLastWriteTime;
            lastSize = (static_cast<uint64_t>(settled.nFileSizeHigh) << 32) | settled.nFileSizeLow;
            haveLast = true;

            HANDLE h = CreateFileA(path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            const DWORD size = GetFileSize(h, nullptr);
            if (size == 0 || size == INVALID_FILE_SIZE || size > kMaxScript)
            {
                CloseHandle(h);
                BootLog::WriteLine("exec", "execute.lua ignored (empty or oversized)");
                continue;
            }
            std::string src;
            src.resize(size);
            DWORD rd = 0;
            const BOOL ok = ReadFile(h, src.data(), size, &rd, nullptr);
            CloseHandle(h);
            if (!ok || rd == 0)
                continue;
            src.resize(rd);
            if (src.size() >= 3 &&
                static_cast<unsigned char>(src[0]) == 0xEF &&
                static_cast<unsigned char>(src[1]) == 0xBB &&
                static_cast<unsigned char>(src[2]) == 0xBF)
                src.erase(0, 3);

            char msg[96]{};
            snprintf(msg, sizeof(msg), "queued execute.lua size=%u", static_cast<unsigned>(src.size()));
            BootLog::WriteLine("exec", msg);

            TaskScheduler::RequestExecution(std::move(src), 0);
        }
    }
} // namespace Detail

inline void Start()
{
    bool expected = false;
    if (!Detail::s_running.compare_exchange_strong(expected, true))
        return;
    std::thread(Detail::WatchLoop).detach();
}

inline void Stop()
{
    Detail::s_running.store(false, std::memory_order_release);
}

} // namespace ExecWatcher

#pragma once
// Boot log helper: appends timestamped lines to %LOCALAPPDATA%\Viridian\boot.log.
// Each write opens, appends one line, and closes (flush per line) so the UI
// tail and the status snapshot tail observe lines immediately.
#include <Windows.h>
#include <cstdio>
#include <string>
#include "StatusRing.hpp"

namespace BootLog
{
inline bool ResolveLogPath(char* out, size_t outSize)
{
    char base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return false;
    snprintf(out, outSize, "%s\\Viridian\\boot.log", base);
    return true;
}

inline void EnsureParent()
{
    char base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    CreateDirectoryA((std::string(base) + "\\Viridian").c_str(), nullptr);
}

// Append "[tag] message" with HH:MM:SS prefix. Never throws.
inline void WriteLine(const char* tag, const char* message)
{
    EnsureParent();
    char path[MAX_PATH + 32]{};
    if (!ResolveLogPath(path, sizeof(path)))
        return;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u [%s] %s\n", st.wHour, st.wMinute, st.wSecond, tag, message);
    fclose(f);
    StatusRing::RefreshLogTail();
}

// Append the BOOT pid line. Format: "YYYY-MM-DD HH:MM:SS BOOT pid=<pid>".
inline void WriteBootPid()
{
    EnsureParent();
    char path[MAX_PATH + 32]{};
    if (!ResolveLogPath(path, sizeof(path)))
        return;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u BOOT pid=%lu\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        static_cast<unsigned long>(GetCurrentProcessId()));
    fclose(f);
    StatusRing::RefreshLogTail();
}

// Append a phase marker: "[boot] phase=<name>".
inline void WritePhase(const char* phase)
{
    char msg[128]{};
    snprintf(msg, sizeof(msg), "phase=%s", phase);
    WriteLine("boot", msg);
}

// Refresh hook point: subsystems call this after writing lines directly.
inline void Refresh()
{
    StatusRing::RefreshLogTail();
}

} // namespace BootLog

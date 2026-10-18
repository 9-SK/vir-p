#pragma once

#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <intrin.h>

#include <Utilitys/Globals.hpp>

namespace CrashHandler
{
    inline volatile LONG g_handlingCrash = 0;

    static bool SafeRead(uintptr_t address, uintptr_t& result)
    {
        result = 0;

        __try
        {
            result = *reinterpret_cast<const uintptr_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void GetModuleName(uintptr_t address, char* out, size_t size)
    {
        if (!out || !size)
            return;

        out[0] = '\0';

        HMODULE module = nullptr;

        if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),
            &module))
        {
            sprintf_s(out, size, "0x%llX",
                static_cast<unsigned long long>(address));
            return;
        }

        char path[MAX_PATH]{};

        if (!GetModuleFileNameA(module, path, MAX_PATH))
        {
            sprintf_s(out, size, "0x%llX",
                static_cast<unsigned long long>(address));
            return;
        }

        const char* name = strrchr(path, '\\');
        name = name ? name + 1 : path;

        const uintptr_t base =
            reinterpret_cast<uintptr_t>(module);

        sprintf_s(
            out,
            size,
            "%s+0x%llX",
            name,
            static_cast<unsigned long long>(address - base)
        );
    }

    static void WriteRegisters(FILE* file, const CONTEXT* ctx)
    {
        if (!file || !ctx)
            return;

#ifdef _M_X64
        fprintf(
            file,
            "\nregisters:\n"
            "RAX = %016llX\n"
            "RBX = %016llX\n"
            "RCX = %016llX\n"
            "RDX = %016llX\n"
            "RSI = %016llX\n"
            "RDI = %016llX\n"
            "RBP = %016llX\n"
            "RSP = %016llX\n"
            "RIP = %016llX\n"
            "R8  = %016llX\n"
            "R9  = %016llX\n"
            "R10 = %016llX\n"
            "R11 = %016llX\n"
            "R12 = %016llX\n"
            "R13 = %016llX\n"
            "R14 = %016llX\n"
            "R15 = %016llX\n"
            "EFLAGS = %08lX\n",
            ctx->Rax,
            ctx->Rbx,
            ctx->Rcx,
            ctx->Rdx,
            ctx->Rsi,
            ctx->Rdi,
            ctx->Rbp,
            ctx->Rsp,
            ctx->Rip,
            ctx->R8,
            ctx->R9,
            ctx->R10,
            ctx->R11,
            ctx->R12,
            ctx->R13,
            ctx->R14,
            ctx->R15,
            ctx->EFlags
        );
#endif
    }

    static void WriteStack(FILE* file, const CONTEXT* ctx)
    {
        if (!file || !ctx)
            return;

#ifdef _M_X64
        fprintf(file, "\nstack:\n");

        uintptr_t stack = ctx->Rsp;

        for (int i = 0; i < 64; ++i)
        {
            uintptr_t value = 0;

            if (!SafeRead(stack + (i * sizeof(uintptr_t)), value))
                break;

            if (!value)
                continue;

            char symbol[512]{};
            GetModuleName(value, symbol, sizeof(symbol));

            fprintf(
                file,
                "  [%02d] %016llX  %s\n",
                i,
                static_cast<unsigned long long>(value),
                symbol
            );
        }
#endif
    }

    static void WriteException(FILE* file, EXCEPTION_POINTERS* ep)
    {
        if (!file || !ep || !ep->ExceptionRecord)
            return;

        const EXCEPTION_RECORD* record = ep->ExceptionRecord;
        const CONTEXT* context = ep->ContextRecord;

        char address[512]{};

        GetModuleName(
            reinterpret_cast<uintptr_t>(record->ExceptionAddress),
            address,
            sizeof(address)
        );

        fprintf(
            file,
            "exception:\n"
            "  code      = 0x%08lX\n"
            "  flags     = 0x%08lX\n"
            "  address   = %s\n"
            "  parameters = %lu\n",
            record->ExceptionCode,
            record->ExceptionFlags,
            address,
            record->NumberParameters
        );

        for (DWORD i = 0;
            i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS;
            ++i)
        {
            fprintf(
                file,
                "  param[%lu] = 0x%llX\n",
                i,
                static_cast<unsigned long long>(
                    record->ExceptionInformation[i]
                    )
            );
        }

        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            record->NumberParameters >= 2)
        {
            const char* type =
                record->ExceptionInformation[0] == 0
                ? "read"
                : record->ExceptionInformation[0] == 1
                ? "write"
                : "execute";

            fprintf(
                file,
                "  access    = %s\n"
                "  address   = 0x%llX\n",
                type,
                static_cast<unsigned long long>(
                    record->ExceptionInformation[1]
                    )
            );
        }

        WriteRegisters(file, context);
        WriteStack(file, context);
    }

    static LONG WINAPI Handler(EXCEPTION_POINTERS* ep)
    {
        if (InterlockedCompareExchange(&g_handlingCrash, 1, 0) != 0)
            return EXCEPTION_CONTINUE_SEARCH;

        wchar_t folder[MAX_PATH]{};
        wchar_t path[MAX_PATH]{};

        if (!ExpandEnvironmentStringsW(
            L"%LOCALAPPDATA%\\Viridian\\Crashlogs",
            folder,
            MAX_PATH))
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Create both levels — CreateDirectoryW only makes the leaf
        wchar_t base[MAX_PATH]{};
        if (ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Viridian", base, MAX_PATH))
        {
            CreateDirectoryW(base, nullptr);
        }
        CreateDirectoryW(folder, nullptr);

        swprintf_s(
            path,
            L"%s\\Crashlog_%lu.txt",
            folder,
            GetCurrentProcessId()
        );

        FILE* file = nullptr;

        if (_wfopen_s(&file, path, L"w") != 0 || !file)
            return EXCEPTION_CONTINUE_SEARCH;

        SYSTEMTIME time{};
        GetLocalTime(&time);

        fprintf(
            file,
            "========================================\n"
            "Viridian Crash Log\n"
            "========================================\n\n"
            "time: %04u-%02u-%02u %02u:%02u:%02u\n"
            "pid:  %lu\n"
            "tid:  %lu\n\n",
            time.wYear,
            time.wMonth,
            time.wDay,
            time.wHour,
            time.wMinute,
            time.wSecond,
            GetCurrentProcessId(),
            GetCurrentThreadId()
        );

        __try
        {
            WriteException(file, ep);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            fprintf(file, "\n[exception while generating crash information]\n");
        }

        fprintf(file, "\n========================================\n");

        fclose(file);

        __try
        {
            wchar_t message[1024]{};

            swprintf_s(
                message,
                L"A crash occurred.\n\nCrash log:\n%s",
                path
            );

            // Owned by the game window at crash time (foreground): modal to the
            // game so the dialog can't hide behind it. MB_TOPMOST preserved
            // (already intended here). Only reachable from the unhandled-
            // exception Handler (g_handlingCrash-guarded), never spontaneous.
            HWND owner = GetForegroundWindow();
            MessageBoxW(
                owner,
                message,
                L"Viridian",
                MB_OK | MB_ICONERROR | MB_TOPMOST
            );
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        TerminateProcess(GetCurrentProcess(), 0);

        return EXCEPTION_CONTINUE_SEARCH;
    }

    inline void Initialize()
    {
        SetUnhandledExceptionFilter(Handler);
    }
}
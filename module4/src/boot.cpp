#include "contract.h"
#include "ipc.h"
#include <Windows.h>
#include <cstdio>

static DWORD __stdcall viridian_worker(void* pv);

struct WorkerCtx { int active = 1; };

static WorkerCtx* g_ctx = nullptr;

static void spawn_worker() {
    auto* ctx = (WorkerCtx*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WorkerCtx));
    if (!ctx) return;
    g_ctx = ctx;
    HANDLE h = CreateThread(nullptr, 0, viridian_worker, ctx, 0, nullptr);
    if (!h) {
        HeapFree(GetProcessHeap(), 0, ctx);
        g_ctx = nullptr;
        return;
    }
    CloseHandle(h);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinst);
        spawn_worker();
        break;
    case DLL_PROCESS_DETACH:
        if (g_ctx) g_ctx->active = 0;
        break;
    default:
        break;
    }
    return TRUE;
}

static uintptr_t find_client_base() {
    return (uintptr_t)GetModuleHandleA(nullptr);
}

static bool read_sentinel(uintptr_t base, uintptr_t& dm_out, unsigned long long& value_out) {
    __try {
        uintptr_t fake = *(uintptr_t*)(base + (uintptr_t)rv::FakeDataModel);
        if (!fake) return false;
        uintptr_t dm = *(uintptr_t*)(fake + (uintptr_t)rv::DataModelPtr);
        if (!dm) return false;
        dm_out = dm;
        value_out = *(unsigned long long*)(dm + (uintptr_t)rv::GameLoadedSentinel);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static DWORD __stdcall viridian_worker(void* pv) {
    auto* ctx = (WorkerCtx*)pv;
    if (!ctx) return 1;

    ipc::ensure_dir();
    ipc::write_bootlog("[boot] worker up (v0.2)");

    uintptr_t client = find_client_base();
    char buf[192];
    if (!client) {
        ipc::write_bootlog("[boot] FAIL: no exe module found");
        ctx->active = 0;
        HeapFree(GetProcessHeap(), 0, ctx);
        return 1;
    }
    sprintf_s(buf, sizeof(buf), "[boot] client exe base=0x%llX", (unsigned long long)client);
    ipc::write_bootlog(buf);

    ipc::write_status("booted v0.2 armed");

    bool announced_dm = false;
    bool reported_fail = false;
    unsigned long long last_sentinel = ~0ull;

    while (ctx->active) {
        uintptr_t dm = 0;
        unsigned long long sentinel = 0;
        if (read_sentinel(client, dm, sentinel)) {
            reported_fail = false;
            if (!announced_dm) {
                announced_dm = true;
                sprintf_s(buf, sizeof(buf), "[arm] dm=0x%llX", (unsigned long long)dm);
                ipc::write_bootlog(buf);
            }
            if (sentinel != last_sentinel) {
                last_sentinel = sentinel;
                if (sentinel == rv::GameLoadedValue) {
                    sprintf_s(buf, sizeof(buf), "[arm] sentinel=%llu game loaded", sentinel);
                    ipc::write_status("armed; game loaded");
                } else {
                    sprintf_s(buf, sizeof(buf), "[arm] sentinel=%llu", sentinel);
                }
                ipc::write_bootlog(buf);
            }
        } else if (!reported_fail) {
            reported_fail = true;
            ipc::write_bootlog("[arm] sentinel read fault (retrying)");
        }
        Sleep(1000);
    }

    HeapFree(GetProcessHeap(), 0, ctx);
    return 0;
}

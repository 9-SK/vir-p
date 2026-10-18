



#include "real.h"
#include "ipc.h"
#include <Windows.h>

typedef unsigned long long uint64x;

static DWORD __stdcall viridian_worker(void* pv);

struct WorkerCtx { int active = 1; };

static void spawn_worker() {
    auto* ctx = (WorkerCtx*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WorkerCtx));
    if (!ctx) return;
    HANDLE h = CreateThread(nullptr, 0, viridian_worker, ctx, 0, nullptr);
    if (!h) {
        HeapFree(GetProcessHeap(), 0, ctx);
        return;
    }
    
    
    CloseHandle(h);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinst);
        ipc::ensure_dir();
        ipc::write_bootlog("[boot] dll attached; spawning worker (v0.1)");
        spawn_worker();
        break;
    case DLL_PROCESS_DETACH:
        ipc::write_bootlog("[boot] dll detached");
        break;
    default:
        break;
    }
    return TRUE;
}



#include <string>
#include <cstdio>




static uintptr_t find_client_base() {
    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t addr = 0;
    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.Type == MEM_IMAGE && (mbi.AllocationProtect & PAGE_EXECUTE)) {
            
            
            return (uintptr_t)mbi.AllocationBase;
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 0;
}

static DWORD __stdcall viridian_worker(void* pv) {
    auto* ctx = (WorkerCtx*)pv;
    if (!ctx) return 1;

    ipc::write_bootlog("[boot] worker up");

    uintptr_t client = find_client_base();
    char buf[160];
    if (client) {
        sprintf_s(buf, sizeof(buf), "[boot] client image base=0x%llX",
                  (unsigned long long)client);
        ipc::write_bootlog(buf);
    } else {
        ipc::write_bootlog("[boot] FAIL: no executable image found");
        ctx->active = 0;
        HeapFree(GetProcessHeap(), 0, ctx);
        return 1;
    }

    
    
    
    ipc::write_bootlog("[boot] v0.1 passive skeleton complete");
    ipc::write_status("booted v0.1 passive");

    
    while (ctx->active) Sleep(1000);

    HeapFree(GetProcessHeap(), 0, ctx);
    return 0;
}

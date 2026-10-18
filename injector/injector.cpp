#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <ktmw32.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "ws2_32.lib")








namespace dbgwin {
    static HWND   g_wnd  = nullptr;
    static HWND   g_edit = nullptr;
    static HFONT  g_font = nullptr;
    static HBRUSH g_bg   = nullptr;
    static bool g_destroyed = false;
    static std::vector<std::string> g_pending; 

    static void AppendLine(const std::string& s); 

    static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_SIZE:
            if (g_edit) MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == g_edit) {
                SetBkColor((HDC)wp, RGB(12, 12, 12));
                SetTextColor((HDC)wp, RGB(186, 255, 186));
                return (LRESULT)(INT_PTR)g_bg;
            }
            break;
        case WM_COPYDATA: {
            
            
            auto* cds = (COPYDATASTRUCT*)lp;
            if (cds && cds->lpData && cds->cbData && cds->dwData == 0x56444C4Du) {
                char buf[1600];
                size_t n = cds->cbData < sizeof(buf) - 1 ? (size_t)cds->cbData : sizeof(buf) - 1;
                memcpy(buf, cds->lpData, n);
                buf[n] = 0;
                for (char* p = buf; *p; ++p)
                    if (*p == '\r' || *p == '\n') *p = ' ';
                SYSTEMTIME st;
                GetLocalTime(&st);
                char line[1700];
                snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u][dll] %s\r\n",
                         (unsigned)st.wHour, (unsigned)st.wMinute,
                         (unsigned)st.wSecond, (unsigned)st.wMilliseconds, buf);
                AppendLine(line);
            }
            return TRUE;
        }
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            g_destroyed = true;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcA(w, msg, wp, lp);
    }

    static void AppendLine(const std::string& s) {
        int len = GetWindowTextLengthA(g_edit);
        if (len > 240000) { 
            SendMessageA(g_edit, EM_SETSEL, 0, len / 2);
            SendMessageA(g_edit, EM_REPLACESEL, FALSE, (LPARAM)"...[older lines trimmed]...\r\n");
            len = GetWindowTextLengthA(g_edit);
        }
        SendMessageA(g_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(g_edit, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
        SendMessageA(g_edit, EM_SCROLLCARET, 0, 0);
    }

    static void Pump() {
        MSG m{};
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }

    static void Open() {
        if (g_wnd) return;
        WNDCLASSA wc{};
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.hCursor       = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(12, 12, 12));
        wc.lpszClassName = "ViridianInjDbg";
        RegisterClassA(&wc);
        g_wnd = CreateWindowExA(0, "ViridianInjDbg", "Viridian injector",
                                WS_OVERLAPPEDWINDOW, 60, 60, 800, 540,
                                nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_wnd) return;
        g_edit = CreateWindowExA(0, "EDIT", "",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT |
                                 ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 0, 0, 780, 500, g_wnd, nullptr, wc.hInstance, nullptr);
        g_font = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if (g_edit) {
            SendMessageA(g_edit, WM_SETFONT, (WPARAM)g_font, 0);
            SendMessageA(g_edit, EM_SETLIMITTEXT, 0x100000, 0);
        }
        g_bg = CreateSolidBrush(RGB(12, 12, 12));
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
        if (g_edit) for (auto& s : g_pending) AppendLine(s);
        g_pending.clear();
    }

    
    
    static void LogFile(const char* line) {
        static FILE* fp = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            char base[MAX_PATH]{};
            DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
            if (n && n < MAX_PATH) {
                char dir[MAX_PATH]{};
                snprintf(dir, sizeof(dir), "%s\\Viridian", base);
                CreateDirectoryA(dir, nullptr);
                char path[MAX_PATH]{};
                snprintf(path, sizeof(path), "%s\\injector.log", dir);
                fp = _fsopen(path, "a", _SH_DENYNO);
            }
        }
        OutputDebugStringA(line);
        if (fp) { fputs(line, fp); fflush(fp); }
    }

    static void log(const char* fmt, ...) {
        char msg[1400];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[1500];
        snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] %s\r\n",
                 (unsigned)st.wHour, (unsigned)st.wMinute,
                 (unsigned)st.wSecond, (unsigned)st.wMilliseconds, msg);
        LogFile(line);
        if (g_wnd && g_edit && !g_destroyed) AppendLine(line);
        else g_pending.push_back(line);
        Pump(); 
    }

    
    static void DiscardPending() { g_pending.clear(); g_pending.shrink_to_fit(); }

    static void Title(const char* t) {
        if (g_wnd && !g_destroyed) SetWindowTextA(g_wnd, t);
    }

    
    
    
    
    static void WaitClose(int autoCloseMs) {
        ULONGLONG start = GetTickCount64();
        while (g_wnd && !g_destroyed) {
            Pump();
            if (g_destroyed) break;
            if (autoCloseMs >= 0 && GetTickCount64() - start >= (ULONGLONG)autoCloseMs)
                DestroyWindow(g_wnd);
            Sleep(40);
        }
    }
}


static int Fail(const char* what, DWORD gle = 0, unsigned long st = 0) {
    if (gle)     dbgwin::log("[FAIL] %s (GetLastError=%lu)", what, gle);
    else if (st) dbgwin::log("[FAIL] %s (NTSTATUS=0x%08lX)", what, st);
    else         dbgwin::log("[FAIL] %s", what);
    dbgwin::Title("Viridian injector - FAILED");
    return 1;
}


static constexpr uint8_t XOR_KEY[] = { 'M','o','d','o','v','3','!' };

template<size_t N>
struct EncStr {
    uint8_t data[N];
};

template<size_t N>
static constexpr EncStr<N> Enc(const char (&s)[N]) {
    EncStr<N> r{};
    for (size_t i = 0; i < N; i++)
        r.data[i] = (uint8_t)s[i] ^ XOR_KEY[i % 7];
    return r;
}

static void Dec(const uint8_t* enc, size_t len, char* out) {
    for (size_t i = 0; i < len; i++)
        out[i] = (char)(enc[i] ^ XOR_KEY[i % 7]);
    out[len] = 0;
}

static constexpr auto enc_ntdll           = Enc("ntdll.dll");
static constexpr auto enc_NtWriteVirtual = Enc("NtWriteVirtualMemory");
static constexpr auto enc_NtCreateSection= Enc("NtCreateSection");
static constexpr auto enc_NtMapView      = Enc("NtMapViewOfSection");
static constexpr auto enc_NtUnmapView    = Enc("NtUnmapViewOfSection");
static constexpr auto enc_NtQueryObject  = Enc("NtQueryObject");
static constexpr auto enc_ZwSetIoCompl   = Enc("ZwSetIoCompletion");
static constexpr auto enc_SeDebug        = Enc("SeDebugPrivilege");
static constexpr auto enc_RobloxPlayer   = Enc("RobloxPlayerBeta");
static constexpr auto enc_kernel32       = Enc("kernel32.dll");
static constexpr auto enc_LoadLibraryA   = Enc("LoadLibraryA");
static constexpr auto enc_GetProcAddress = Enc("GetProcAddress");
static constexpr auto enc_Module         = Enc("viridian.dll");

static constexpr uint8_t enc_IoCompletion[] = {
    0x04, 0x6F, 0x0B, 0x6F, 0x35, 0x33, 0x4E, 0x4D,
    0x02, 0x64, 0x1F, 0x76, 0x5F, 0x21, 0x28, 0x6F,
    0x10, 0x6F, 0x1F, 0x33, 0x4E, 0x4D, 0x01, 0x64
};


typedef NTSTATUS(NTAPI* NtWriteVirtualMemory_t)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS(NTAPI* NtCreateSection_t)(PHANDLE, ACCESS_MASK, PVOID, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef NTSTATUS(NTAPI* NtMapViewOfSection_t)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
typedef NTSTATUS(NTAPI* NtUnmapViewOfSection_t)(HANDLE, PVOID);
typedef NTSTATUS(NTAPI* NtQueryObject_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI* NtSetIoCompletion_t)(HANDLE, PVOID, PVOID, NTSTATUS, ULONG_PTR);
typedef NTSTATUS(NTAPI* NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static NtWriteVirtualMemory_t      pNtWriteVirtualMemory;
static NtCreateSection_t           pNtCreateSection;
static NtMapViewOfSection_t        pNtMapViewOfSection;
static NtUnmapViewOfSection_t      pNtUnmapViewOfSection;
static NtQueryObject_t             pNtQueryObject;
static NtSetIoCompletion_t         pNtSetIoCompletion;
static NtQueryInformationProcess_t pNtQueryInformationProcess;

static SIZE_T AlignUp(SIZE_T v, SIZE_T a) { return (v + a - 1) & ~(a - 1); }

static bool ResolveNtdll() {




    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;

    pNtWriteVirtualMemory      = (NtWriteVirtualMemory_t)      GetProcAddress(ntdll, "NtWriteVirtualMemory");
    pNtCreateSection           = (NtCreateSection_t)           GetProcAddress(ntdll, "NtCreateSection");
    pNtMapViewOfSection        = (NtMapViewOfSection_t)        GetProcAddress(ntdll, "NtMapViewOfSection");
    pNtUnmapViewOfSection      = (NtUnmapViewOfSection_t)      GetProcAddress(ntdll, "NtUnmapViewOfSection");
    pNtQueryObject             = (NtQueryObject_t)             GetProcAddress(ntdll, "NtQueryObject");
    pNtSetIoCompletion         = (NtSetIoCompletion_t)         GetProcAddress(ntdll, "ZwSetIoCompletion");
    pNtQueryInformationProcess = (NtQueryInformationProcess_t) GetProcAddress(ntdll, "NtQueryInformationProcess");

    return pNtWriteVirtualMemory && pNtCreateSection && pNtMapViewOfSection &&
           pNtUnmapViewOfSection && pNtQueryObject && pNtSetIoCompletion &&
           pNtQueryInformationProcess;
}

static bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;
    char buf[32];
    Dec(enc_SeDebug.data, sizeof(enc_SeDebug.data) - 1, buf);
    LUID luid{};
    if (!LookupPrivilegeValueA(nullptr, buf, &luid)) { CloseHandle(hToken); return false; }
    TOKEN_PRIVILEGES tp{ 1, {{ luid, SE_PRIVILEGE_ENABLED }} };
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return true;
}

static DWORD FindRobloxProcess() {
    DWORD pids[1024], cbNeeded = 0;
    if (!EnumProcesses(pids, sizeof(pids), &cbNeeded)) return 0;
    DWORD count = cbNeeded / sizeof(DWORD);
    char target[32];
    Dec(enc_RobloxPlayer.data, sizeof(enc_RobloxPlayer.data) - 1, target);
    for (DWORD i = 0; i < count; i++) {
        if (!pids[i]) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        char path[MAX_PATH]{};
        DWORD len = MAX_PATH;
        bool hit = false;
        if (QueryFullProcessImageNameA(h, 0, path, &len)) {
            if (strstr(path, target)) hit = true;
        }
        CloseHandle(h);
        if (hit) return pids[i];
    }
    return 0;
}

static uint64_t RemoteGetModule(HANDLE process, const char* name) {
    PROCESS_BASIC_INFORMATION pbi{};
    ULONG ret = 0;
    if (pNtQueryInformationProcess(process, 0, &pbi, sizeof(pbi), &ret) < 0)
        return 0;

    uint64_t peb = (uint64_t)pbi.PebBaseAddress;
    uint64_t ldr = 0;
    ReadProcessMemory(process, (void*)(peb + 0x18), &ldr, 8, nullptr);
    if (!ldr) return 0;

    uint64_t head = ldr + 0x10, cur = 0;
    ReadProcessMemory(process, (void*)head, &cur, 8, nullptr);

    while (cur && cur != head) {
        uint64_t base = 0;
        ReadProcessMemory(process, (void*)(cur + 0x30), &base, 8, nullptr);

        UNICODE_STRING us{};
        ReadProcessMemory(process, (void*)(cur + 0x58), &us, sizeof(us), nullptr);

        if (base && us.Buffer && us.Length) {
            std::vector<wchar_t> mod(us.Length / sizeof(wchar_t) + 1, L'\0');
            ReadProcessMemory(process, us.Buffer, mod.data(), us.Length, nullptr);

            char narrow[128]{};
            WideCharToMultiByte(CP_ACP, 0, mod.data(), -1, narrow, sizeof(narrow), nullptr, nullptr);
            if (_stricmp(narrow, name) == 0)
                return base;
        }

        uint64_t next = 0;
        ReadProcessMemory(process, (void*)cur, &next, 8, nullptr);
        cur = next;
    }
    return 0;
}

static uint64_t RemoteGetProc(HANDLE process, uint64_t modBase, const char* fn) {
    IMAGE_DOS_HEADER dos{};
    if (!ReadProcessMemory(process, (void*)modBase, &dos, sizeof(dos), nullptr) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    IMAGE_NT_HEADERS64 nt{};
    if (!ReadProcessMemory(process, (void*)(modBase + dos.e_lfanew), &nt, sizeof(nt), nullptr) ||
        nt.Signature != IMAGE_NT_SIGNATURE)
        return 0;

    DWORD expRva = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!expRva) return 0;

    IMAGE_EXPORT_DIRECTORY exp{};
    ReadProcessMemory(process, (void*)(modBase + expRva), &exp, sizeof(exp), nullptr);

    std::vector<DWORD> names(exp.NumberOfNames), funcs(exp.NumberOfFunctions);
    std::vector<WORD> ords(exp.NumberOfNames);
    ReadProcessMemory(process, (void*)(modBase + exp.AddressOfNames), names.data(), names.size() * 4, nullptr);
    ReadProcessMemory(process, (void*)(modBase + exp.AddressOfNameOrdinals), ords.data(), ords.size() * 2, nullptr);
    ReadProcessMemory(process, (void*)(modBase + exp.AddressOfFunctions), funcs.data(), funcs.size() * 4, nullptr);

    for (DWORD i = 0; i < exp.NumberOfNames; ++i) {
        char n[128]{};
        ReadProcessMemory(process, (void*)(modBase + names[i]), n, sizeof(n) - 1, nullptr);
        if (_stricmp(n, fn) == 0 && ords[i] < exp.NumberOfFunctions)
            return modBase + funcs[ords[i]];
    }
    return 0;
}

static HANDLE FindIoCompletion(HANDLE process) {
    uint8_t target[24];
    for (size_t i = 0; i < 24; i++)
        target[i] = enc_IoCompletion[i] ^ XOR_KEY[i % 7];

    DWORD startValue = (rand() % 17) + 1;
    for (DWORD i = 0; i < 0x4000; i++) {
        if ((i & 0x1FF) == 0) dbgwin::Pump(); 
        DWORD h = startValue + i;
        if (h > 0x4000) h -= 0x4000;

        HANDLE dup = nullptr;
        if (!DuplicateHandle(process, (HANDLE)(ULONG_PTR)h, GetCurrentProcess(),
                             &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
            continue;

        ULONG infoBytes = 0x1000;
        std::vector<uint8_t> info(infoBytes);
        NTSTATUS status = pNtQueryObject(dup, 2, info.data(), infoBytes, &infoBytes);
        if (status == (NTSTATUS)0xC0000004L) {
            info.resize(infoBytes);
            status = pNtQueryObject(dup, 2, info.data(), infoBytes, &infoBytes);
        }
        if (status >= 0) {
            const uint16_t nameBytes = *(uint16_t*)info.data();
            const wchar_t* name = *(wchar_t**)(info.data() + 8);
            if (name && nameBytes == 24 && memcmp(name, target, 24) == 0)
                return dup;
        }
        CloseHandle(dup);
    }
    return nullptr;
}

static void* FindZeroedTpSlot(HANDLE process) {
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<uint8_t> buffer(0x1000);
    constexpr SIZE_T kAlign = sizeof(void*);
    for (uint8_t* address = nullptr;
         VirtualQueryEx(process, address, &mbi, sizeof(mbi));
         address = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        static int pumpTick = 0;
        if ((++pumpTick & 0x3F) == 0) dbgwin::Pump();
        if (mbi.State != MEM_COMMIT || mbi.Protect != PAGE_READWRITE) continue;
        for (SIZE_T offset = 0; offset + 0x48 <= mbi.RegionSize; offset += buffer.size()) {
            SIZE_T read = 0;
            const SIZE_T count = (std::min)(buffer.size(), mbi.RegionSize - offset);
            if (!ReadProcessMemory(process, (uint8_t*)mbi.BaseAddress + offset, buffer.data(), count, &read))
                break;
            for (SIZE_T i = 0; i + 0x48 <= read; i += kAlign) {
                bool zero = true;
                for (SIZE_T j = 0; j < 0x48; ++j) if (buffer[i + j]) { zero = false; break; }
                if (zero) return (uint8_t*)mbi.BaseAddress + offset + i;
            }
        }
    }
    return nullptr;
}



static void SweepStaleStagingTemps() {
    char dir[MAX_PATH]{};
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (!n || n >= MAX_PATH) return;
    const char* patterns[2] = { "yst*.tmp", "BYF*.tmp" };
    int removed = 0;
    for (int pi = 0; pi < 2; ++pi) {
        char pattern[MAX_PATH]{};
        lstrcpyA(pattern, dir);
        lstrcatA(pattern, patterns[pi]);
        WIN32_FIND_DATAA fd{};
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            char full[MAX_PATH]{};
            lstrcpyA(full, dir);
            lstrcatA(full, fd.cFileName);
            if (DeleteFileA(full)) removed++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}


static std::vector<HANDLE> g_suspended;

static void SuspendAllThreads(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{ sizeof(te) };
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (t) {
                    if (SuspendThread(t) != (DWORD)-1) g_suspended.push_back(t);
                    else CloseHandle(t);
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static void ResumeAllThreads() {
    for (HANDLE t : g_suspended) { ResumeThread(t); CloseHandle(t); }
    g_suspended.clear();
}

struct LoaderContext {
    uint64_t          imageBase;
    void*             fLoadLibraryA;
    void*             fGetProcAddress;
    volatile uint64_t status;
};

static const unsigned char loader_stub[0x308] = {
    0x48, 0x85, 0xc9, 0x0f, 0x84, 0xfe, 0x02, 0x00, 0x00, 0x48, 0x8b, 0xc4,
    0x48, 0x89, 0x48, 0x08, 0x56, 0x48, 0x83, 0xec, 0x70, 0x48, 0x89, 0x58,
    0xf0, 0x48, 0x8b, 0xf1, 0x48, 0x89, 0x68, 0xe8, 0x48, 0x89, 0x78, 0xe0,
    0x4c, 0x89, 0x60, 0xd8, 0x4c, 0x89, 0x68, 0xd0, 0x4c, 0x8b, 0x69, 0x10,
    0x4c, 0x89, 0x70, 0xc8, 0x4c, 0x8b, 0x31, 0x4c, 0x89, 0x78, 0xc0, 0xc7,
    0x41, 0x18, 0x01, 0x00, 0x00, 0x00, 0x49, 0x63, 0x6e, 0x3c, 0x49, 0x03,
    0xee, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x8b, 0x45, 0x28, 0x49, 0x03, 0xc6,
    0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8b, 0x41, 0x08, 0x48, 0x89, 0x84,
    0x24, 0x98, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x60, 0x00,
    0x00, 0x00, 0x48, 0x8b, 0x40, 0x18, 0x48, 0x8b, 0x40, 0x10, 0x66, 0x66,
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0x78, 0x30,
    0x49, 0x63, 0x4f, 0x3c, 0x42, 0x8b, 0xbc, 0x39, 0x88, 0x00, 0x00, 0x00,
    0x49, 0x03, 0xff, 0x49, 0x3b, 0xff, 0x74, 0x61, 0x44, 0x8b, 0x57, 0x18,
    0x45, 0x85, 0xd2, 0x74, 0x58, 0x8b, 0x5f, 0x20, 0x49, 0x03, 0xdf, 0x66,
    0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0xff, 0xca, 0x41,
    0xb9, 0xfc, 0xf1, 0xf5, 0xb3, 0x42, 0x8b, 0x14, 0x93, 0x45, 0x0f, 0xb6,
    0x04, 0x17, 0x48, 0x8d, 0x4a, 0x01, 0x49, 0x03, 0xcf, 0x45, 0x84, 0xc0,
    0x74, 0x26, 0x66, 0x90, 0x41, 0x0f, 0xbe, 0xd0, 0x44, 0x0f, 0xb6, 0x01,
    0x48, 0x8d, 0x49, 0x01, 0x41, 0x33, 0xd1, 0x44, 0x69, 0xca, 0x93, 0x01,
    0x00, 0x01, 0x45, 0x84, 0xc0, 0x75, 0xe5, 0x41, 0x81, 0xf9, 0xd7, 0x06,
    0x1d, 0xee, 0x74, 0x0a, 0x45, 0x85, 0xd2, 0x75, 0xb7, 0x48, 0x8b, 0x00,
    0xeb, 0x82, 0x8b, 0x4f, 0x24, 0x49, 0x03, 0xcf, 0x42, 0x0f, 0xb7, 0x14,
    0x51, 0x8b, 0x4f, 0x1c, 0x49, 0x8b, 0xfe, 0x49, 0x03, 0xcf, 0x44, 0x8b,
    0x24, 0x91, 0x44, 0x89, 0xa4, 0x24, 0x90, 0x00, 0x00, 0x00, 0x48, 0x2b,
    0x7d, 0x30, 0x0f, 0x84, 0x8a, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x9d, 0xb0,
    0x00, 0x00, 0x00, 0x8b, 0xc3, 0x48, 0xc1, 0xeb, 0x20, 0x48, 0x03, 0xd8,
    0x49, 0x03, 0xde, 0x4e, 0x8d, 0x14, 0x30, 0x4c, 0x3b, 0xd3, 0x73, 0x6e,
    0xb9, 0x00, 0xf0, 0x00, 0x00, 0xc7, 0x84, 0x24, 0x88, 0x00, 0x00, 0x00,
    0x00, 0xa0, 0x00, 0x00, 0x41, 0x8b, 0x42, 0x04, 0x85, 0xc0, 0x74, 0x56,
    0x4c, 0x8d, 0x58, 0xf8, 0x45, 0x33, 0xc0, 0x49, 0xd1, 0xeb, 0x4d, 0x8d,
    0x4a, 0x08, 0x45, 0x85, 0xdb, 0x74, 0x37, 0x90, 0x41, 0x0f, 0xb7, 0x11,
    0x0f, 0xb7, 0xc2, 0x66, 0x23, 0xc1, 0x66, 0x3b, 0x84, 0x24, 0x88, 0x00,
    0x00, 0x00, 0x75, 0x16, 0x41, 0x8b, 0x0a, 0x81, 0xe2, 0xff, 0x0f, 0x00,
    0x00, 0x49, 0x8d, 0x04, 0x16, 0x48, 0x01, 0x3c, 0x01, 0xb9, 0x00, 0xf0,
    0x00, 0x00, 0x41, 0xff, 0xc0, 0x49, 0x83, 0xc1, 0x02, 0x45, 0x3b, 0xc3,
    0x72, 0xca, 0x41, 0x8b, 0x42, 0x04, 0x4c, 0x03, 0xd0, 0x4c, 0x3b, 0xd3,
    0x72, 0xa2, 0xc7, 0x46, 0x18, 0x02, 0x00, 0x00, 0x00, 0x8b, 0xad, 0x90,
    0x00, 0x00, 0x00, 0x49, 0x03, 0xee, 0x83, 0x7d, 0x0c, 0x00, 0x74, 0x78,
    0x4c, 0x8b, 0xa4, 0x24, 0x98, 0x00, 0x00, 0x00, 0x8b, 0x4d, 0x0c, 0x49,
    0x03, 0xce, 0x41, 0xff, 0xd4, 0x44, 0x8b, 0x45, 0x00, 0x48, 0x8b, 0xf0,
    0x8b, 0x45, 0x10, 0x8b, 0xd8, 0x4b, 0x8d, 0x0c, 0x30, 0x48, 0x85, 0xc9,
    0x41, 0x0f, 0x45, 0xd8, 0x49, 0x03, 0xde, 0x48, 0x8b, 0x0b, 0x48, 0x85,
    0xc9, 0x74, 0x2b, 0x4a, 0x8d, 0x3c, 0x30, 0x79, 0x05, 0x0f, 0xb7, 0xd1,
    0xeb, 0x07, 0x49, 0x8d, 0x56, 0x02, 0x48, 0x03, 0xd1, 0x48, 0x8b, 0xce,
    0x41, 0xff, 0xd5, 0x48, 0x83, 0xc3, 0x08, 0x48, 0x89, 0x07, 0x48, 0x83,
    0xc7, 0x08, 0x48, 0x8b, 0x0b, 0x48, 0x85, 0xc9, 0x75, 0xd9, 0x48, 0x83,
    0xc5, 0x14, 0x83, 0x7d, 0x0c, 0x00, 0x75, 0xa0, 0x44, 0x8b, 0xa4, 0x24,
    0x90, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xb4, 0x24, 0x80, 0x00, 0x00, 0x00,
    0x48, 0x8b, 0x5c, 0x24, 0x20, 0x4c, 0x8b, 0x6c, 0x24, 0x48, 0x48, 0x8b,
    0x7c, 0x24, 0x58, 0x48, 0x8b, 0x6c, 0x24, 0x60, 0xc7, 0x46, 0x18, 0x03,
    0x00, 0x00, 0x00, 0x4c, 0x8b, 0x83, 0xa0, 0x00, 0x00, 0x00, 0x49, 0x8b,
    0xc0, 0x48, 0xc1, 0xe8, 0x20, 0x85, 0xc0, 0x74, 0x25, 0x48, 0x8b, 0xc8,
    0x48, 0xb8, 0xab, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x48, 0xf7,
    0xe1, 0x41, 0x8b, 0xc8, 0x4d, 0x8b, 0xc6, 0x41, 0x8b, 0xc4, 0x49, 0x03,
    0xce, 0x48, 0xc1, 0xea, 0x03, 0x49, 0x03, 0xc7, 0xff, 0xd0, 0x48, 0x8b,
    0x8b, 0xd0, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0x7c, 0x24, 0x38, 0x48, 0x8b,
    0xc1, 0x4c, 0x8b, 0x64, 0x24, 0x50, 0x48, 0xc1, 0xe8, 0x20, 0x85, 0xc0,
    0x74, 0x2d, 0x8b, 0xc1, 0x4a, 0x8b, 0x5c, 0x30, 0x18, 0x48, 0x85, 0xdb,
    0x74, 0x21, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00, 0x48, 0x8b, 0x03, 0x48,
    0x85, 0xc0, 0x74, 0x13, 0x45, 0x33, 0xc0, 0xba, 0x01, 0x00, 0x00, 0x00,
    0x49, 0x8b, 0xce, 0xff, 0xd0, 0x48, 0x83, 0xc3, 0x08, 0x75, 0xe5, 0x45,
    0x33, 0xc0, 0xc7, 0x46, 0x18, 0x04, 0x00, 0x00, 0x00, 0xba, 0x01, 0x00,
    0x00, 0x00, 0x49, 0x8b, 0xce, 0xff, 0x54, 0x24, 0x28, 0x4c, 0x8b, 0x74,
    0x24, 0x40, 0x48, 0x8b, 0x5c, 0x24, 0x68, 0xc7, 0x46, 0x18, 0x05, 0x00,
    0x00, 0x00, 0x48, 0x83, 0xc4, 0x70, 0x5e, 0xc3,
};

static unsigned long g_lastNtStatus = 0; 

static bool NtWrite(HANDLE proc, void* addr, const void* data, SIZE_T size) {
    SIZE_T written = 0;
    NTSTATUS st = pNtWriteVirtualMemory(proc, addr, const_cast<void*>(data), size, &written);
    g_lastNtStatus = (unsigned long)st;
    return st >= 0 && written == size;
}




struct MapChunk { uint64_t base; SIZE_T size; };

static bool ChunkedWrite(HANDLE process, const std::vector<MapChunk>& chunks,
                         uint64_t dest, const void* src, SIZE_T size) {
    const uint8_t* p = (const uint8_t*)src;
    SIZE_T done = 0;
    while (done < size) {
        bool placed = false;
        for (auto& c : chunks) {
            if (dest + done >= c.base && dest + done < c.base + c.size) {
                SIZE_T n = (SIZE_T)((c.base + c.size) - (dest + done));
                if (n > size - done) n = size - done;
                if (!NtWrite(process, (void*)(dest + done), p + done, n)) return false;
                done += n;
                placed = true;
                break;
            }
        }
        if (!placed) return false;
    }
    return true;
}

static bool StageImage(HANDLE process, const std::vector<uint8_t>& image,
                       uint64_t& remoteBase, std::vector<MapChunk>& chunks) {
    auto* dos = (IMAGE_DOS_HEADER*)image.data();
    auto* nt  = (IMAGE_NT_HEADERS64*)(image.data() + dos->e_lfanew);
    SIZE_T need = AlignUp(nt->OptionalHeader.SizeOfImage, 0x10000);
    SIZE_T nChunks = need / 0x10000;

    
    
    
    
    char sysDir[MAX_PATH]{};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    char sysPattern[MAX_PATH]{};
    lstrcpyA(sysPattern, sysDir);
    lstrcatA(sysPattern, "\\*.dll");
    std::vector<std::string> victims;
    WIN32_FIND_DATAA vfd{};
    HANDLE vh = FindFirstFileA(sysPattern, &vfd);
    if (vh != INVALID_HANDLE_VALUE) {
        do {
            if ((vfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !vfd.cFileName[0])
                continue;
            if (victims.size() >= 128) break;
            char cand[MAX_PATH]{};
            lstrcpyA(cand, sysDir);
            lstrcatA(cand, "\\");
            lstrcatA(cand, vfd.cFileName);
            HANDLE ch = CreateFileA(cand, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
            if (ch == INVALID_HANDLE_VALUE) continue;
            IMAGE_DOS_HEADER dh{};
            DWORD rr = 0;
            bool ok = ReadFile(ch, &dh, sizeof(dh), &rr, nullptr) && rr == sizeof(dh) &&
                      dh.e_magic == IMAGE_DOS_SIGNATURE;
            IMAGE_NT_HEADERS64 nh{};
            if (ok) {
                LARGE_INTEGER off{};
                off.QuadPart = dh.e_lfanew;
                ok = SetFilePointerEx(ch, off, nullptr, FILE_BEGIN) &&
                     ReadFile(ch, &nh, sizeof(nh), &rr, nullptr) && rr == sizeof(nh) &&
                     nh.Signature == IMAGE_NT_SIGNATURE &&
                     nh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                     nh.OptionalHeader.SizeOfImage >= 0x10000;
            }
            CloseHandle(ch);
            if (ok) victims.push_back(vfd.cFileName);
        } while (FindNextFileA(vh, &vfd));
        FindClose(vh);
    }
    std::string victim = std::string(sysDir) + "\\kernel32.dll";
    if (!victims.empty())
        victim = std::string(sysDir) + "\\" + victims[rand() % victims.size()];
    if (victims.empty())
        dbgwin::log("no usable system32 host dlls found - falling back to kernel32.dll");

    char tmpFile[MAX_PATH]{};
    GetTempPathA(MAX_PATH, sysDir); 
    if (!GetTempFileNameA(sysDir, "BYF", 0, tmpFile)) {
        dbgwin::log("[FAIL] GetTempFileNameA (GetLastError=%lu)", GetLastError());
        return false;
    }
    if (!CopyFileA(victim.c_str(), tmpFile, FALSE)) {
        dbgwin::log("[FAIL] CopyFileA(%s) (GetLastError=%lu)", victim.c_str(), GetLastError());
        DeleteFileA(tmpFile);
        return false;
    }

    HANDLE hFile = CreateFileA(tmpFile, GENERIC_READ | GENERIC_EXECUTE,
                               FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        dbgwin::log("[FAIL] CreateFileA(staging temp) (GetLastError=%lu)", GetLastError());
        DeleteFileA(tmpFile);
        return false;
    }
    
    

    
    uint64_t base = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t* addr = (uint8_t*)0x10000;
         VirtualQueryEx(process, addr, &mbi, sizeof(mbi));
         addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize) {
        if (mbi.State != MEM_FREE) continue;
        uint64_t aligned = AlignUp((uint64_t)mbi.BaseAddress, 0x10000);
        if (mbi.RegionSize >= need && aligned &&
            aligned + need <= (uint64_t)mbi.BaseAddress + mbi.RegionSize) {
            base = aligned;
            break;
        }
    }
    if (!base) {
        dbgwin::log("[FAIL] no free 64k-aligned hole for %llu bytes", (unsigned long long)need);
        CloseHandle(hFile);
        DeleteFileA(tmpFile);
        return false;
    }

    std::vector<uint8_t> zeros(0x10000, 0);

    
    std::vector<HANDLE> sections;
    sections.reserve(nChunks);
    bool stageOk = true;
    for (SIZE_T i = 0; i < nChunks; ++i) {
        HANDLE sec = nullptr;
        NTSTATUS s = pNtCreateSection(&sec, SECTION_ALL_ACCESS, nullptr, nullptr,
                                      PAGE_READONLY, 0x1000000 , hFile);
        if (s < 0 || !sec) {
            dbgwin::log("[FAIL] NtCreateSection(chunk %d/%d) (NTSTATUS=0x%08lX)",
                        (int)i, (int)nChunks, (unsigned long)s);
            stageOk = false;
            break;
        }
        sections.push_back(sec);
    }
    CloseHandle(hFile);
    if (!stageOk || sections.size() != nChunks) {
        for (HANDLE h : sections) CloseHandle(h);
        DeleteFileA(tmpFile);
        return false;
    }

    
    for (SIZE_T i = 0; i < nChunks; ++i) {
        HANDLE sec = sections[i];
        PVOID target = (PVOID)(base + i * 0x10000);
        SIZE_T viewSize = 0x10000;
        NTSTATUS s = pNtMapViewOfSection(sec, process, &target, 0, 0, nullptr, &viewSize,
                                         2 , 0, PAGE_EXECUTE_READ);
        CloseHandle(sec);
        if (s < 0 || (uint64_t)target != base + i * 0x10000) {
            dbgwin::log("[FAIL] NtMapViewOfSection(chunk %d) (NTSTATUS=0x%08lX, target=0x%p)",
                        (int)i, (unsigned long)s, target);
            return false;
        }
        DWORD op = 0;
        VirtualProtectEx(process, target, 0x10000, PAGE_EXECUTE_READWRITE, &op);
    
    
        if (!NtWrite(process, target, zeros.data(), zeros.size())) {
            dbgwin::log("[FAIL] zeroing view %d (NTSTATUS=0x%08lX)", (int)i, g_lastNtStatus);
            return false;
        }
        chunks.push_back({ (uint64_t)target, 0x10000 });
    }

    if (!ChunkedWrite(process, chunks, base, image.data(), nt->OptionalHeader.SizeOfHeaders)) {
        dbgwin::log("[FAIL] headers write (NTSTATUS=0x%08lX)", g_lastNtStatus);
        return false;
    }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!sec->SizeOfRawData) continue;
        if (!ChunkedWrite(process, chunks, base + sec->VirtualAddress,
                          image.data() + sec->PointerToRawData, sec->SizeOfRawData)) {
            dbgwin::log("[FAIL] section %d write (NTSTATUS=0x%08lX)", (int)i, g_lastNtStatus);
            return false;
        }
    }
    FlushInstructionCache(process, (void*)base, need);
    
    
    HANDLE tx = CreateTransaction(nullptr, nullptr, 0, 0, 0, 0, nullptr);
    if (tx && tx != INVALID_HANDLE_VALUE) {
        DeleteFileTransactedA(tmpFile, tx);
        CommitTransaction(tx);
        CloseHandle(tx);
    }
    DeleteFileA(tmpFile); 
    dbgwin::log("staged: victim=%s base=0x%llX chunks=%d", victim.c_str(),
                (unsigned long long)base, (int)chunks.size());
    remoteBase = base;
    return true;
}












static const uint64_t kFakeDmCandidates[] = { 0x8DC8000, 0x8B54980, 0x8EE1728, 0x8E42C98, 0x8DC2258, 0x8D22868 }; 




static const uint64_t kFdmDmAdd   = 0x1F8;  


static const uint64_t kGameLoadedCands[] = {
    0x5D0, 0x5D8, 0x5E0, 0x5E8, 0x5F0, 0x5F8, 0x600, 0x608,
    0x610, 0x618, 0x620, 0x628, 0x630, 0x638, 0x640, 0x648,
    0x650, 0x658, 0x660, 0x668, 0x670, 0x680, 0x690, 0x6A0,
    0x6B0, 0x6C0, 0x6D0, 0x6E0, 0x6F0, 0x700, 0x710, 0x720
};



static bool waitForGameLoaded(HANDLE process, DWORD pid) {
    uint64_t base = 0;
    uint64_t imgSize = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            base = (uint64_t)me.modBaseAddr; 
            imgSize = me.modBaseSize;
        }
        CloseHandle(snap);
    }
    if (!base) {
        dbgwin::log("[FAIL] could not read Roblox exe base (module snapshot)");
        return false;
    }
    
    
    
    
    
    auto inImage = [&](uint64_t p) {
        return imgSize && p >= base && p < base + imgSize;
    };
    dbgwin::log("exe base 0x%llX size 0x%llX; probing fakeDM RVA 0x%llX then fallback 0x%llX",
                (unsigned long long)base, (unsigned long long)imgSize,
                (unsigned long long)kFakeDmCandidates[0],
                (unsigned long long)kFakeDmCandidates[1]);

    
    
    
    
    const int nCand = (int)(sizeof(kFakeDmCandidates) / sizeof(kFakeDmCandidates[0]));
    uint64_t steadyDm = 0, lastFake = 0, lastDm = 0;
    uint32_t lastLoaded = 0;
    int same = 0, matched = -1, bogus = 0;
    for (int i = 0; i < 480; ++i) {
        bool anyChain = false;
        bogus = 0;
        for (int c = 0; c < nCand; ++c) {
            uint64_t fake = 0, dm = 0;
            SIZE_T r = 0;
            if (!ReadProcessMemory(process, (void*)(base + kFakeDmCandidates[c]), &fake, 8, &r) ||
                r != 8 || fake <= 0x10000 || (fake & 7) != 0)
                continue;
            if (inImage(fake)) { bogus = 1; continue; } 
            
            if (!ReadProcessMemory(process, (void*)(fake + kFdmDmAdd), &dm, 8, &r) ||
                r != 8 || dm <= 0x10000 || (dm & 7) != 0)
                continue;
            if (inImage(dm)) { bogus = 2; continue; }   
            anyChain = true;
            lastFake = fake;
            lastDm = dm;
            lastLoaded = 0;
            for (uint64_t glo : kGameLoadedCands) {
                uint32_t v = 0;
                if (ReadProcessMemory(process, (void*)(dm + glo), &v, 4, &r) && r == 4) {
                    lastLoaded = v;
                    if (v == 31) break;
                }
            }
            if (lastLoaded != 31) continue; 
            if (matched != c) {
                dbgwin::log("fakeDM RVA 0x%llX responded (fake=0x%llX dm=0x%llX sentinel=31)",
                            (unsigned long long)kFakeDmCandidates[c],
                            (unsigned long long)fake, (unsigned long long)dm);
                matched = c;
            }
            if (dm == steadyDm) {
                if (++same >= 3) return true;
            } else {
                steadyDm = dm;
                same = 1;
            }
            break; 
        }
        if (!anyChain && matched >= 0)
            dbgwin::log("warning: fakeDM RVA 0x%llX went silent again",
                        (unsigned long long)kFakeDmCandidates[matched]);
        DWORD ec = 0;
        if (GetExitCodeProcess(process, &ec) && ec != STILL_ACTIVE) {
            dbgwin::log("Roblox exited while waiting for game load (exit code 0x%lX)", ec);
            return false;
        }
        if ((i % 40) == 39)
            dbgwin::log("waiting for game load (%ds): chain=%s fake=0x%llX dm=0x%llX sentinel=%u (need 31)%s",
                        (i + 1) / 4, anyChain ? "alive" : (bogus ? "bogus" : "dead"),
                        (unsigned long long)lastFake, (unsigned long long)lastDm,
                        (unsigned)lastLoaded,
                        bogus ? " [ptr inside exe image = pre-place placeholder]" : "");
        dbgwin::Pump();
        Sleep(250);
    }
    dbgwin::log("[FAIL] game load never confirmed in 120s (last: fake=0x%llX dm=0x%llX sentinel=%u).",
                (unsigned long long)lastFake, (unsigned long long)lastDm, (unsigned)lastLoaded);
    dbgwin::log("       if the chain stayed dead, this client build moved the fakeDM global -");
    dbgwin::log("       add its RVA to kFakeDmCandidates[] above waitForGameLoaded().");

    
    if (lastDm) {
        dbgwin::log("scanning dm+0x400..0x800 for sentinel 31...");
        for (uint64_t off = 0x400; off <= 0x800; off += 4) {
            uint32_t v = 0; SIZE_T rr = 0;
            if (ReadProcessMemory(process, (void*)(lastDm + off), &v, 4, &rr) && rr == 4 && v == 31)
                dbgwin::log("  HIT GameLoaded candidate @ dm+0x%llX (==31)", (unsigned long long)off);
        }
    }

    return false;
}

static int RunInject() {
    srand(GetCurrentProcessId() ^ (unsigned)__rdtsc() ^ GetTickCount());

    SweepStaleStagingTemps();
    dbgwin::log("stale staging temps swept");

    if (!ResolveNtdll()) {
        return Fail("ResolveNtdll - ntdll exports missing");
    }
    dbgwin::log("ntdll exports resolved");

    if (EnableDebugPrivilege()) dbgwin::log("SeDebugPrivilege enabled");
    else                        dbgwin::log("SeDebugPrivilege not enabled (not admin?) - continuing");

    
    
    
    DWORD pid = 0;
    for (int i = 0; i < 240 && !pid; ++i) {
        pid = FindRobloxProcess();
        if (!pid) {
            if (i == 0)              dbgwin::log("waiting for RobloxPlayerBeta (%d/240)...", i + 1);
            else if ((i % 20) == 19) dbgwin::log("still waiting for RobloxPlayerBeta (%d/240)...", i + 1);
            dbgwin::Pump();
            Sleep(500);
        }
    }
    if (!pid) {
        return Fail("RobloxPlayerBeta not found in 120s (client not running?)");
    }
    dbgwin::log("RobloxPlayerBeta found, PID %lu", (unsigned long)pid);

    HANDLE process = OpenProcess(0x1FFFFF, FALSE, pid);
    if (process) dbgwin::log("OpenProcess full access OK");
    else {
        DWORD gle = GetLastError();
        dbgwin::log("OpenProcess(0x1FFFFF) failed (GetLastError=%lu), retrying with limited rights", gle);
        process = OpenProcess(0x478, FALSE, pid);
        if (process) dbgwin::log("OpenProcess limited rights OK (0x478)");
    }
    if (!process) {
        return Fail("OpenProcess", GetLastError());
    }

    if (!waitForGameLoaded(process, pid)) {
        DWORD ec = 0;
        if (GetExitCodeProcess(process, &ec) && ec != STILL_ACTIVE) {
            CloseHandle(process);
            return Fail("Roblox exited before game load");
        }
        dbgwin::log("game load NOT confirmed - injecting anyway (watch the client)");
    }

    
    SuspendAllThreads(pid);
    dbgwin::log("suspended %d thread(s) for staging", (int)g_suspended.size());

    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (slash) *(slash + 1) = 0;

    char modName[32];
    Dec(enc_Module.data, sizeof(enc_Module.data) - 1, modName);
    std::string dllPath = std::string(exePath) + modName;
    dbgwin::log("payload: %s", dllPath.c_str());

    HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CloseHandle(process);
        return Fail("CreateFileA(viridian.dll) - is the dll next to the exe?", GetLastError());
    }
    LARGE_INTEGER fileSize{};
    GetFileSizeEx(hFile, &fileSize);
    std::vector<uint8_t> image((size_t)fileSize.QuadPart);
    DWORD bytesRead = 0;
    ReadFile(hFile, image.data(), (DWORD)image.size(), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (bytesRead != image.size()) {
        CloseHandle(process);
        return Fail("ReadFile(viridian.dll) short read");
    }
    dbgwin::log("payload read: %zu bytes", image.size());

    auto* dos = (IMAGE_DOS_HEADER*)image.data();
    auto* nt  = (IMAGE_NT_HEADERS64*)(image.data() + dos->e_lfanew);
    if (image.size() < sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS64) ||
        dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(process);
        return Fail("payload is not a valid PE image");
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        CloseHandle(process);
        return Fail("payload is not x64 (the client is x64)");
    }

    uint64_t remoteBase = 0;
    std::vector<MapChunk> chunks;
    if (!StageImage(process, image, remoteBase, chunks)) {
        ResumeAllThreads();
        CloseHandle(process);
        return Fail("StageImage - manual map failed (see the lines above)");
    }
    ResumeAllThreads();

    DWORD exportRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    DWORD exportSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    uint64_t runServerAddr = 0;
    if (exportRVA && exportSize) {
        auto RvaToOffset = [&](DWORD rva) -> DWORD {
            auto* s = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
                DWORD ss = (s->Misc.VirtualSize > s->SizeOfRawData) ? s->Misc.VirtualSize : s->SizeOfRawData;
                if (rva >= s->VirtualAddress && rva < s->VirtualAddress + ss)
                    return s->PointerToRawData + (rva - s->VirtualAddress);
            }
            return 0;
        };
        auto* expDir = (IMAGE_EXPORT_DIRECTORY*)(image.data() + RvaToOffset(exportRVA));
        DWORD* nameRVAs = (DWORD*)(image.data() + RvaToOffset(expDir->AddressOfNames));
        WORD* ordinals  = (WORD*)(image.data() + RvaToOffset(expDir->AddressOfNameOrdinals));
        DWORD* funcRVAs = (DWORD*)(image.data() + RvaToOffset(expDir->AddressOfFunctions));
        for (DWORD i = 0; i < expDir->NumberOfNames; ++i) {
            const char* nm = (const char*)(image.data() + RvaToOffset(nameRVAs[i]));
            if (_stricmp(nm, "RunServer") == 0) {
                runServerAddr = remoteBase + funcRVAs[ordinals[i]];
                break;
            }
        }
    }

    if (runServerAddr) dbgwin::log("RunServer export @ 0x%llX", (unsigned long long)runServerAddr);
    else               dbgwin::log("payload exports no RunServer - skipping the second fire");

    
    
    
    image.clear();
    image.shrink_to_fit();
    nt = nullptr;
    dos = nullptr;

    uint64_t k32 = RemoteGetModule(process, "kernel32.dll");
    if (!k32) {
        CloseHandle(process);
        return Fail("RemoteGetModule(kernel32.dll) - remote PEB walk");
    }
    dbgwin::log("remote kernel32 @ 0x%llX", (unsigned long long)k32);
    void* loadLibraryA   = (void*)RemoteGetProc(process, k32, "LoadLibraryA");
    void* getProcAddress = (void*)RemoteGetProc(process, k32, "GetProcAddress");
    if (!loadLibraryA || !getProcAddress) {
        CloseHandle(process);
        return Fail("RemoteGetProc(LoadLibraryA/GetProcAddress)");
    }
    dbgwin::log("remote LoadLibraryA @ 0x%p, GetProcAddress @ 0x%p", loadLibraryA, getProcAddress);

    HANDLE stubSection = nullptr;
    LARGE_INTEGER stubSize{};
    stubSize.QuadPart = 0x1020;
    NTSTATUS s = pNtCreateSection(&stubSection, SECTION_ALL_ACCESS, nullptr, &stubSize,
                                  PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr);
    if (s < 0 || !stubSection) {
        CloseHandle(process);
        return Fail("NtCreateSection(loader stub)", 0, (unsigned long)s);
    }
    PVOID stubBase = nullptr;
    SIZE_T stubViewSize = 0;
    s = pNtMapViewOfSection(stubSection, process, &stubBase, 0, 0, nullptr, &stubViewSize,
                            2, 0, PAGE_EXECUTE_READWRITE);
    CloseHandle(stubSection);
    if (s < 0 || !stubBase) {
        CloseHandle(process);
        return Fail("NtMapViewOfSection(loader stub)", 0, (unsigned long)s);
    }
    uint64_t stubBaseAddr = (uint64_t)stubBase;
    uint64_t contextAddr  = stubBaseAddr + 0x1000;
    dbgwin::log("loader stub mapped @ 0x%p, context @ 0x%llX", stubBase,
                (unsigned long long)contextAddr);

    if (!NtWrite(process, stubBase, loader_stub, sizeof(loader_stub))) {
        return Fail("write loader stub", 0, g_lastNtStatus);
    }

    LoaderContext ctx{};
    ctx.imageBase       = remoteBase;
    ctx.fLoadLibraryA   = loadLibraryA;
    ctx.fGetProcAddress = getProcAddress;
    ctx.status          = 0;
    if (!NtWrite(process, (void*)contextAddr, &ctx, sizeof(ctx))) {
        return Fail("write loader context", 0, g_lastNtStatus);
    }

    HANDLE trampSection = nullptr;
    LARGE_INTEGER trampSize{};
    trampSize.QuadPart = 0x100;
    s = pNtCreateSection(&trampSection, SECTION_ALL_ACCESS, nullptr, &trampSize,
                         PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr);
    if (s < 0 || !trampSection) {
        pNtUnmapViewOfSection(process, stubBase); 
        CloseHandle(process);
        return Fail("NtCreateSection(trampoline)", 0, (unsigned long)s);
    }
    PVOID trampBase = nullptr;
    SIZE_T trampViewSize = 0;
    s = pNtMapViewOfSection(trampSection, process, &trampBase, 0, 0, nullptr, &trampViewSize,
                            2, 0, PAGE_EXECUTE_READWRITE);
    CloseHandle(trampSection);
    if (s < 0 || !trampBase) {
        pNtUnmapViewOfSection(process, stubBase);
        CloseHandle(process);
        return Fail("NtMapViewOfSection(trampoline)", 0, (unsigned long)s);
    }

    uint8_t trampoline[22] = {
        0x48, 0xB8, 0,0,0,0,0,0,0,0,
        0x48, 0xB9, 0,0,0,0,0,0,0,0,
        0xFF, 0xE0
    };
    memcpy(&trampoline[2], &stubBaseAddr, 8);
    memcpy(&trampoline[12], &contextAddr, 8);
    if (!NtWrite(process, trampBase, trampoline, sizeof(trampoline))) {
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("write trampoline", 0, g_lastNtStatus);
    }

    dbgwin::log("scanning remote handle table for the TP completion object...");
    HANDLE iocp = FindIoCompletion(process);
    if (!iocp) {
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("FindIoCompletion - no TP_WAIT completion object in the handle table (client pool changed?)");
    }
    dbgwin::log("completion object found (handle 0x%p)", iocp);

    void* tpDirect = FindZeroedTpSlot(process);
    if (!tpDirect) {
        tpDirect = VirtualAllocEx(process, nullptr, 0x48, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (tpDirect) dbgwin::log("no zeroed tp slot found, allocated one @ 0x%p", tpDirect);
    } else {
        dbgwin::log("zeroed tp slot found @ 0x%p", tpDirect);
    }
    if (!tpDirect) {
        CloseHandle(iocp);
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("worker packet slot (FindZeroedTpSlot/VirtualAllocEx)", GetLastError());
    }

    
    uint8_t packet[0x48]{};
    memcpy(packet + 0x38, &trampBase, sizeof(trampBase));
    if (!NtWrite(process, tpDirect, packet, sizeof(packet))) {
        CloseHandle(iocp);
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("write tp packet", 0, g_lastNtStatus);
    }

    
    NTSTATUS qStatus = pNtSetIoCompletion(iocp, tpDirect, nullptr, 0, 0);
    CloseHandle(iocp);
    if (qStatus < 0) {
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("NtSetIoCompletion", 0, (unsigned long)qStatus);
    }
    dbgwin::log("completion packet queued - a pool worker should pick it up");

    
    
    
    
    bool stubDone = false;
    unsigned long long lastStatus = (unsigned long long)-1;
    for (int i = 0; i < 3000; ++i) {
        Sleep(10);
        dbgwin::Pump();
        LoaderContext result{};
        if (ReadProcessMemory(process, (void*)contextAddr, &result, sizeof(result), nullptr)) {
            unsigned long long stNow = result.status;
            if (stNow != lastStatus) {
                lastStatus = stNow;
                dbgwin::log("loader stub status -> %llu", lastStatus);
            }
            if (stNow == 5) { stubDone = true; break; }
        }
        DWORD ec = 0;
        if (GetExitCodeProcess(process, &ec) && ec != STILL_ACTIVE) {
            dbgwin::log("Roblox died while the loader stub was running (exit code 0x%lX)", ec);
            break;
        }
    }

    if (!stubDone) {
        if (lastStatus == (unsigned long long)-1)
            dbgwin::log("loader stub status never readable / packet never picked up");
        else
            dbgwin::log("loader stub stuck at status %llu (0=packet untouched, 1=started, 2..4=init phases, 5=done)",
                        lastStatus);
        pNtUnmapViewOfSection(process, stubBase);
        pNtUnmapViewOfSection(process, trampBase);
        CloseHandle(process);
        return Fail("loader stub did not finish");
    }

    
    
    
    

    pNtUnmapViewOfSection(process, stubBase);
    pNtUnmapViewOfSection(process, trampBase);
    dbgwin::log("loader stub + trampoline unmapped");

    
    
    

    
    
    

    if (false)
    {
    auto* prot = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++prot) {
        bool exec  = prot->Characteristics & IMAGE_SCN_MEM_EXECUTE;
        bool read  = prot->Characteristics & IMAGE_SCN_MEM_READ;
        bool write = prot->Characteristics & IMAGE_SCN_MEM_WRITE;
        DWORD protect = PAGE_NOACCESS;
        if (exec && write) protect = PAGE_EXECUTE_READWRITE;
        else if (exec && read) protect = PAGE_EXECUTE_READ;
        else if (exec) protect = PAGE_EXECUTE;
        else if (write) protect = PAGE_READWRITE;
        else if (read) protect = PAGE_READONLY;
        DWORD op = 0;
        VirtualProtectEx(process, (void*)(remoteBase + prot->VirtualAddress),
            AlignUp(prot->Misc.VirtualSize ? prot->Misc.VirtualSize : prot->SizeOfRawData,
                    nt->OptionalHeader.SectionAlignment),
            protect, &op);
    }
    }

    if (runServerAddr) {
    
    
        dbgwin::log("firing RunServer @ 0x%llX", (unsigned long long)runServerAddr);
        uint8_t runStub[] = {
            0x48, 0xB8, 0,0,0,0,0,0,0,0,
            0x48, 0x31, 0xC9,
            0xFF, 0xD0,
            0xC3
        };
        memcpy(&runStub[2], &runServerAddr, 8);

        void* runStubBase = VirtualAllocEx(process, nullptr, sizeof(runStub),
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!runStubBase) {
            CloseHandle(process);
            return Fail("VirtualAllocEx(run stub)", GetLastError());
        }
        if (!NtWrite(process, runStubBase, runStub, sizeof(runStub))) {
            VirtualFreeEx(process, runStubBase, 0, MEM_RELEASE);
            CloseHandle(process);
            return Fail("write run stub", 0, g_lastNtStatus);
        }

        HANDLE iocp2 = FindIoCompletion(process);
        if (iocp2) {
            void* tp2 = FindZeroedTpSlot(process);
            if (!tp2)
                tp2 = VirtualAllocEx(process, nullptr, 0x48, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!tp2) {
                CloseHandle(iocp2);
                VirtualFreeEx(process, runStubBase, 0, MEM_RELEASE);
                CloseHandle(process);
                return Fail("RunServer worker packet slot", GetLastError());
            }
            uint8_t packet2[0x48]{};
            memcpy(packet2 + 0x38, &runStubBase, sizeof(runStubBase));
            NtWrite(process, tp2, packet2, sizeof(packet2));
            pNtSetIoCompletion(iocp2, tp2, nullptr, 0, 0);
            CloseHandle(iocp2);
            dbgwin::log("RunServer queued");
        } else {
            dbgwin::log("no completion object for the second fire - payload mapped but RunServer NOT fired");
        }

        
        
        
        DWORD wr = WaitForSingleObject(process, 3000);
        if (wr == WAIT_OBJECT_0) {
            DWORD ec = 0;
            GetExitCodeProcess(process, &ec);
            char buf[128];
            snprintf(buf, sizeof(buf), "Roblox crashed after RunServer (exit code 0x%lX -> payload init)", ec);
            CloseHandle(process);
            return Fail(buf);
        }
        dbgwin::log("RunServer fired, client stayed up 3s");
    }

    
    CloseHandle(process);
    return 0;
}

int main() {
    
    dbgwin::log("Viridian injector starting");
    int rc = RunInject();
    if (rc == 0) {
        dbgwin::log("INJECTION COMPLETE");
        dbgwin::DiscardPending(); 
        return 0;
    }
    dbgwin::log("injection failed - opening the debug window (full trace is also in injector.log)");
    dbgwin::Open(); 
    dbgwin::WaitClose(-1);
    return rc;
}

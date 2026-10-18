#pragma once
// ViridianRing — named shared-memory status ring the UI polls at 100Hz.
//
// EXACT contract (little-endian, all integers unsigned):
//   name "Local\WNF_STATE_RBX_01", 16384 bytes, PAGE_READWRITE file mapping.
//   offset  0: u32 magic      = 0x56495244 ("VIRD")
//   offset  4: u32 version    = 1
//   offset  8: u64 seq        (incremented on EVERY update, bumped LAST via interlocked increment)
//   offset 16: u32 status     (0=idle, 1=injecting, 2=ready, 3=executing, 4=error)
//   offset 20: u32 exec_count (once per executed script, process-wide, monotonic across teleports)
//   offset 24: u32 last_exec_ok    (1/0 for the most recent execution)
//   offset 28: u32 last_exec_ms    (wall-clock DURATION in ms of the most recent execution)
//   offset 32: u32 last_exec_bytes (source size of the most recent execution)
//   offset 36: u32 dm_lo      (low 32 bits of the current DataModel pointer; 0 when none)
//   offset 40: u32 log_len    (bytes used in the tail area, max 8128)
//   offset 44: u8[8128] latest boot.log tail bytes (newest bytes win when over cap)
//   --- request slot (same mapping, total still 16384 bytes) ---
//   offset  8172: u64 req_seq  (UI-owned; UI bumps once per request, LAST after
//                 data/len/id are filled; 0 = no request ever posted)
//   offset  8180: u32 req_id   (UI-owned; UI-assigned id for this request)
//   offset  8184: u32 req_len  (UI-owned; bytes of Lua source that follow, valid 1..7168)
//   offset  8188: u8[7168] req_data (UI-owned; UTF-8 Lua source)
//   offset 15356: u32 last_req_id (DLL-owned; req_id the DLL just FINISHED executing; 0 initially)
//   bytes 15360..16383 remain spare.
//
// Ownership: the UI is the ONLY writer of req_seq/req_id/req_len/req_data;
// the DLL NEVER writes offsets 8172..15356. The DLL is the ONLY writer of
// last_req_id (and of everything else, as before).
// Protocol: UI fills data, then len, then id, then bumps req_seq LAST.
// DLL side: polled ONCE per scheduler tick at the top of ExecuteGameImpl
// (the WaitingHybridScriptsJob step tick that also drains the execute.lua
// queue — see PollRequestSlot). Seqlock-style copy-out, invalid lengths are
// consumed (not retried) so a poisoned slot can never spin forever, and a
// valid request is queued through task_scheduler::RequestExecution — the
// SAME function the execute.lua watcher calls — so status transitions,
// exec_count/last_exec_* and boot.log markers are identical on both paths.
// The execute.lua file watcher is untouched and remains as fallback.
//
// Writer rules honored here:
//  - Fill a local struct, memcpy it into the view, bump seq LAST (InterlockedIncrement64).
//  - All fills guarded by a dedicated CRITICAL_SECTION owned by this file.
//    Never touches ConsoleMutex / scheduler / execution locks, and the lock is
//    never held across blocking calls (file I/O + CreateFileMapping happen outside it).
//  - If the mapping cannot be created: log once to boot.log, keep running without
//    the ring, retry at most once per distinct DataModel init (never in a hot loop).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <string>

namespace ViridianRing
{
inline constexpr uint32_t kMapSize = 16384u;
inline constexpr uint32_t kMagic = 0x56495244u; // "VIRD" in little-endian
inline constexpr uint32_t kVersion = 1u;
inline constexpr uint32_t kTailCap = 8128u;
inline constexpr uint32_t kTailOff = 44u;
inline constexpr uint32_t kUsed = kTailOff + kTailCap; // 8172 bytes published; rest is request slot + echo + spare

// Request slot offsets (byte-exact, little-endian).
inline constexpr uint32_t kReqSeqOff = 8172u;   // u64 req_seq   (UI-owned)
inline constexpr uint32_t kReqIdOff = 8180u;    // u32 req_id    (UI-owned)
inline constexpr uint32_t kReqLenOff = 8184u;   // u32 req_len   (UI-owned)
inline constexpr uint32_t kReqDataOff = 8188u;  // u8[7168] req_data (UI-owned)
inline constexpr uint32_t kReqCap = 7168u;
inline constexpr uint32_t kLastReqIdOff = 15356u; // u32 last_req_id (DLL-owned echo)
inline constexpr uint32_t kSpareOff = 15360u;     // spare bytes run to the end of the mapping

inline constexpr uint32_t kStatusIdle = 0u;
inline constexpr uint32_t kStatusInjecting = 1u;
inline constexpr uint32_t kStatusReady = 2u;
inline constexpr uint32_t kStatusExecuting = 3u;
inline constexpr uint32_t kStatusError = 4u;

// Byte-exact published layout. Default packing already yields the contract
// offsets (verified by static_assert below); do not add pack pragmas.
struct Layout
{
    uint32_t Magic;       // +0
    uint32_t Version;     // +4
    uint64_t Seq;         // +8
    uint32_t Status;      // +16
    uint32_t ExecCount;   // +20
    uint32_t LastExecOk;  // +24
    uint32_t LastExecMs;  // +28
    uint32_t LastExecBytes; // +32
    uint32_t DmLo;        // +36
    uint32_t LogLen;      // +40
    uint8_t Tail[kTailCap]; // +44
    // --- UI-owned request slot (+8172..+15356). The DLL NEVER writes here. ---
    // req_seq is a u64 at a 4-aligned offset, so it is stored as a
    // little-endian lo/hi u32 pair (byte-identical to a u64 at +8172) to keep
    // default packing exact without pack pragmas.
    uint32_t ReqSeqLo;        // +8172 (low half of req_seq)
    uint32_t ReqSeqHi;        // +8176 (high half of req_seq)
    uint32_t ReqId;           // +8180
    uint32_t ReqLen;          // +8184
    uint8_t ReqData[kReqCap]; // +8188
    // --- DLL-owned completion echo ---
    uint32_t LastReqId;                 // +15356
    uint8_t Spare[kMapSize - kSpareOff]; // +15360..16383
};

static_assert(offsetof(Layout, Magic) == 0);
static_assert(offsetof(Layout, Version) == 4);
static_assert(offsetof(Layout, Seq) == 8);
static_assert(offsetof(Layout, Status) == 16);
static_assert(offsetof(Layout, ExecCount) == 20);
static_assert(offsetof(Layout, LastExecOk) == 24);
static_assert(offsetof(Layout, LastExecMs) == 28);
static_assert(offsetof(Layout, LastExecBytes) == 32);
static_assert(offsetof(Layout, DmLo) == 36);
static_assert(offsetof(Layout, LogLen) == 40);
static_assert(offsetof(Layout, Tail) == 44);
static_assert(offsetof(Layout, ReqSeqLo) == 8172);
static_assert(offsetof(Layout, ReqSeqLo) == kReqSeqOff);
static_assert(offsetof(Layout, ReqSeqHi) == 8176);
static_assert(offsetof(Layout, ReqId) == 8180);
static_assert(offsetof(Layout, ReqId) == kReqIdOff);
static_assert(offsetof(Layout, ReqLen) == 8184);
static_assert(offsetof(Layout, ReqLen) == kReqLenOff);
static_assert(offsetof(Layout, ReqData) == 8188);
static_assert(offsetof(Layout, ReqData) == kReqDataOff);
static_assert(kReqDataOff + kReqCap == kLastReqIdOff);
static_assert(offsetof(Layout, LastReqId) == 15356);
static_assert(offsetof(Layout, LastReqId) == kLastReqIdOff);
static_assert(offsetof(Layout, Spare) == 15360);
static_assert(sizeof(Layout) == kMapSize);
static_assert(sizeof(Layout) <= 16384);

namespace Detail
{
    // Kept for process lifetime and DELIBERATELY never unmapped/closed, not
    // even on DLL_PROCESS_DETACH: teardown-time UnmapViewOfFile/CloseHandle
    // can hang and wedge the next launch as a zombie process (a previous
    // incident left a dead game holding the single-instance lock until the
    // zombie was killed manually). The OS reclaims the mapping on process
    // death. See the DllMain detach path in entry.cpp.
    inline HANDLE s_map = nullptr;
    inline uint8_t* s_view = nullptr;
    inline CRITICAL_SECTION s_cs;       // dedicated ring lock; leaf-level, never held across blocking calls
    inline INIT_ONCE s_csOnce = INIT_ONCE_STATIC_INIT;
    inline BOOL CALLBACK InitCsCb(PINIT_ONCE, PVOID, PVOID*) { InitializeCriticalSection(&s_cs); return TRUE; }
    inline void EnsureCs() { InitOnceExecuteOnce(&s_csOnce, InitCsCb, nullptr, nullptr); }

    inline LONG s_tryEpoch = 0;         // 1 once the mapping was attempted for the current epoch
    inline uint64_t s_epochDm = 0;      // DataModel value the current epoch belongs to
    inline bool s_haveEpoch = false;
    inline LONG s_failLogged = 0;       // boot.log failure line emitted at most once

    // Shadow state. Written under s_cs; file I/O always happens outside s_cs.
    // exec counter lives here: process-wide, never reset on teleport/DataModel change.
    inline uint32_t s_status = kStatusIdle;
    inline uint32_t s_execCount = 0;
    inline uint32_t s_lastOk = 0;
    inline uint32_t s_lastMs = 0;
    inline uint32_t s_lastBytes = 0;
    inline uint32_t s_dmLo = 0;
    inline uint32_t s_logLen = 0;
    inline uint8_t s_tail[kTailCap] = {};

    // Request-slot shadow. s_seenReqSeq is DLL-local (never published):
    // the last req_seq consumed. s_lastReqId is echoed at +15356.
    inline uint64_t s_seenReqSeq = 0;
    inline uint32_t s_lastReqId = 0;

    inline void BootLogOnce(const char* msg)
    {
        char base[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char bp[MAX_PATH + 32]{};
        snprintf(bp, sizeof(bp), "%s\\Viridian\\boot.log", base);
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp, "a") != 0 || !bf)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [ring] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
        fclose(bf);
    }

    // Per-event boot.log line (unlike BootLogOnce above). Blocking file I/O:
    // callers run it OUTSIDE s_cs, per the ring pattern.
    inline void BootLogLine(const char* tag, const char* msg)
    {
        char base[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char bp[MAX_PATH + 32]{};
        snprintf(bp, sizeof(bp), "%s\\Viridian\\boot.log", base);
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp, "a") != 0 || !bf)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [%s] %s\n", st.wHour, st.wMinute, st.wSecond, tag, msg);
        fclose(bf);
    }

    inline void NoteMapFailed()
    {
        if (InterlockedCompareExchange(&s_failLogged, 1, 0) != 0)
            return;
        BootLogOnce("shared memory unavailable, continuing without ring");
    }

    // Copy shadow into the view. s_cs MUST be held. No blocking calls in here:
    // plain memcpys, then seq bumped LAST with an interlocked increment.
    inline void PublishLocked()
    {
        uint8_t* v = s_view;
        if (!v)
            return;
        struct Snap
        {
            uint32_t magic;
            uint32_t version;
            uint32_t status;
            uint32_t execCount;
            uint32_t lastOk;
            uint32_t lastMs;
            uint32_t lastBytes;
            uint32_t dmLo;
            uint32_t logLen;
            uint8_t tail[kTailCap];
            uint32_t lastReqId;
        };
        Snap s{};
        s.magic = kMagic;
        s.version = kVersion;
        s.status = s_status;
        s.execCount = s_execCount;
        s.lastOk = s_lastOk;
        s.lastMs = s_lastMs;
        s.lastBytes = s_lastBytes;
        s.dmLo = s_dmLo;
        s.logLen = s_logLen;
        memcpy(s.tail, s_tail, kTailCap);
        s.lastReqId = s_lastReqId;
        memcpy(v + 0, &s.magic, 8);                    // offsets 0..8 (magic+version)
        memcpy(v + 16, &s.status, kUsed - 16);         // offsets 16..8172 (everything after seq)
        // Offsets 8172..15356 are UI-owned: NEVER written by the DLL.
        memcpy(v + kLastReqIdOff, &s.lastReqId, 4);    // offset 15356 (completion echo)
        InterlockedIncrement64(reinterpret_cast<LONG64*>(v + 8)); // seq LAST
    }

    inline void EnsureMapping()
    {
        EnsureCs();
        EnterCriticalSection(&s_cs);
        bool have = (s_view != nullptr);
        LeaveCriticalSection(&s_cs);
        if (have)
            return;
        // At most one creation attempt per epoch (epoch = distinct DataModel init).
        if (InterlockedCompareExchange(&s_tryEpoch, 1, 0) != 0)
            return;
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMapSize, L"Local\\WNF_STATE_RBX_01");
        if (!m)
        {
            NoteMapFailed();
            return;
        }
        uint8_t* v = static_cast<uint8_t*>(MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, kMapSize));
        if (!v)
        {
            CloseHandle(m);
            NoteMapFailed();
            return;
        }
        bool installed = false;
        EnterCriticalSection(&s_cs);
        if (!s_view)
        {
            s_map = m;
            s_view = v;
            installed = true;
        }
        LeaveCriticalSection(&s_cs);
        if (!installed)
        {
            // Lost a creation race; drop the duplicate, keep the first view.
            UnmapViewOfFile(v);
            CloseHandle(m);
        }
    }

    inline void SetStatus(uint32_t st)
    {
        EnsureCs();
        EnterCriticalSection(&s_cs);
        s_status = st;
        PublishLocked();
        LeaveCriticalSection(&s_cs);
    }

    // Newest-first tail read. No locks held; caller publishes under s_cs after.
    inline void ReadTailBytes(uint8_t* out, uint32_t* outLen)
    {
        *outLen = 0;
        char base[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char bp[MAX_PATH + 32]{};
        snprintf(bp, sizeof(bp), "%s\\Viridian\\boot.log", base);
        FILE* f = nullptr;
        if (fopen_s(&f, bp, "rb") != 0 || !f)
            return;
        _fseeki64(f, 0, SEEK_END);
        LONGLONG sz = _ftelli64(f);
        if (sz > 0)
        {
            LONGLONG take = sz > static_cast<LONGLONG>(kTailCap) ? static_cast<LONGLONG>(kTailCap) : sz;
            if (_fseeki64(f, sz - take, SEEK_SET) == 0)
            {
                size_t rd = fread(out, 1, static_cast<size_t>(take), f);
                *outLen = static_cast<uint32_t>(rd);
            }
        }
        fclose(f);
    }
} // namespace Detail

inline void RefreshLogTail()
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    bool have = (Detail::s_view != nullptr);
    LeaveCriticalSection(&Detail::s_cs);
    if (!have)
        return; // no view: skip file I/O; the next successful map does a full refresh
    uint8_t buf[kTailCap]{};
    uint32_t len = 0;
    Detail::ReadTailBytes(buf, &len); // blocking file I/O OUTSIDE the lock
    if (len > kTailCap)
        len = kTailCap;
    EnterCriticalSection(&Detail::s_cs);
    memcpy(Detail::s_tail, buf, len);
    if (len < kTailCap)
        memset(Detail::s_tail + len, 0, kTailCap - len);
    Detail::s_logLen = len;
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

// Process boot: create the mapping, publish idle state + current log tail.
inline void Init()
{
    Detail::EnsureCs();
    Detail::EnsureMapping();
    uint8_t buf[kTailCap]{};
    uint32_t len = 0;
    EnterCriticalSection(&Detail::s_cs);
    bool have = (Detail::s_view != nullptr);
    LeaveCriticalSection(&Detail::s_cs);
    if (have)
        Detail::ReadTailBytes(buf, &len); // outside the lock
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusIdle;
    Detail::s_dmLo = 0;
    Detail::s_lastReqId = 0; // fresh session: no request finished yet (s_seenReqSeq
                             // stays 0 so only req_seq==0, "no request ever", is ignored)
    if (have)
    {
        if (len > kTailCap)
            len = kTailCap;
        memcpy(Detail::s_tail, buf, len);
        Detail::s_logLen = len;
    }
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

// Earliest liveness signal: publish status=injecting(1) immediately at
// process boot, BEFORE any game-object wait, so the UI sees the ring within
// milliseconds of DllMain. Layout/contract untouched (status field only).
inline void MarkInjecting()
{
    Detail::EnsureMapping();
    Detail::SetStatus(kStatusInjecting);
}

// Per-DataModel init attempt: allow one mapping retry for a NEW DataModel
// value only (same-DM ticks never retry), then mark injecting.
inline void BeginInit(uintptr_t dm)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    uint64_t want = static_cast<uint64_t>(dm);
    if (!Detail::s_haveEpoch || Detail::s_epochDm != want)
    {
        Detail::s_epochDm = want;
        Detail::s_haveEpoch = true;
        InterlockedExchange(&Detail::s_tryEpoch, 0);
    }
    LeaveCriticalSection(&Detail::s_cs);
    Detail::EnsureMapping();
    Detail::SetStatus(kStatusInjecting);
}

// Right after the existing "[init] ... OK" log.
inline void OnReady(uintptr_t dm)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusReady;
    Detail::s_dmLo = static_cast<uint32_t>(static_cast<uint64_t>(dm) & 0xFFFFFFFFull);
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

inline void OnError()
{
    Detail::SetStatus(kStatusError);
}

// Same DataModel value that gets logged in "[game] switched". Publishes the
// new dm_lo AND drops status to injecting: the old ExploitThread is dead at
// this point and nothing can run until re-init completes (OnReady). Without
// this the UI polls ready + new dm_lo while no script can execute.
// Layout/contract untouched (value transitions only: ready->injecting->ready).
inline void OnSwitch(uintptr_t newDm)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusInjecting;
    Detail::s_dmLo = static_cast<uint32_t>(static_cast<uint64_t>(newDm) & 0xFFFFFFFFull);
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

inline void OnExecBegin()
{
    Detail::SetStatus(kStatusExecuting);
}

// Completion of one executed script: exec_count+1, last_exec_* filled, back to ready.
inline void OnExecEnd(bool ok, uint32_t bytes, uint32_t ms)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_execCount += 1;
    Detail::s_lastOk = ok ? 1u : 0u;
    Detail::s_lastMs = ms;
    Detail::s_lastBytes = bytes;
    Detail::s_status = kStatusReady;
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

// Request-slot poll — the ONLY ring-request consumer.
//
// Context (ONE consistent place): called at the top of ExecuteGameImpl in
// RBX/task_scheduler/task_scheduler.cpp — the WaitingHybridScriptsJob step
// tick that also drains the execute.lua-fed RequestExecution queue. A valid
// request is queued through task_scheduler::RequestExecution(src, reqId),
// i.e. the SAME function the execute.lua watcher calls, so status
// transitions (3 while running, back to 2), exec_count/last_exec_* and the
// boot.log "=== DONE ===" marker are identical on both paths. The file
// watcher is untouched and remains as fallback.
//
// Returns true with (outSrc, outReqId) filled when a NEW valid request was
// copied out. Never blocks the tick: the copy-out is a memcpy; execution
// happens later through the normal queue, and the ring lock is never held
// across execution (or across logging/file I/O).
inline bool PollRequestSlot(std::string& outSrc, uint32_t& outReqId)
{
    Detail::EnsureCs();
    uint8_t* v = nullptr;
    uint64_t seen = 0;
    EnterCriticalSection(&Detail::s_cs);
    v = Detail::s_view;
    seen = Detail::s_seenReqSeq;
    LeaveCriticalSection(&Detail::s_cs);
    if (!v)
        return false;

    // Seqlock-style read of the UI-owned slot. s_cs is process-local and
    // cannot synchronize with the UI process, so it is NOT held here; every
    // multi-byte load is a memcpy (req_seq sits at a 4-aligned offset) and
    // req_seq is re-read after the copy to detect a concurrent UI overwrite.
    uint64_t seq1 = 0;
    memcpy(&seq1, v + kReqSeqOff, 8);
    if (seq1 == 0 || seq1 == seen)
        return false; // startup state (0 = no request ever) or already consumed
    uint32_t reqId = 0;
    uint32_t reqLen = 0;
    memcpy(&reqId, v + kReqIdOff, 4);
    memcpy(&reqLen, v + kReqLenOff, 4);
    uint32_t take = reqLen > kReqCap ? kReqCap : reqLen;
    char buf[kReqCap];
    if (take > 0)
        memcpy(buf, v + kReqDataOff, take);
    uint64_t seq2 = 0;
    memcpy(&seq2, v + kReqSeqOff, 8);
    if (seq2 != seq1)
        return false; // UI overwrote mid-copy; discard, retry next tick

    if (reqLen == 0 || reqLen > kReqCap)
    {
        // Poisoned slot: consume it (advance last-seen past this seq) so it
        // can never spin forever, and log exactly one line for this seq.
        char msg[64]{};
        snprintf(msg, sizeof(msg), "bad request (len=%u)", reqLen);
        Detail::BootLogLine("ring", msg); // file I/O outside the lock, per ring pattern
        EnterCriticalSection(&Detail::s_cs);
        Detail::s_seenReqSeq = seq1;
        LeaveCriticalSection(&Detail::s_cs);
        RefreshLogTail();
        return false;
    }

    outSrc.assign(buf, take);
    // Parity with the execute.lua watcher: strip a UTF-8 BOM if present.
    if (outSrc.size() >= 3 &&
        (unsigned char)outSrc[0] == 0xEF &&
        (unsigned char)outSrc[1] == 0xBB &&
        (unsigned char)outSrc[2] == 0xBF)
        outSrc.erase(0, 3);
    outReqId = reqId;
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_seenReqSeq = seq1;
    LeaveCriticalSection(&Detail::s_cs);
    char msg[96]{};
    snprintf(msg, sizeof(msg), "queued ring request id=%u size=%u", reqId, reqLen);
    Detail::BootLogLine("ring", msg);
    RefreshLogTail();
    return true;
}

// Completion echo for a ring request. Called by whichever queue drain
// (ExecuteGameImpl or ScriptsHandler) finishes a FromRing script, AFTER
// Execution::ExecuteScript has returned — so the ring lock is never held
// across execution, and exec_count/last_exec_* (published by OnExecEnd
// inside ExecuteScript) are already final. Publishes last_req_id = reqId.
inline void OnRequestExecuted(uint32_t reqId)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_lastReqId = reqId;
    Detail::PublishLocked();
    LeaveCriticalSection(&Detail::s_cs);
}

} // namespace ViridianRing

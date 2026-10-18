#pragma once
// Status ring: the status/completion channel served over the loopback socket
// (CMD_STATUS -> SnapshotTo). Layout is byte-identical to the ring the desktop
// application parses:
//   offset  0: u32 magic   = 0x56495244 ("VIRD")
//   offset  4: u32 version = 1
//   offset  8: u64 seq     (bumped on every publish)
//   offset 16: u32 status  (0=idle, 1=injecting, 2=ready, 3=executing, 4=error)
//   offset 20: u32 exec_count      (once per executed script)
//   offset 24: u32 last_exec_ok    (1/0 for the most recent execution)
//   offset 28: u32 last_exec_ms    (wall-clock duration of the most recent run)
//   offset 32: u32 last_exec_bytes (source size of the most recent run)
//   offset 36: u32 dm_lo           (low 32 bits of the current DataModel)
//   offset 40: u32 log_len         (bytes used in the tail area, max 8128)
//   offset 44: u8[8128] newest boot.log tail bytes
//   offset 15356: u32 last_req_id  (req id the DLL just FINISHED executing)
//   snapshot size: 15360 bytes; the UI-owned request slot bytes (8172..15356)
//   are not served over the socket and stay zero.
//
// TRANSPORT NOTE: nothing here creates a kernel object. Named mappings
// ("Local\WNF_STATE_RBX_01" and friends) were each killed on sight by the
// client; the loopback snapshot is the proven transport. Do not re-add
// CreateFileMapping anywhere in this file.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string>

namespace StatusRing
{
inline constexpr uint32_t kMapSize = 16384u;
inline constexpr uint32_t kMagic = 0x56495244u;
inline constexpr uint32_t kVersion = 1u;
inline constexpr uint32_t kTailCap = 8128u;
inline constexpr uint32_t kTailOff = 44u;
inline constexpr uint32_t kUsed = kTailOff + kTailCap;

inline constexpr uint32_t kReqSeqOff = 8172u;
inline constexpr uint32_t kReqIdOff = 8180u;
inline constexpr uint32_t kReqLenOff = 8184u;
inline constexpr uint32_t kReqDataOff = 8188u;
inline constexpr uint32_t kReqCap = 7168u;
inline constexpr uint32_t kLastReqIdOff = 15356u;
inline constexpr uint32_t kSnapshotSize = 15360u;

inline constexpr uint32_t kStatusIdle = 0u;
inline constexpr uint32_t kStatusInjecting = 1u;
inline constexpr uint32_t kStatusReady = 2u;
inline constexpr uint32_t kStatusExecuting = 3u;
inline constexpr uint32_t kStatusError = 4u;

struct Layout
{
    uint32_t Magic;
    uint32_t Version;
    uint64_t Seq;
    uint32_t Status;
    uint32_t ExecCount;
    uint32_t LastExecOk;
    uint32_t LastExecMs;
    uint32_t LastExecBytes;
    uint32_t DmLo;
    uint32_t LogLen;
    uint8_t Tail[kTailCap];
    uint32_t ReqSeqLo;
    uint32_t ReqSeqHi;
    uint32_t ReqId;
    uint32_t ReqLen;
    uint8_t ReqData[kReqCap];
    uint32_t LastReqId;
    uint8_t Spare[kMapSize - 15360u];
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
static_assert(offsetof(Layout, ReqId) == kReqIdOff);
static_assert(offsetof(Layout, ReqLen) == kReqLenOff);
static_assert(offsetof(Layout, ReqData) == kReqDataOff);
static_assert(offsetof(Layout, LastReqId) == kLastReqIdOff);
static_assert(sizeof(Layout) == kMapSize);

namespace Detail
{
    inline CRITICAL_SECTION s_cs;
    inline INIT_ONCE s_csOnce = INIT_ONCE_STATIC_INIT;
    inline BOOL CALLBACK InitCsCb(PINIT_ONCE, PVOID, PVOID*) { InitializeCriticalSection(&s_cs); return TRUE; }
    inline void EnsureCs() { InitOnceExecuteOnce(&s_csOnce, InitCsCb, nullptr, nullptr); }

    inline uint32_t s_status = kStatusIdle;
    inline uint32_t s_execCount = 0;
    inline uint32_t s_lastOk = 0;
    inline uint32_t s_lastMs = 0;
    inline uint32_t s_lastBytes = 0;
    inline uint32_t s_dmLo = 0;
    inline uint32_t s_logLen = 0;
    inline uint8_t s_tail[kTailCap] = {};
    inline uint64_t s_seq = 0;
    inline uint32_t s_lastReqId = 0;
} // namespace Detail

// Throwing-free tail read of %LOCALAPPDATA%\Viridian\boot.log (newest bytes).
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
    long long sz = _ftelli64(f);
    if (sz > 0)
    {
        long long take = sz > static_cast<long long>(kTailCap) ? static_cast<long long>(kTailCap) : sz;
        if (_fseeki64(f, sz - take, SEEK_SET) == 0)
        {
            size_t rd = fread(out, 1, static_cast<size_t>(take), f);
            *outLen = static_cast<uint32_t>(rd);
        }
    }
    fclose(f);
}

inline void RefreshLogTail()
{
    Detail::EnsureCs();
    // TLS scratch: 8KB off the tick thread's stack. Fully rewritten by
    // ReadTailBytes+memcpy below before any read.
    static thread_local uint8_t buf[kTailCap];
    uint32_t len = 0;
    ReadTailBytes(buf, &len);
    if (len > kTailCap)
        len = kTailCap;
    EnterCriticalSection(&Detail::s_cs);
    memcpy(Detail::s_tail, buf, len);
    if (len < kTailCap)
        memset(Detail::s_tail + len, 0, kTailCap - len);
    Detail::s_logLen = len;
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void Init()
{
    Detail::EnsureCs();
    // TLS scratch (see RefreshLogTail): 8KB off the stack, boot-once.
    static thread_local uint8_t buf[kTailCap];
    uint32_t len = 0;
    ReadTailBytes(buf, &len);
    if (len > kTailCap)
        len = kTailCap;
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusIdle;
    Detail::s_dmLo = 0;
    Detail::s_lastReqId = 0;
    memcpy(Detail::s_tail, buf, len);
    if (len < kTailCap)
        memset(Detail::s_tail + len, 0, kTailCap - len);
    Detail::s_logLen = len;
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void SetStatus(uint32_t st)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = st;
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void MarkInjecting()
{
    SetStatus(kStatusInjecting);
}

inline void BeginInit(uintptr_t dm)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusInjecting;
    Detail::s_dmLo = static_cast<uint32_t>(static_cast<uint64_t>(dm) & 0xFFFFFFFFull);
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void OnReady(uintptr_t dm)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_status = kStatusReady;
    Detail::s_dmLo = static_cast<uint32_t>(static_cast<uint64_t>(dm) & 0xFFFFFFFFull);
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void OnError()
{
    SetStatus(kStatusError);
}

inline void OnSwitch(uintptr_t newDm)
{
    BeginInit(newDm);
}

inline void OnExecBegin()
{
    SetStatus(kStatusExecuting);
}

inline void OnExecEnd(bool ok, uint32_t bytes, uint32_t ms)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_execCount += 1;
    Detail::s_lastOk = ok ? 1u : 0u;
    Detail::s_lastMs = ms;
    Detail::s_lastBytes = bytes;
    Detail::s_status = kStatusReady;
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

inline void OnRequestExecuted(uint32_t reqId)
{
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    Detail::s_lastReqId = reqId;
    Detail::s_seq += 1;
    LeaveCriticalSection(&Detail::s_cs);
}

// Copies the current status into `out` using the standard ring layout, so the
// consumer's parsing is byte-identical to the shared-memory era. Returns false
// if `cap` is too small.
inline bool SnapshotTo(uint8_t* out, uint32_t cap)
{
    if (!out || cap < kSnapshotSize)
        return false;
    // Zero BEFORE taking the CS: `out` is caller-private scratch, and 15KB of
    // memset under the lock would starve the game's tick thread on every poll.
    memset(out, 0, kSnapshotSize);
    Detail::EnsureCs();
    EnterCriticalSection(&Detail::s_cs);
    const uint64_t seq = Detail::s_seq;
    const uint32_t status = Detail::s_status;
    const uint32_t execCount = Detail::s_execCount;
    const uint32_t lastOk = Detail::s_lastOk;
    const uint32_t lastMs = Detail::s_lastMs;
    const uint32_t lastBytes = Detail::s_lastBytes;
    const uint32_t dmLo = Detail::s_dmLo;
    const uint32_t logLen = Detail::s_logLen;
    const uint32_t lastReqId = Detail::s_lastReqId;
    uint8_t tail[kTailCap];
    memcpy(tail, Detail::s_tail, kTailCap);
    LeaveCriticalSection(&Detail::s_cs);

    const uint32_t magic = kMagic;
    const uint32_t version = kVersion;
    memcpy(out + 0, &magic, 4);
    memcpy(out + 4, &version, 4);
    memcpy(out + 8, &seq, 8);
    memcpy(out + 16, &status, 4);
    memcpy(out + 20, &execCount, 4);
    memcpy(out + 24, &lastOk, 4);
    memcpy(out + 28, &lastMs, 4);
    memcpy(out + 32, &lastBytes, 4);
    memcpy(out + 36, &dmLo, 4);
    memcpy(out + 40, &logLen, 4);
    memcpy(out + kTailOff, tail, kTailCap);
    memcpy(out + kLastReqIdOff, &lastReqId, 4);
    return true;
}

} // namespace StatusRing

#include "Raknet.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Globals.hpp>
#include <Utilitys/Utils.hpp>
#include <RBX/update.hpp>

#include <lstate.h>
#include <lualib.h>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>


namespace Raknet
{
    bool NetReader::read(void* out, uint32_t len)
    {
        if (pos + len > n) return false;
        memcpy(out, d + pos, len);
        pos += len;
        return true;
    }

    bool NetReader::u8(uint8_t& v) { return read(&v, 1); }
    bool NetReader::u16(uint16_t& v) { return read(&v, 2); }
    bool NetReader::u32(uint32_t& v) { return read(&v, 4); }

    bool NetReader::varint(uint32_t& v)
    {
        v = 0;
        uint32_t shift = 0;
        for (int i = 0; i < 10; i++)
        {
            uint8_t b;
            if (!u8(b)) return false;
            v |= (uint32_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
            shift += 7;
        }
        return false;
    }

    const char* ItemTypeName(uint8_t id)
    {
        switch ((ItemType)id)
        {
        case ItemTypeEnd: return ("End");
        case ItemTypeDelete: return ("Delete");
        case ItemTypeNew: return ("New");
        case ItemTypeChangeProperty: return ("ChangeProperty");
        case ItemTypeMarker: return ("Marker");
        case ItemTypePing: return ("Ping");
        case ItemTypePingBack: return ("PingBack");
        case ItemTypeEventInvocation: return ("EventInvocation");
        case ItemTypeRequestCharacter: return ("RequestCharacter");
        case ItemTypeRocky: return ("Rocky");
        case ItemTypeCFrameAcknowledgement: return ("CFrameAck");
        case ItemTypeJoinData: return ("JoinData");
        case ItemTypeUpdateClientQuota: return ("UpdateClientQuota");
        case ItemTypeStreamData: return ("StreamData");
        case ItemTypeRegionRemoval: return ("RegionRemoval");
        case ItemTypeInstanceRemoval: return ("InstanceRemoval");
        case ItemTypeTag: return ("Tag");
        case ItemTypeStats: return ("Stats");
        case ItemTypeHash: return ("Hash");
        case ItemTypeResolveDeferredTree: return ("ResolveDeferredTree");
        case ItemTypeStreamDataInfo: return ("StreamDataInfo");
        case ItemTypeChangeAttribute: return ("ChangeAttribute");
        case ItemTypeJoinDataV2: return ("JoinDataV2");
        case ItemTypeStreamTerrain: return ("StreamTerrain");
        case ItemTypeStreamPrefetchInfo: return ("StreamPrefetchInfo");
        case ItemClientQoutaStreamUpdate: return ("ClientQuotaStreamUpdate");
        case ItemClientQos: return ("ClientQos");
        case ItemDeferredSharedString: return ("DeferredSharedString");
        case ItemTypeISRTimestamp: return ("ISRTimestamp");
        case ItemStreamPinData: return ("StreamPinData");
        default: return ("Unknown");
        }
    }

    std::string DecodeReplicationItem(const uint8_t* b, uint32_t n)
    {
        if (n < 2)
            return ItemTypeName(0);

        NetReader r{ b + 2, n - 2, 0 };
        char buf[256];

        switch ((ItemType)b[1])
        {
        case ItemTypeDelete:
        {
            uint32_t id = 0;
            if (r.varint(id)) { sprintf_s(buf, ("Delete obj=0x%X"), id); return buf; }
            break;
        }
        case ItemTypeMarker:
        {
            uint32_t ts = 0;
            if (r.u32(ts)) { sprintf_s(buf, ("Marker ts=%u"), ts); return buf; }
            break;
        }
        case ItemTypePing:
        case ItemTypePingBack:
        {
            uint8_t flag;
            uint64_t d;
            uint32_t first;
            if (r.u8(flag) && r.read(&d, 8) && r.u32(first)) { sprintf_s(buf, ("Ping flag=%u value=%llu"), flag, d); return buf; }
            break;
        }
        case ItemTypeEventInvocation:
        {
            uint32_t obj = 0, extra = 0;
            uint16_t ev = 0;
            if (r.varint(obj) && r.u32(extra) && r.u16(ev))
            {
                sprintf_s(buf, ("EventInvocation obj=0x%X ev=0x%X (%d args left=%u)"), obj, ev, ev, (uint32_t)(r.n - r.pos));
                return buf;
            }
            break;
        }
        case ItemTypeISRTimestamp:
        {
            uint32_t ts = 0;
            if (r.u32(ts)) { sprintf_s(buf, ("ISRTimestamp ts=%u"), ts); return buf; }
            break;
        }
        default:
            break;
        }
        return ItemTypeName(b[1]);
    }

    void RecordPacket(uint8_t id, uint8_t itemId, uint32_t bitsUsed, PacketPriority priority, PacketReliability reliability, char channel, uint32_t size, const uint8_t* bytes, bool broadcast, uint32_t forceReceipt)
    {
        PacketLogEntry e;
        e.seq = ++packetSeq;
        e.id = id;
        e.itemId = itemId;
        e.priority = priority;
        e.reliability = reliability;
        e.channel = channel;
        e.size = size;
        e.bitsUsed = bitsUsed;
        e.broadcast = broadcast;
        e.forceReceipt = forceReceipt;
        e.blocked = false;
        e.truncated = size > MaxStoredPacketBytes;
        e.bytes.assign(bytes, bytes + (std::min)((size_t)size, MaxStoredPacketBytes));
        e.summary = (id == 0x83 && size >= 2) ? DecodeReplicationItem(bytes, size) : ("");

        std::lock_guard<std::mutex> lock(logMutex);
        packetLog.push_back(std::move(e));
        if (packetLog.size() > MaxPacketLogEntries)
            packetLog.erase(packetLog.begin());
    }

    std::vector<PacketLogEntry> SnapshotLog()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        return packetLog;
    }

    void ClearLog()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        packetLog.clear();
    }

    void MarkLastPacketBlocked()
    {
        OutputDebugStringA(("[Raknet] packet blocked by callback"));
        std::lock_guard<std::mutex> lock(logMutex);
        if (!packetLog.empty())
            packetLog.back().blocked = true;
    }

    // â”€â”€ pushPacketUserdata â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    void pushPacketUserdata(lua_State* L, uint8_t* bytes, uint32_t size,
                            PacketPriority priority, PacketReliability reliability, char orderingChannel)
    {
        lua_newtable(L);

        lua_pushinteger(L, (int)size);
        lua_setfield(L, -2, ("Size"));

        lua_pushinteger(L, (int)priority);
        lua_setfield(L, -2, ("Priority"));

        lua_pushinteger(L, (int)reliability);
        lua_setfield(L, -2, ("Reliability"));

        lua_pushinteger(L, (int)(unsigned char)orderingChannel);
        lua_setfield(L, -2, ("OrderingChannel"));

        if (size >= 1) {
            lua_pushinteger(L, (int)bytes[0]);
            lua_setfield(L, -2, ("Id"));

            lua_pushinteger(L, (int)bytes[0]);
            lua_setfield(L, -2, ("PacketId"));
        }

        if (size >= 2) {
            lua_pushinteger(L, (int)bytes[1]);
            lua_setfield(L, -2, ("ItemId"));
        }

        // AsBuffer
        void* buf = lua_newbuffer(L, size);
        memcpy(buf, bytes, size);
        lua_setfield(L, -2, ("AsBuffer"));

        // AsArray
        lua_newtable(L);
        for (uint32_t i = 0; i < size; i++) {
            lua_pushinteger(L, (int)bytes[i]);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, ("AsArray"));

        // AsString
        lua_pushlstring(L, reinterpret_cast<const char*>(bytes), size);
        lua_setfield(L, -2, ("AsString"));
    }

    // â”€â”€ Collect refs from a callback list (called under hooksMutex) â”€â”€
    // NOTE: queue sites iterate the callback vectors directly (instead of a
    // CollectRefs snapshot) so each queued item carries its own
    // registration generation for the stale-across-reinit gate.

    // â”€â”€ ProcessCallbacks â€” drains queue, calls on exploit thread â”€â”€
    // Called during scheduler tick on Shared::ExploitThread (has capabilities).
    static std::atomic<uint64_t> hookGen{ 0 };

    void ProcessCallbacks(lua_State* L)
    {
        if (!L) return;

        std::vector<QueuedCallback> batch;
        {
            std::lock_guard<std::mutex> lock(callbackQueueMutex);
            if (callbackQueue.empty()) return;
            batch.swap(callbackQueue);
        }

        char dbg[128];
        sprintf_s(dbg, ("[Raknet] ProcessCallbacks: draining %zu items\n"), batch.size());
        OutputDebugStringA(dbg);

        const uint64_t liveGen = hookGen.load(std::memory_order_relaxed);

        for (auto& qc : batch) {
            // Stale across re-init (queued before register_lib cleared):
            // fail closed without touching the Lua stack.
            if (qc.gen != liveGen) continue;
            if (qc.ref <= LUA_REFNIL) continue;
            if (qc.size == 0 || qc.data.empty()) continue;

            // Isolate each callback: every iteration starts from the same
            // top, so func/arg alignment can never drift and one bad slot
            // can never poison the next call.
            const int top0 = lua_gettop(L);

            lua_getref(L, qc.ref);
            if (lua_type(L, -1) != LUA_TFUNCTION) {
                lua_settop(L, top0);
                continue;
            }

            pushPacketUserdata(L, qc.data.data(), qc.size, qc.priority, qc.reliability, qc.orderingChannel);

            // Exactly [func, packetTable] must be on top; otherwise the
            // pcall below would invoke the wrong slot. Fail closed.
            if (lua_gettop(L) != top0 + 2) {
                lua_settop(L, top0);
                continue;
            }

            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                // No locks, no Lua re-entry, no recursion: tostring of a
                // string error is pure C (never runs __tostring); anything
                // else gets a fixed placeholder.
                if (lua_type(L, -1) == LUA_TSTRING) {
                    const char* err = lua_tostring(L, -1);
                    if (err) {
                        OutputDebugStringA(("[Raknet] hook error: "));
                        OutputDebugStringA(err);
                        OutputDebugStringA(("\n"));
                    }
                } else {
                    OutputDebugStringA(("[Raknet] hook error: (non-string error object)\n"));
                }
            }

            lua_settop(L, top0);
        }
    }

    // Track whether any send hook is registered
    static std::atomic<bool> hasSendHooks{ false };
    static std::atomic<bool> hasReceiveHooks{ false };

    int64_t __fastcall rakpeerSendSharedHook(int64_t* _this, int64_t* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, int64_t* systemIdentifier, bool Broadcast, uint32_t ForceReceiptNumber, uint64_t unk)
    {
        static int fireCount = 0;
        if (fireCount < 5) {
            fireCount++;
            OutputDebugStringA(("[Raknet] SEND HOOK FIRED\n"));
        }

        if (!original_raknet_send) {
            return 0;
        }

        uintptr_t realBitStream = bitStream[0];
        uint8_t* packetBytes = *reinterpret_cast<uint8_t**>(realBitStream + 0x10);
        uint32_t packetSize = *reinterpret_cast<uint32_t*>(realBitStream + 0x30);
        uint32_t bitsUsed = *reinterpret_cast<uint32_t*>(realBitStream + 0x18);

        if (!packetBytes || packetSize == 0)
            return original_raknet_send(_this, bitStream, priority, reliability, orderingChannel, systemIdentifier, Broadcast, ForceReceiptNumber, unk);

        uint8_t packetId = packetBytes[0];
        ItemType itemId = (packetSize > 1) ? (ItemType)packetBytes[1] : ItemTypeEnd;

        if (!capturePaused)
            RecordPacket(packetId, (uint8_t)itemId, bitsUsed, priority, reliability, orderingChannel, packetSize, packetBytes, Broadcast, ForceReceiptNumber);

        // Desync: corrupt position packets
        if (desyncing && packetId == 0xA1) {
            packetBytes[0] = 0xFF;
            packetBytes[1] = 0xFF;
            packetBytes[2] = 0xFF;
            packetBytes[3] = 0xFF;
            packetBytes[4] = 0xFF;
            packetBytes[5] = 0xFF;
        }

        // Queue for callbacks
        if (hasSendHooks.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> hlock(hooksMutex);
            if (!OnSharedSendCallbacks.empty()) {
                std::lock_guard<std::mutex> lock(callbackQueueMutex);
                for (auto& cb : OnSharedSendCallbacks) {
                    if (cb.ref <= LUA_REFNIL) continue;
                    QueuedCallback qc;
                    qc.ref = cb.ref;
                    qc.gen = cb.gen;
                    qc.data.assign(packetBytes, packetBytes + packetSize);
                    qc.size = packetSize;
                    qc.priority = priority;
                    qc.reliability = reliability;
                    qc.orderingChannel = orderingChannel;
                    qc.isSend = true;
                    callbackQueue.push_back(std::move(qc));
                }
            }
        }

        return original_raknet_send(
            _this, bitStream, priority, reliability, orderingChannel,
            systemIdentifier, Broadcast, ForceReceiptNumber, unk
        );
    }

    void* __fastcall rakpeerReceiveHook(int64_t* _this)
    {
        if (!original_raknet_receive)
            return nullptr;

        void* packet = original_raknet_receive(_this);

        if (!packet)
            return nullptr;

        uint8_t* packetBytes = *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(packet));
        uint32_t packetSize = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(packet) + 0x08);

        if (!packetBytes || packetSize == 0)
            return packet;

        uint8_t packetId = packetBytes[0];
        ItemType itemId = (packetSize > 1) ? (ItemType)packetBytes[1] : ItemTypeEnd;

        if (!capturePaused)
            RecordPacket(packetId, (uint8_t)itemId, 0, MEDIUM_PRIORITY, RELIABLE_ORDERED, 0, packetSize, packetBytes, false, 0);

        // Queue for receive callbacks (filter by per-hook packetId)
        if (hasReceiveHooks.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> hlock(hooksMutex);
            if (!OnSharedReceiveCallbacks.empty()) {
                std::lock_guard<std::mutex> lock(callbackQueueMutex);
                for (auto& cb : OnSharedReceiveCallbacks) {
                    // packetId -1 = all packets, otherwise must match
                    if (cb.packetId >= 0 && cb.packetId != (int)packetId)
                        continue;
                    if (cb.ref <= LUA_REFNIL) continue;

                    QueuedCallback qc;
                    qc.ref = cb.ref;
                    qc.gen = cb.gen;
                    qc.data.assign(packetBytes, packetBytes + packetSize);
                    qc.size = packetSize;
                    qc.priority = MEDIUM_PRIORITY;
                    qc.reliability = RELIABLE_ORDERED;
                    qc.orderingChannel = 0;
                    qc.isSend = false;
                    callbackQueue.push_back(std::move(qc));
                }
            }
        }

        return packet;
    }

     void InitializeHook(uintptr_t Peer)
    {
        if (hooked || !Peer)
            return;

        char dbg[160];
        sprintf_s(dbg, "[Raknet] InitializeHook Peer=0x%llX", (unsigned long long)Peer);
        OutputDebugStringA(dbg);

        uintptr_t RakPeer = *reinterpret_cast<uintptr_t*>(Peer + Offsets::Raknet::RakPeer);
        if (!RakPeer) {
            OutputDebugStringA("[Raknet] InitializeHook: RakPeer is null");
            return;
        }

        sprintf_s(dbg, "[Raknet] RakPeer=0x%llX", (unsigned long long)RakPeer);
        OutputDebugStringA(dbg);

        void** original_vftable = *(void***)(RakPeer);
        void** new_vftable = new void* [150];
        memcpy(new_vftable, original_vftable, sizeof(void*) * 150);

        original_raknet_send = reinterpret_cast<send_fn_t>(original_vftable[0x14]);

        new_vftable[0x14] = reinterpret_cast<void*>(&rakpeerSendSharedHook);

        *(void***)(RakPeer) = new_vftable;

        hooked = true;
        OutputDebugStringA("[Raknet] hook installed");
    }
    int raknet_desync(lua_State* L)
    {
        desyncing = lua_toboolean(L, 1);
        return 0;
    }

    int raknet_add_send_hook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);

        // Pin the documented slot (arg 1) directly. lua_ref neither pops
        // nor pushes, so the frame stays balanced. Never index the
        // transient top (-1) here: any extra value on the frame would make
        // -1 name the wrong object (e.g. a thread value), and every later
        // fire would then invoke that wrong slot.
        int ref = lua_ref(L, 1);
        if (ref <= LUA_REFNIL) {
            // Unreachable after checktype, but fail closed: return the
            // function unchanged so remove_send_hook parity holds.
            lua_pushvalue(L, 1);
            return 1;
        }

        {
            std::lock_guard<std::mutex> lock(hooksMutex);
            OnSharedSendCallbacks.push_back({ Shared::ExploitThread, ref, -1, hookGen.load(std::memory_order_relaxed) });
            hasSendHooks.store(true, std::memory_order_relaxed);
        }

        char buf[128];
        sprintf_s(buf, ("[Raknet] add_send_hook ref=%d total=%zu"), ref, OnSharedSendCallbacks.size());
        OutputDebugStringA(buf);

        lua_pushvalue(L, 1);
        return 1;
    }

    int raknet_remove_send_hook(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TFUNCTION) {
            lua_pushboolean(L, false);
            return 1;
        }

        // Snapshot refs under the lock; every Lua API call runs outside it
        // so the network-thread queue path can never block on Lua/GC.
        std::vector<int> refs;
        {
            std::lock_guard<std::mutex> lock(hooksMutex);
            refs.reserve(OnSharedSendCallbacks.size());
            for (auto& cb : OnSharedSendCallbacks)
                refs.push_back(cb.ref);
        }

        const void* targetFn = lua_topointer(L, 1);

        int match = LUA_NOREF;
        for (int r : refs) {
            if (r <= LUA_REFNIL) continue;
            lua_getref(L, r);
            bool same = (lua_type(L, -1) == LUA_TFUNCTION) && (lua_topointer(L, -1) == targetFn);
            lua_pop(L, 1);
            if (same) { match = r; break; }
        }

        bool removed = false;
        if (match != LUA_NOREF) {
            std::lock_guard<std::mutex> lock(hooksMutex);
            for (size_t i = 0; i < OnSharedSendCallbacks.size(); i++) {
                if (OnSharedSendCallbacks[i].ref == match) {
                    OnSharedSendCallbacks.erase(OnSharedSendCallbacks.begin() + i);
                    hasSendHooks.store(!OnSharedSendCallbacks.empty(), std::memory_order_relaxed);
                    removed = true;
                    break;
                }
            }
        }

        if (removed)
            lua_unref(L, match);

        lua_pushboolean(L, removed);
        return 1;
    }

    // ── raknet.add_receive_hook(callback [, packetId]) ────────
    //  callback : function(packetData)
    //  packetId : int (optional, default -1 = all packets)
    //  Returns the callback so you can pass it to remove_receive_hook.
    //  Callbacks are dispatched through ProcessCallbacks on the
    //  exploit thread during the scheduler tick — safe for any
    //  userdata calls, yield, task.wait, etc.
    int raknet_add_receive_hook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);

        // packetId: -1 = all, 0-255 = specific packet id
        int packetId = (int)luaL_optinteger(L, 2, -1);

        // Pin the documented slot (arg 1) directly; see add_send_hook.
        int ref = lua_ref(L, 1);
        if (ref <= LUA_REFNIL) {
            lua_pushvalue(L, 1);
            return 1;
        }

        {
            std::lock_guard<std::mutex> lock(hooksMutex);
            OnSharedReceiveCallbacks.push_back({ Shared::ExploitThread, ref, packetId, hookGen.load(std::memory_order_relaxed) });
            hasReceiveHooks.store(true, std::memory_order_relaxed);
        }

        char buf[160];
        sprintf_s(buf, "[Raknet] add_receive_hook ref=%d packetId=%s total=%zu",
            ref,
            packetId < 0 ? "ALL" : std::to_string(packetId).c_str(),
            OnSharedReceiveCallbacks.size());
        OutputDebugStringA(buf);

        lua_pushvalue(L, 1);
        return 1;
    }

    // ── raknet.remove_receive_hook(callback) ──────────────────
    //  Pass the same function you gave to add_receive_hook.
    //  Returns true if removed, false if not found.
    int raknet_remove_receive_hook(lua_State* L)
    {
        if (lua_type(L, 1) != LUA_TFUNCTION) {
            lua_pushboolean(L, false);
            return 1;
        }

        // Snapshot refs under the lock; every Lua API call runs outside it.
        std::vector<int> refs;
        {
            std::lock_guard<std::mutex> lock(hooksMutex);
            refs.reserve(OnSharedReceiveCallbacks.size());
            for (auto& cb : OnSharedReceiveCallbacks)
                refs.push_back(cb.ref);
        }

        const void* targetFn = lua_topointer(L, 1);

        int match = LUA_NOREF;
        for (int r : refs) {
            if (r <= LUA_REFNIL) continue;
            lua_getref(L, r);
            bool same = (lua_type(L, -1) == LUA_TFUNCTION) && (lua_topointer(L, -1) == targetFn);
            lua_pop(L, 1);
            if (same) { match = r; break; }
        }

        bool removed = false;
        size_t remaining = 0;
        if (match != LUA_NOREF) {
            std::lock_guard<std::mutex> lock(hooksMutex);
            for (size_t i = 0; i < OnSharedReceiveCallbacks.size(); i++) {
                if (OnSharedReceiveCallbacks[i].ref == match) {
                    OnSharedReceiveCallbacks.erase(OnSharedReceiveCallbacks.begin() + i);
                    hasReceiveHooks.store(!OnSharedReceiveCallbacks.empty(), std::memory_order_relaxed);
                    removed = true;
                    break;
                }
            }
            remaining = OnSharedReceiveCallbacks.size();
        }

        if (removed) {
            lua_unref(L, match);

            char buf[128];
            sprintf_s(buf, "[Raknet] remove_receive_hook ref=%d remaining=%zu",
                match, remaining);
            OutputDebugStringA(buf);
        }

        lua_pushboolean(L, removed);
        return 1;
    }

    int raknet_getpacketlog(lua_State* L)
    {
        auto snap = SnapshotLog();
        lua_newtable(L);
        int idx = 1;

        for (auto& e : snap) {
            lua_newtable(L);

            lua_pushinteger(L, (lua_Integer)e.seq);
            lua_setfield(L, -2, ("Seq"));

            lua_pushinteger(L, (int)e.id);
            lua_setfield(L, -2, ("Id"));

            lua_pushinteger(L, (int)e.itemId);
            lua_setfield(L, -2, ("ItemId"));

            lua_pushstring(L, ItemTypeName(e.itemId));
            lua_setfield(L, -2, ("ItemName"));

            lua_pushinteger(L, (int)e.priority);
            lua_setfield(L, -2, ("Priority"));

            lua_pushinteger(L, (int)e.reliability);
            lua_setfield(L, -2, ("Reliability"));

            lua_pushinteger(L, (int)(unsigned char)e.channel);
            lua_setfield(L, -2, ("Channel"));

            lua_pushinteger(L, (int)e.size);
            lua_setfield(L, -2, ("Size"));

            lua_pushboolean(L, e.blocked);
            lua_setfield(L, -2, ("Blocked"));

            if (!e.summary.empty())
                lua_pushstring(L, e.summary.c_str());
            else
                lua_pushstring(L, ItemTypeName(e.itemId));
            lua_setfield(L, -2, ("Summary"));

            lua_newtable(L);
            uint32_t showSize = static_cast<uint32_t>((std::min)((size_t)e.size, (size_t)64));
            for (uint32_t i = 0; i < showSize; i++) {
                lua_pushinteger(L, (int)e.bytes[i]);
                lua_rawseti(L, -2, i + 1);
            }
            lua_setfield(L, -2, ("Bytes"));

            lua_rawseti(L, -2, idx++);
        }

        return 1;
    }

    int raknet_clearpacketlog(lua_State* L)
    {
        ClearLog();
        return 0;
    }

    int raknet_capturepaused(lua_State* L)
    {
        capturePaused = lua_toboolean(L, 1);
        return 0;
    }

    int raknet_desyncping(lua_State* L)
    {
        bool enabled = lua_toboolean(L, 1);

        if (enabled) {
            if (!Shared::ExploitThread) return 0;
            Shared::StateGeneration.fetch_add(1);
        }

        return 0;
    }

    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    //  OUTGOING PACKET API
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

    bool SendRawPacket(const uint8_t* data, uint32_t size, PacketPriority priority,
                       PacketReliability reliability, char channel)
    {
        if (!RakPeer_instance || !original_raknet_send) {
            OutputDebugStringA(("[Raknet] SendRawPacket: RakPeer not initialized"));
            return false;
        }

        if (!data || size == 0) {
            OutputDebugStringA(("[Raknet] SendRawPacket: empty data"));
            return false;
        }

        // Queue for send hooks before sending
        if (hasSendHooks.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> hlock(hooksMutex);
            if (!OnSharedSendCallbacks.empty()) {
                std::lock_guard<std::mutex> lock(callbackQueueMutex);
                for (auto& cb : OnSharedSendCallbacks) {
                    if (cb.ref <= LUA_REFNIL) continue;
                    QueuedCallback qc;
                    qc.ref = cb.ref;
                    qc.gen = cb.gen;
                    qc.data.assign(data, data + size);
                    qc.size = size;
                    qc.priority = priority;
                    qc.reliability = reliability;
                    qc.orderingChannel = channel;
                    qc.isSend = true;
                    callbackQueue.push_back(std::move(qc));
                }
            }
        }

        const size_t kBSOffset = 0x20;
        const size_t kDataOffset = 0x60;
        const size_t kTotalSize = kDataOffset + size;

        uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, kTotalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mem) return false;

        memset(mem, 0, kTotalSize);

        uint8_t* dataBuf = mem + kDataOffset;
        memcpy(dataBuf, data, size);

        *reinterpret_cast<uint8_t**>(mem + kBSOffset + 0x10) = dataBuf;
        *reinterpret_cast<uint32_t*>(mem + kBSOffset + 0x18) = size * 8;
        *reinterpret_cast<uint32_t*>(mem + kBSOffset + 0x20) = 0;
        *reinterpret_cast<uint32_t*>(mem + kBSOffset + 0x24) = size * 8;
        *reinterpret_cast<uint32_t*>(mem + kBSOffset + 0x28) = size * 8;
        *reinterpret_cast<uint32_t*>(mem + kBSOffset + 0x30) = size;
        *reinterpret_cast<uintptr_t*>(mem) = (uintptr_t)(mem + kBSOffset);

        int64_t result = original_raknet_send(
            reinterpret_cast<int64_t*>(RakPeer_instance),
            reinterpret_cast<int64_t*>(mem),
            priority,
            reliability,
            channel,
            nullptr,
            false,
            0,
            0
        );

        VirtualFree(mem, 0, MEM_RELEASE);

        char buf[128];
        sprintf_s(buf, ("[Raknet] SendRawPacket: %u bytes, result=%lld"), size, (long long)result);
        OutputDebugStringA(buf);

        return result != 0;
    }

    int raknet_send_raw(lua_State* L)
    {
        size_t dataLen = 0;
        const char* data = luaL_checklstring(L, 1, &dataLen);

        int priority = (int)luaL_optinteger(L, 2, HIGH_PRIORITY);
        int reliability = (int)luaL_optinteger(L, 3, RELIABLE_ORDERED);
        int channel = (int)luaL_optinteger(L, 4, 0);

        if (dataLen == 0) {
            luaL_error(L, ("raknet.send: empty packet data"));
            return 0;
        }

        bool ok = SendRawPacket(
            reinterpret_cast<const uint8_t*>(data),
            (uint32_t)dataLen,
            (PacketPriority)priority,
            (PacketReliability)reliability,
            (char)channel
        );

        lua_pushboolean(L, ok);
        return 1;
    }

    // â”€â”€ BitStream userdata methods â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    static LuaBitStream* checkBitStream(lua_State* L, int idx = 1) {
        return (LuaBitStream*)luaL_checkudata(L, idx, ("RakBitStream"));
    }

    static int bitstream_gc(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        bs->~LuaBitStream();
        return 0;
    }

    int raknet_bitstream_new(lua_State* L) {
        LuaBitStream* bs = (LuaBitStream*)lua_newuserdata(L, sizeof(LuaBitStream));
        new (bs) LuaBitStream();

        luaL_getmetatable(L, ("RakBitStream"));
        lua_setmetatable(L, -2);
        return 1;
    }

    int raknet_bitstream_write(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        size_t len = 0;
        const char* data = luaL_checklstring(L, 2, &len);
        bs->buffer.insert(bs->buffer.end(), data, data + len);
        return 0;
    }

    int raknet_bitstream_writestring(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        size_t len = 0;
        const char* data = luaL_checklstring(L, 2, &len);

        uint32_t val = (uint32_t)len;
        while (val >= 0x80) {
            bs->buffer.push_back((uint8_t)(val | 0x80));
            val >>= 7;
        }
        bs->buffer.push_back((uint8_t)val);

        bs->buffer.insert(bs->buffer.end(), data, data + len);
        return 0;
    }

    int raknet_bitstream_writeu8(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        uint8_t v = (uint8_t)luaL_checkinteger(L, 2);
        bs->buffer.push_back(v);
        return 0;
    }

    int raknet_bitstream_writeu16(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        uint16_t v = (uint16_t)luaL_checkinteger(L, 2);
        bs->buffer.push_back((uint8_t)(v & 0xFF));
        bs->buffer.push_back((uint8_t)((v >> 8) & 0xFF));
        return 0;
    }

    int raknet_bitstream_writeu32(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        uint32_t v = (uint32_t)luaL_checkinteger(L, 2);
        bs->buffer.push_back((uint8_t)(v));
        bs->buffer.push_back((uint8_t)(v >> 8));
        bs->buffer.push_back((uint8_t)(v >> 16));
        bs->buffer.push_back((uint8_t)(v >> 24));
        return 0;
    }

    int raknet_bitstream_writef32(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        float v = (float)luaL_checknumber(L, 2);
        uint32_t iv;
        memcpy(&iv, &v, 4);
        bs->buffer.push_back((uint8_t)(iv));
        bs->buffer.push_back((uint8_t)(iv >> 8));
        bs->buffer.push_back((uint8_t)(iv >> 16));
        bs->buffer.push_back((uint8_t)(iv >> 24));
        return 0;
    }

    int raknet_bitstream_writef64(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        double v = luaL_checknumber(L, 2);
        uint64_t iv;
        memcpy(&iv, &v, 8);
        for (int i = 0; i < 8; i++)
            bs->buffer.push_back((uint8_t)((iv >> (i * 8)) & 0xFF));
        return 0;
    }

    int raknet_bitstream_clear(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        bs->buffer.clear();
        bs->sent = false;
        return 0;
    }

    int raknet_bitstream_getlength(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        lua_pushinteger(L, (lua_Integer)bs->buffer.size());
        return 1;
    }

    int raknet_bitstream_send(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);

        if (bs->buffer.empty()) {
            luaL_error(L, ("BitStream:Send â€” buffer is empty"));
            return 0;
        }

        int priority = (int)luaL_optinteger(L, 2, HIGH_PRIORITY);
        int reliability = (int)luaL_optinteger(L, 3, RELIABLE_ORDERED);
        int channel = (int)luaL_optinteger(L, 4, 0);

        bool ok = SendRawPacket(
            bs->buffer.data(),
            (uint32_t)bs->buffer.size(),
            (PacketPriority)priority,
            (PacketReliability)reliability,
            (char)channel
        );

        lua_pushboolean(L, ok);
        return 1;
    }

    int raknet_bitstream_tostring(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);
        lua_pushlstring(L, reinterpret_cast<const char*>(bs->buffer.data()), (int)bs->buffer.size());
        return 1;
    }

    int rakensend_bitstream(lua_State* L) {
        LuaBitStream* bs = checkBitStream(L);

        if (bs->buffer.empty()) {
            luaL_error(L, ("raknet.Send â€” BitStream is empty"));
            return 0;
        }

        int priority = (int)luaL_optinteger(L, 2, HIGH_PRIORITY);
        int reliability = (int)luaL_optinteger(L, 3, RELIABLE_ORDERED);
        int channel = (int)luaL_optinteger(L, 4, 0);

        bool ok = SendRawPacket(
            bs->buffer.data(),
            (uint32_t)bs->buffer.size(),
            (PacketPriority)priority,
            (PacketReliability)reliability,
            (char)channel
        );

        lua_pushboolean(L, ok);
        return 1;
    }

    // â”€â”€ PacketContext metatable â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    struct PacketContextUD {
        uint8_t* bytes;
        uint32_t size;
        PacketPriority priority;
        PacketReliability reliability;
        char orderingChannel;
        bool blocked;
        bool modified;
        std::vector<uint8_t> modifiedBytes;
    };

    static int pktctx_index(lua_State* L)
    {
        PacketContextUD* ctx = (PacketContextUD*)lua_touserdata(L, 1);
        const char* key = luaL_checkstring(L, 2);
        if (!ctx || !key) { lua_pushnil(L); return 1; }

        if (strcmp(key, ("Size")) == 0 || strcmp(key, ("Length")) == 0)
            lua_pushinteger(L, ctx->size);
        else if (strcmp(key, ("Id")) == 0 || strcmp(key, ("PacketId")) == 0)
            lua_pushinteger(L, ctx->size >= 1 ? (int)ctx->bytes[0] : 0);
        else if (strcmp(key, ("ItemId")) == 0)
            lua_pushinteger(L, ctx->size >= 2 ? (int)ctx->bytes[1] : 0);
        else if (strcmp(key, ("Priority")) == 0)
            lua_pushinteger(L, (int)ctx->priority);
        else if (strcmp(key, ("Reliability")) == 0)
            lua_pushinteger(L, (int)ctx->reliability);
        else if (strcmp(key, ("OrderingChannel")) == 0)
            lua_pushinteger(L, (int)(unsigned char)ctx->orderingChannel);
        else if (strcmp(key, ("Blocked")) == 0)
            lua_pushboolean(L, ctx->blocked);
        else if (strcmp(key, ("Modified")) == 0)
            lua_pushboolean(L, ctx->modified);
        else if (strcmp(key, ("Bytes")) == 0) {
            lua_newtable(L);
            uint32_t show = ctx->modified ? (uint32_t)ctx->modifiedBytes.size() : ctx->size;
            const uint8_t* src = ctx->modified ? ctx->modifiedBytes.data() : ctx->bytes;
            for (uint32_t i = 0; i < show; i++) {
                lua_pushinteger(L, (int)src[i]);
                lua_rawseti(L, -2, i + 1);
            }
        }
        else if (strcmp(key, ("AsString")) == 0) {
            if (ctx->modified)
                lua_pushlstring(L, (const char*)ctx->modifiedBytes.data(), (int)ctx->modifiedBytes.size());
            else
                lua_pushlstring(L, (const char*)ctx->bytes, ctx->size);
        }
        else if (strcmp(key, ("AsBuffer")) == 0) {
            if (ctx->modified) {
                void* buf = lua_newbuffer(L, (int)ctx->modifiedBytes.size());
                memcpy(buf, ctx->modifiedBytes.data(), ctx->modifiedBytes.size());
            } else {
                void* buf = lua_newbuffer(L, ctx->size);
                memcpy(buf, ctx->bytes, ctx->size);
            }
        }
        else lua_pushnil(L);

        return 1;
    }

    static int pktctx_newindex(lua_State* L)
    {
        PacketContextUD* ctx = (PacketContextUD*)lua_touserdata(L, 1);
        const char* key = luaL_checkstring(L, 2);
        if (!ctx || !key) return 0;

        if (strcmp(key, ("Blocked")) == 0) {
            ctx->blocked = lua_toboolean(L, 3) != 0;
        }
        else if (strcmp(key, ("Bytes")) == 0) {
            if (lua_istable(L, 3)) {
                int len = (int)lua_objlen(L, 3);
                ctx->modifiedBytes.resize(len);
                for (int i = 0; i < len; i++) {
                    lua_rawgeti(L, 3, i + 1);
                    ctx->modifiedBytes[i] = (uint8_t)lua_tointeger(L, -1);
                    lua_pop(L, 1);
                }
                ctx->modified = true;
            }
            else if (lua_isstring(L, 3)) {
                size_t len = 0;
                const char* s = lua_tolstring(L, 3, &len);
                ctx->modifiedBytes.assign((const uint8_t*)s, (const uint8_t*)s + len);
                ctx->modified = true;
            }
        }
        else if (strcmp(key, ("AsString")) == 0) {
            if (lua_isstring(L, 3)) {
                size_t len = 0;
                const char* s = lua_tolstring(L, 3, &len);
                ctx->modifiedBytes.assign((const uint8_t*)s, (const uint8_t*)s + len);
                ctx->modified = true;
            }
        }
        else if (strcmp(key, ("Size")) == 0) {
            ctx->size = (uint32_t)lua_tointeger(L, 3);
        }
        else if (strcmp(key, ("Priority")) == 0) {
            ctx->priority = (PacketPriority)lua_tointeger(L, 3);
        }
        else if (strcmp(key, ("Reliability")) == 0) {
            ctx->reliability = (PacketReliability)lua_tointeger(L, 3);
        }
        else if (strcmp(key, ("OrderingChannel")) == 0) {
            ctx->orderingChannel = (char)lua_tointeger(L, 3);
        }

        return 0;
    }

    static int pktctx_gc(lua_State* L)
    {
        PacketContextUD* ctx = (PacketContextUD*)lua_touserdata(L, 1);
        if (ctx) ctx->~PacketContextUD();
        return 0;
    }

    void RegisterPacketContextMeta(lua_State* L)
    {
        if (luaL_newmetatable(L, ("RaknetPacketCtx"))) {
            lua_pushcfunction(L, pktctx_index, ("RaknetPacketCtx.__index"));
            lua_setfield(L, -2, ("__index"));

            lua_pushcfunction(L, pktctx_newindex, ("RaknetPacketCtx.__newindex"));
            lua_setfield(L, -2, ("__newindex"));

            lua_pushcfunction(L, pktctx_gc, ("RaknetPacketCtx.__gc"));
            lua_setfield(L, -2, ("__gc"));
        }
        lua_pop(L, 1);
    }

    void register_lib(lua_State* L)
    {
        funcnameinc = 0;
        desyncing = false;
        hasSendHooks.store(false, std::memory_order_relaxed);
        hasReceiveHooks.store(false, std::memory_order_relaxed);

        RegisterPacketContextMeta(L);

        // Clean up stale callback lists on re-init. Bump first so anything
        // queued-but-undrained is stale-gated in ProcessCallbacks even if
        // it slips past the clears below.
        hookGen.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(callbackQueueMutex);
            callbackQueue.clear();
        }
        {
            // NOTE: refs are intentionally NOT unref'd here. They were
            // pinned on a possibly-destroyed global (teleport); unref'ing
            // those numbers against the NEW registry corrupts its freelist
            // and lets later slots alias live objects. Old-global slots die
            // with the old global; same-global stragglers are a bounded,
            // harmless pin gated by hookGen at dispatch.
            std::lock_guard<std::mutex> lock(hooksMutex);
            OnSharedSendCallbacks.clear();
            OnSharedReceiveCallbacks.clear();
        }

        // â”€â”€ BitStream metatable â”€â”€
        luaL_newmetatable(L, ("RakBitStream"));
        lua_pushcfunction(L, bitstream_gc, ("RakBitStream.__gc"));
        lua_setfield(L, -2, ("__gc"));

        lua_newtable(L);
        Environment->make_table_function(L, ("Write"), raknet_bitstream_write, funcnameinc++);
        Environment->make_table_function(L, ("WriteString"), raknet_bitstream_writestring, funcnameinc++);
        Environment->make_table_function(L, ("WriteU8"), raknet_bitstream_writeu8, funcnameinc++);
        Environment->make_table_function(L, ("WriteU16"), raknet_bitstream_writeu16, funcnameinc++);
        Environment->make_table_function(L, ("WriteU32"), raknet_bitstream_writeu32, funcnameinc++);
        Environment->make_table_function(L, ("WriteFloat"), raknet_bitstream_writef32, funcnameinc++);
        Environment->make_table_function(L, ("WriteDouble"), raknet_bitstream_writef64, funcnameinc++);
        Environment->make_table_function(L, ("Clear"), raknet_bitstream_clear, funcnameinc++);
        Environment->make_table_function(L, ("GetLength"), raknet_bitstream_getlength, funcnameinc++);
        Environment->make_table_function(L, ("Send"), raknet_bitstream_send, funcnameinc++);
        Environment->make_table_function(L, ("ToString"), raknet_bitstream_tostring, funcnameinc++);
        lua_setfield(L, -2, ("__index"));
        lua_pop(L, 1);

        // â”€â”€ Main raknet table â”€â”€
        lua_newtable(L);

        Environment->make_table_function(L, ("desync"), raknet_desync, funcnameinc++);
        Environment->make_table_function(L, ("add_send_hook"), raknet_add_send_hook, funcnameinc++);
        Environment->make_table_function(L, ("remove_send_hook"), raknet_remove_send_hook, funcnameinc++);
        Environment->make_table_function(L, ("add_receive_hook"), raknet_add_receive_hook, funcnameinc++);
        Environment->make_table_function(L, ("remove_receive_hook"), raknet_remove_receive_hook, funcnameinc++);
        Environment->make_table_function(L, ("getpacketlog"), raknet_getpacketlog, funcnameinc++);
        Environment->make_table_function(L, ("clearpacketlog"), raknet_clearpacketlog, funcnameinc++);
        Environment->make_table_function(L, ("capturepaused"), raknet_capturepaused, funcnameinc++);
        Environment->make_table_function(L, ("desyncping"), raknet_desyncping, funcnameinc++);

        Environment->make_table_function(L, ("send"), raknet_send_raw, funcnameinc++);
        Environment->make_table_function(L, ("Send"), rakensend_bitstream, funcnameinc++);

        lua_newtable(L);
        Environment->make_table_function(L, ("new"), raknet_bitstream_new, funcnameinc++);
        lua_setfield(L, -2, ("BitStream"));

        lua_setglobal(L, ("raknet"));

        Environment->make_global(L, ("add_send_hook"), Raknet::raknet_add_send_hook, funcnameinc++);
        Environment->make_global(L, ("remove_send_hook"), Raknet::raknet_remove_send_hook, funcnameinc++);
        Environment->make_global(L, ("add_receive_hook"), Raknet::raknet_add_receive_hook, funcnameinc++);
        Environment->make_global(L, ("remove_receive_hook"), Raknet::raknet_remove_receive_hook, funcnameinc++);
    }
}

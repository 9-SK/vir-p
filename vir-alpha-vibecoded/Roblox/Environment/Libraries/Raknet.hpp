#pragma once

#include <Windows.h>
#include <lualib.h>
#include <algorithm>
#include <vector>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Update/Offsets.hpp>

namespace Raknet
{
    struct LuaCallbacks
    {
        lua_State* L;
        int ref;
    };

    inline std::vector<LuaCallbacks> OnSharedSendCallbacks;

    enum PacketPriority
    {
        IMMEDIATE_PRIORITY,
        HIGH_PRIORITY,
        MEDIUM_PRIORITY,
        LOW_PRIORITY,
        NUMBER_OF_PRIORITIES
    };

    enum PacketReliability
    {
        UNRELIABLE,
        UNRELIABLE_SEQUENCED,
        RELIABLE,
        RELIABLE_ORDERED,
        RELIABLE_SEQUENCED,
        UNRELIABLE_WITH_ACK_RECEIPT,
        RELIABLE_WITH_ACK_RECEIPT,
        RELIABLE_ORDERED_WITH_ACK_RECEIPT,
        NUMBER_OF_RELIABILITIES
    };

    enum ItemType : int32_t
    {
        ItemTypeEnd = 0x0,
        ItemTypeDelete = 0x1,
        ItemTypeNew = 0x2,
        ItemTypeChangeProperty = 0x3,
        ItemTypeMarker = 0x4,
        ItemTypePing = 0x5,
        ItemTypePingBack = 0x6,
        ItemTypeEventInvocation = 0x7,
        ItemTypeRequestCharacter = 0x8,
        ItemTypeRocky = 0x9,
        ItemTypeCFrameAcknowledgement = 0xA,
        ItemTypeJoinData = 0xB,
        ItemTypeUpdateClientQuota = 0xC,
        ItemTypeStreamData = 0xD,
        ItemTypeRegionRemoval = 0xE,
        ItemTypeInstanceRemoval = 0xF,
        ItemTypeTag = 0x10,
        ItemTypeStats = 0x11,
        ItemTypeHash = 0x12,
        ItemTypeResolveDeferredTree = 0x13,
        ItemTypeStreamDataInfo = 0x14,
        ItemTypeChangeAttribute = 0x15,
        ItemTypeJoinDataV2 = 0x16,
        ItemTypeStreamTerrain = 0x17,
        ItemTypeStreamPrefetchInfo = 0x18,
        ItemClientQoutaStreamUpdate = 0x19,
        ItemClientQos = 0x1F,
        ItemDeferredSharedString = 0x22,
        ItemTypeISRTimestamp = 0x23,
        ItemStreamPinData = 0x25
    };

    struct PacketContext {
        uint8_t* originalBytes;
        uint32_t size;
        PacketPriority priority;
        PacketReliability reliability;
        char orderingChannel;

        std::vector<uint8_t> modifiedBytes;
        bool blocked;
        bool modified;

        PacketContext(uint8_t* bytes, uint32_t sz, PacketPriority p, PacketReliability r, char ch)
            : originalBytes(bytes), size(sz), priority(p), reliability(r),
            orderingChannel(ch), blocked(false), modified(false) {
        }
    };

    inline int packet_Block(lua_State* L)
    {
        auto* ctx = *reinterpret_cast<PacketContext**>(lua_touserdata(L, 1));
        ctx->blocked = true;
        return 0;
    }

    inline int packet_SetData(lua_State* L)
    {
        auto* ctx = *reinterpret_cast<PacketContext**>(lua_touserdata(L, 1));

        ctx->modifiedBytes.clear();

        int t = lua_type(L, 2);
        if (t == LUA_TTABLE) {
            int len = (int)lua_objlen(L, 2);
            for (int i = 1; i <= len; i++) {
                lua_rawgeti(L, 2, i);
                ctx->modifiedBytes.push_back((uint8_t)lua_tointeger(L, -1));
                lua_pop(L, 1);
            }
        }
        else if (t == LUA_TBUFFER) {
            size_t len = 0;
            void* data = lua_tobuffer(L, 2, &len);
            if (data && len > 0) {
                ctx->modifiedBytes.assign(reinterpret_cast<uint8_t*>(data),
                    reinterpret_cast<uint8_t*>(data) + len);
            }
        }
        else {
            luaL_error(L, "SetData expects a table or buffer");
            return 0;
        }

        ctx->modified = true;
        return 0;
    }

    inline void pushPacketUserdata(lua_State* L, PacketContext* ctx)
    {
        auto** ud = reinterpret_cast<PacketContext**>(lua_newuserdata(L, sizeof(PacketContext*)));
        *ud = ctx;

        lua_newtable(L);
        lua_newtable(L);

        lua_pushcfunction(L, packet_Block, "Block");
        lua_setfield(L, -2, "Block");

        lua_pushcfunction(L, packet_SetData, "SetData");
        lua_setfield(L, -2, "SetData");

        lua_pushinteger(L, (int)ctx->size);
        lua_setfield(L, -2, "Size");

        lua_pushinteger(L, (int)ctx->priority);
        lua_setfield(L, -2, "Priority");

        lua_pushinteger(L, (int)ctx->reliability);
        lua_setfield(L, -2, "Reliability");

        lua_pushinteger(L, (int)(unsigned char)ctx->orderingChannel);
        lua_setfield(L, -2, "OrderingChannel");

        void* buf = lua_newbuffer(L, ctx->size);
        memcpy(buf, ctx->originalBytes, ctx->size);
        lua_setfield(L, -2, "AsBuffer");

        lua_pushinteger(L, (int)ctx->originalBytes[0]);
        lua_setfield(L, -2, "Id");

        lua_pushinteger(L, (int)ctx->originalBytes[0]);
        lua_setfield(L, -2, "PacketId");

        if (ctx->size >= 2) {
            lua_pushinteger(L, (int)ctx->originalBytes[1]);
            lua_setfield(L, -2, "ItemId");
        }

        lua_newtable(L);
        for (uint32_t i = 0; i < ctx->size; i++) {
            lua_pushinteger(L, (int)ctx->originalBytes[i]);
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "AsArray");

        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
    }

    inline bool callCallbacks(PacketContext* ctx)
    {
        for (auto& hook : OnSharedSendCallbacks) {
            lua_getref(hook.L, hook.ref);
            pushPacketUserdata(hook.L, ctx);

            if (lua_pcall(hook.L, 1, 0, 0) != LUA_OK) {
                lua_pop(hook.L, 1);
                continue;
            }
        }

        return !ctx->blocked;
    }

    using send_fn_t = int64_t(__fastcall*)(int64_t*, int64_t*, PacketPriority, PacketReliability, char, int64_t*, bool, uint32_t, uint64_t);
    inline send_fn_t original_raknet_send = nullptr;

    inline bool desyncing = false;
    inline bool hooked = false;

    struct PacketLogEntry
    {
        uint64_t seq;
        uint8_t id;
        uint8_t itemId;
        PacketPriority priority;
        PacketReliability reliability;
        char channel;
        uint32_t size;
        uint32_t bitsUsed;
        bool broadcast;
        uint32_t forceReceipt;
        bool blocked;
        bool truncated;
        std::string summary;
        std::vector<uint8_t> bytes;
    };

    inline std::mutex logMutex;
    inline std::vector<PacketLogEntry> packetLog;
    inline uint64_t packetSeq = 0;
    inline bool capturePaused = false;
    inline constexpr size_t MaxPacketLogEntries = 200;
    inline constexpr size_t MaxStoredPacketBytes = 512;

    inline const char* ItemTypeName(uint8_t id);

    // Byte-cursor reader mirroring RBX::Network::NetworkStream (data +16, len +48, pos +56)
    struct NetReader
    {
        const uint8_t* d;
        uint32_t n;
        uint32_t pos;

        bool read(void* out, uint32_t len)
        {
            if (pos + len > n) return false;
            memcpy(out, d + pos, len);
            pos += len;
            return true;
        }
        bool u8(uint8_t& v) { return read(&v, 1); }
        bool u16(uint16_t& v) { return read(&v, 2); }
        bool u32(uint32_t& v) { return read(&v, 4); }
        bool varint(uint32_t& v)
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
    };

    // Wire format (from Replicator::readItem/deserializeItem): [0x83][itemId][payload], itemType = itemId - 1
    inline std::string DecodeReplicationItem(const uint8_t* b, uint32_t n)
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
            if (r.varint(id)) { sprintf_s(buf, "Delete obj=0x%X", id); return buf; }
            break;
        }
        case ItemTypeMarker:
        {
            uint32_t ts = 0;
            if (r.u32(ts)) { sprintf_s(buf, "Marker ts=%u", ts); return buf; }
            break;
        }
        case ItemTypePing:
        case ItemTypePingBack:
        {
            uint8_t flag;
            uint64_t d;
            uint32_t first;
            if (r.u8(flag) && r.read(&d, 8) && r.u32(first)) { sprintf_s(buf, "Ping flag=%u value=%llu", flag, d); return buf; }
            break;
        }
        case ItemTypeEventInvocation:
        {
            uint32_t obj = 0, extra = 0;
            uint16_t ev = 0;
            if (r.varint(obj) && r.u32(extra) && r.u16(ev))
            {
                sprintf_s(buf, "EventInvocation obj=0x%X ev=0x%X (%d args left=%u)", obj, ev, ev, (uint32_t)(r.n - r.pos));
                return buf;
            }
            break;
        }
        case ItemTypeISRTimestamp:
        {
            uint32_t ts = 0;
            if (r.u32(ts)) { sprintf_s(buf, "ISRTimestamp ts=%u", ts); return buf; }
            break;
        }
        default:
            break;
        }
        return ItemTypeName(b[1]);
    }

    inline void RecordPacket(uint8_t id, uint8_t itemId, uint32_t bitsUsed, PacketPriority priority, PacketReliability reliability, char channel, uint32_t size, const uint8_t* bytes, bool broadcast, uint32_t forceReceipt)
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
        e.summary = (id == 0x83 && size >= 2) ? DecodeReplicationItem(bytes, size) : "";

        std::lock_guard<std::mutex> lock(logMutex);
        packetLog.push_back(std::move(e));
        if (packetLog.size() > MaxPacketLogEntries)
            packetLog.erase(packetLog.begin());
    }

    inline std::vector<PacketLogEntry> SnapshotLog()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        return packetLog;
    }

    inline void ClearLog()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        packetLog.clear();
    }

    inline const char* ItemTypeName(uint8_t id)
    {
        switch ((ItemType)id)
        {
        case ItemTypeEnd: return "End";
        case ItemTypeDelete: return "Delete";
        case ItemTypeNew: return "New";
        case ItemTypeChangeProperty: return "ChangeProperty";
        case ItemTypeMarker: return "Marker";
        case ItemTypePing: return "Ping";
        case ItemTypePingBack: return "PingBack";
        case ItemTypeEventInvocation: return "EventInvocation";
        case ItemTypeRequestCharacter: return "RequestCharacter";
        case ItemTypeRocky: return "Rocky";
        case ItemTypeCFrameAcknowledgement: return "CFrameAck";
        case ItemTypeJoinData: return "JoinData";
        case ItemTypeUpdateClientQuota: return "UpdateClientQuota";
        case ItemTypeStreamData: return "StreamData";
        case ItemTypeRegionRemoval: return "RegionRemoval";
        case ItemTypeInstanceRemoval: return "InstanceRemoval";
        case ItemTypeTag: return "Tag";
        case ItemTypeStats: return "Stats";
        case ItemTypeHash: return "Hash";
        case ItemTypeResolveDeferredTree: return "ResolveDeferredTree";
        case ItemTypeStreamDataInfo: return "StreamDataInfo";
        case ItemTypeChangeAttribute: return "ChangeAttribute";
        case ItemTypeJoinDataV2: return "JoinDataV2";
        case ItemTypeStreamTerrain: return "StreamTerrain";
        case ItemTypeStreamPrefetchInfo: return "StreamPrefetchInfo";
        case ItemClientQoutaStreamUpdate: return "ClientQuotaStreamUpdate";
        case ItemClientQos: return "ClientQos";
        case ItemDeferredSharedString: return "DeferredSharedString";
        case ItemTypeISRTimestamp: return "ISRTimestamp";
        case ItemStreamPinData: return "StreamPinData";
        default: return "Unknown";
        }
    }

    inline void MarkLastPacketBlocked()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (!packetLog.empty())
            packetLog.back().blocked = true;
    }

    inline int64_t __fastcall rakpeerSendSharedHook(int64_t* _this, int64_t* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, int64_t* systemIdentifier, bool Broadcast, uint32_t ForceReceiptNumber, uint64_t unk)
    {
        if (!original_raknet_send) {
            return 0;
        }

        uintptr_t realBitStream = bitStream[0];
        uint8_t* packetBytes = *reinterpret_cast<uint8_t**>(realBitStream + 0x10);
        uint32_t packetSize = *reinterpret_cast<uint32_t*>(realBitStream + 0x30);
        uint32_t bitsUsed = *reinterpret_cast<uint32_t*>(realBitStream + 0x18);

        uint8_t packetId = packetBytes ? packetBytes[0] : 0;
        ItemType itemId = (packetBytes && packetSize > 1) ? (ItemType)packetBytes[1] : ItemTypeEnd;

        if (packetBytes && packetSize > 0 && !capturePaused)
            RecordPacket(packetId, (uint8_t)itemId, bitsUsed, priority, reliability, orderingChannel, packetSize, packetBytes, Broadcast, ForceReceiptNumber);

        switch (packetId)
        {
        case 0x83: {

            break;
        }
        case 0xA1: {
            if (desyncing) {
                packetBytes[0] = 0xFF;
                packetBytes[1] = 0xFF;
                packetBytes[2] = 0xFF;
                packetBytes[3] = 0xFF;
                packetBytes[4] = 0xFF;
                packetBytes[5] = 0xFF;
            }

            break;
        }
        };

        if (!OnSharedSendCallbacks.empty()) {
            PacketContext ctx(packetBytes, packetSize, priority, reliability, orderingChannel);

            if (!callCallbacks(&ctx)) {
                MarkLastPacketBlocked();
                return 0;
            }

            if (ctx.modified && !ctx.modifiedBytes.empty()) {
                size_t writeSize = (std::min)((size_t)packetSize, ctx.modifiedBytes.size());
                memcpy(packetBytes, ctx.modifiedBytes.data(), writeSize);
            }
        }

        return original_raknet_send(
            _this, bitStream, priority, reliability, orderingChannel,
            systemIdentifier, Broadcast, ForceReceiptNumber, unk
        );
    }

    inline void InitializeHook(uintptr_t Peer)
    {
        if (hooked || !Peer)
            return;

        uintptr_t RakPeer = *reinterpret_cast<uintptr_t*>(Peer + Offsets::Raknet::RakPeer);
        if (!RakPeer) {
            return;
        }

        void** original_vftable = *(void***)(RakPeer);
        void** new_vftable = new void* [150];
        memcpy(new_vftable, original_vftable, sizeof(void*) * 150);

        original_raknet_send = reinterpret_cast<send_fn_t>(original_vftable[0x14]);

        new_vftable[0x14] = reinterpret_cast<void*>(&rakpeerSendSharedHook);

        *(void***)(RakPeer) = new_vftable;

        hooked = true;
    }

    inline int raknet_desync(lua_State* L)
    {
        desyncing = lua_toboolean(L, 1);
        return 0;
    }

    inline int raknet_add_send_hook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);

        lua_pushvalue(L, 1);
        lua_xmove(L, Shared::VoltState, 1);

        int ref = lua_ref(Shared::VoltState, -1);

        OnSharedSendCallbacks.push_back({ Shared::VoltState, ref });

        return 0;
    }

    inline int raknet_remove_send_hook(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TFUNCTION);

        lua_pushvalue(L, 1);

        for (size_t i = 0; i < OnSharedSendCallbacks.size(); i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, OnSharedSendCallbacks[i].ref);
            bool same = lua_rawequal(L, -1, -2);
            lua_pop(L, 1);

            if (same) {
                lua_unref(OnSharedSendCallbacks[i].L, OnSharedSendCallbacks[i].ref);
                OnSharedSendCallbacks.erase(OnSharedSendCallbacks.begin() + i);
                lua_pop(L, 1);
                lua_pushboolean(L, true);
                return 1;
            }
        }

        lua_pop(L, 1);
        lua_pushboolean(L, false);
        return 1;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        desyncing = false;

        lua_newtable(L);

        Utils::AddTableFunction(L, "desync", raknet_desync);
        Utils::AddTableFunction(L, "add_send_hook", raknet_add_send_hook);
        Utils::AddTableFunction(L, "remove_send_hook", raknet_remove_send_hook);

        lua_setglobal(L, "raknet");
    }
}
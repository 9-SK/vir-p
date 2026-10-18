-- raknet send hook: print detection flags (ItemType 0x0 - 0x7+)
-- run this in your executor after the game has loaded

local ITEM_NAMES = {
    [0x0] = "End",
    [0x1] = "Delete",
    [0x2] = "New",
    [0x3] = "ChangeProperty",
    [0x4] = "Marker",
    [0x5] = "Ping",
    [0x6] = "PingBack",
    [0x7] = "EventInvocation",
    [0x8] = "RequestCharacter",
    [0x9] = "Rocky",
    [0xA] = "CFrameAck",
    [0xB] = "JoinData",
    [0xC] = "UpdateClientQuota",
    [0xD] = "StreamData",
    [0xE] = "RegionRemoval",
    [0xF] = "InstanceRemoval",
    [0x10] = "Tag",
    [0x11] = "Stats",
    [0x12] = "Hash",
}

local seen = {}

local hook
hook = raknet.add_send_hook(function(pkt)
    if not pkt or not pkt.ItemId then
        return
    end

    local flag = pkt.ItemId
    local hex = string.format("0x%X", flag)
    local name = ITEM_NAMES[flag] or "Unknown"
    local pktHex = string.format("0x%X", pkt.PacketId or 0)

    -- only spam the 0x0-0x7 detection range once each, then print interesting ones
    if flag <= 0x7 then
        if not seen[flag] then
            seen[flag] = true
            print(string.format("[SEND FLAG] %s  %s  size=%d  pkt=%s", hex, name, pkt.Size or 0, pktHex))
        end
    else
        print(string.format("[SEND FLAG] %s  %s  size=%d  pkt=%s", hex, name, pkt.Size or 0, pktHex))
    end
end)

print("[raknet] send flag hook installed")
print("[raknet] watching for flags 0x0 - 0x7 (and everything above)")

-- to remove later:
-- raknet.remove_send_hook(hook)

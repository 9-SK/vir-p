// viridian - Roblox offsets
// client: 0.743.0.7431134 / version-11f155f3450f4c8e (deploy 2026-10-15)
// updated 2026-10-17: Luau/core values from the 10/15 IDA dump (184 offsets);
// engine namespaces cross-checked against theo RbxDumperV2 2.2.4 (392 offsets,
// same build - TaskScheduler::Pointer 0x8d71348 matches).
// Entries marked STALE had no counterpart in either dump and keep their previous
// values - verify against the client before relying on them.
// Previous generation archived in Offsut.hpp.
#pragma once
#include <lua.h>
#include <Windows.h>
#include <cstdint>
#include <memory>

#define Rebase(Address) (Address + reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))

struct LookupResult
{
    std::uint32_t index;
    std::uint8_t missing;
    std::uint8_t reserved[3];
};

struct lua_State;
struct YieldState;
struct YieldingLuaThread;

namespace Offsets {
    const uintptr_t Print = Rebase(0x1CE58F0); // was 0x1C65A30
    const uintptr_t OpcodeLookupTable = Rebase(0x71433E0); // was 0x6F7FC40
    const uintptr_t TaskDefer = Rebase(0x4323E50); // was 0x4305390
    const uintptr_t GetLuaStateForInstance = Rebase(0x41921C0); // STALE - no 0.743 counterpart in dump

    namespace Luau
    {
        const uintptr_t LuaO_NilObject = Rebase(0x667F848); // was 0x6504708
        const uintptr_t LuaH_DummyNode = Rebase(0x667F2E8); // was 0x6500E48
        const uintptr_t LuaD_Throw = Rebase(0x262DDC0); // was 0x259B470
        const uintptr_t Luau_Execute = Rebase(0x265F2B0); // was 0x25CB0D0
        const uintptr_t LuaVM_Load = Rebase(0x41A3610); // was 0x4188AF0
    }

    namespace Signals
    {
        const uintptr_t FireTouchInterest = Rebase(0xA08FC0); // was 0x14DAC20
        const uintptr_t StrictFilter = 0x30B8;
        const uintptr_t EventWhitelist = 0x38;
        const uintptr_t DisconnectConnect = Rebase(0x41547C0); // STALE - no 0.743 counterpart in dump
    }

    namespace Flags
    {
        inline const uintptr_t FireMouseClick = Rebase(0x3BD1060); // was 0x3BC8C60
        inline const uintptr_t FireRightMouseClick = Rebase(0x3BD1200); // was 0x3BC8E00
        inline const uintptr_t FireMouseHoverEnter = Rebase(0x3BCA250); // STALE - no 0.743 counterpart in dump
        inline const uintptr_t FireMouseHoverLeave = Rebase(0x3BCA3F0); // STALE - no 0.743 counterpart in dump
        inline const uintptr_t FireProximityPrompt = Rebase(0x30D1720); // was 0x30E9D90
        const uintptr_t TaskSchedulerTargetFps = Rebase(0x85DCC40); // was 0x83E7808
    }

    namespace DataModel
    {
        const uintptr_t ScriptContext = 0x440;
        const uintptr_t Children = 0x78;
        const uintptr_t GameLoaded = 0x5D0;
        const uintptr_t ClassDescriptor = 0x18;

        const uintptr_t FakeDataModelToDataModel = 0x1F8;
        const uintptr_t FakeDataModelPointer = Rebase(0x8DC8000); // was 0x8BCFD50
    }

    namespace ExtraSpace
    {
        const uintptr_t ScriptContextResume = Rebase(0x4258330); // was 0x423C6F0
        const uintptr_t ScriptContextToResume = 0x8F0;
        const uintptr_t RequireBypass = 0xA81; // was 0x975
    }

    namespace Identity
    {
        const uintptr_t Identity = Rebase(0x835AD38); // STALE - no 0.743 counterpart in dump
        const uintptr_t GetIdentityStruct = Rebase(0x6BB0); // STALE - no 0.743 counterpart in dump
    }

    namespace Capabilities
    {
        const uintptr_t GetCapabilities = Rebase(0x1D241C0); // was 0x1CA23E0
        const uintptr_t Capabilities = 0x30;
    }

    namespace Scripts
    {
        const uintptr_t LocalScriptByteCode = 0x180;
        const uintptr_t ModuleScriptByteCode = 0x128;
        const uintptr_t BytecodeStringOffset = 0x18;
        const uintptr_t ServerContextFlag = 0x130;

        const uintptr_t Pointer = 0x18;
        const uintptr_t Size = 0x28;
    }

    namespace InstanceBridge
    {
        // MISATTRIBUTED (audit 2026-10-18, ref\analysis\offsets_audit.md):
        // 0x417A040 is the Instance::Cache registry-key constant in the
        // 0.741 reference (lfdyn/RobloxExecutor Update.h:106) - NOT a
        // callable. It is called as a function from ~8 sites, every
        // Instance crossing a Lua boundary; a call jumps into whatever
        // code lives there. The real engine Instance->Lua push in the
        // 0.741 reference is Instance::PushInstance 0x7a3390; the 0.743
        // RVA is not yet recovered (dedicated binary hunt queued).
        // Do not call this until the 0.743 value is verified.
        const uintptr_t Push = Rebase(0x417A040); // MISATTRIBUTED - see comment
    }

    namespace TaskScheduler
    {
        const uintptr_t TaskSchedulerPtr = Rebase(0x8D71348); // was 0x8B79128
        const uintptr_t JobStart = 0xC8;
        const uintptr_t JobEnd = 0xD0;
        const uintptr_t JobName = 0x18;
        const uintptr_t JobTypeName = 0xF8;
        const uintptr_t Fps = 0xB0;
    }

    namespace Renderer
    {
        const uintptr_t ViewBase = 0x1E0;
        const uintptr_t Device = 0x8;
        const uintptr_t SwapChain = 0x80;
    }

    namespace ScriptContext
    {
        const uintptr_t RequireBypass = 0xA81; // was 0x975
    }

    namespace ClassDescriptor
    {
        const uintptr_t GetPropertyData = Rebase(0x1D25C30); // was 0x29B8010 - InternName
        // 0.743 dump names: GetPropertyDescriptor = 0x1D223A0, HashLookUp = 0x2A5DC90.
        // FindBucket maps to HashLookUp; if property lookups misbehave at runtime,
        // flip to GetPropertyDescriptor and re-test before touching anything else.
        const uintptr_t GetPropertyDescriptor = Rebase(0x1D223A0);
        const uintptr_t GetProperty = Rebase(0x2A5DC90); // was 0x1CA05A0 - FindBucket
    }

    namespace Properties
    {
        const uintptr_t CallbackOffsetInDescriptor = 0x88;
        const uintptr_t HolderSelfOffset = 0x38;
        const uintptr_t CallbackOwnerOffset = 0x18;
        const uintptr_t CallbackFunctionOffset = 0x38;
        const uintptr_t LiveThreadRefOffset = 0x18;
        const uintptr_t CallbackThreadOffset = 0x28;
        const uintptr_t LuaStateGlobalOffset = 0x18;
        const uintptr_t FunctionRefIdOffset = 0x34;
        const uintptr_t AlternateHolderAdjustment = 0x1F0;
    }

    namespace signal_offsets
    {
        constexpr uintptr_t signature = 0x50;

        namespace connection
        {
            constexpr uintptr_t next = 0x10;
            constexpr uintptr_t enabled = 0x20;
            constexpr uintptr_t slot_wrapper = 0x30;
            constexpr uintptr_t slot_wrapper_self = 0x38;
        }

        namespace slot_wrapper
        {
            constexpr uintptr_t thread = 0x28;
            constexpr uintptr_t function_id = 0x34;
            constexpr uintptr_t weak_object_ref_fallback = 0x38;
            constexpr uintptr_t weak_object_ref = 0x68;
            constexpr uintptr_t weak_object_ref_legacy = 0x48;
        }

        namespace getter
        {
            constexpr uintptr_t chain = 0x68;
            constexpr uintptr_t self = 0x18;
            constexpr uintptr_t container = 0x50;
            constexpr uintptr_t container_thread = 0x28;
        }
    }

    namespace Connection
    {
        inline constexpr uintptr_t enabled = 0x20;
        inline constexpr uintptr_t next = 0x10;
        inline constexpr uintptr_t slot_wrapper = 0x30;
        inline constexpr uintptr_t slot_wrapper_self = 0x38;
    }

    namespace SlotWrapper
    {
        inline constexpr uintptr_t function_id = 0x34;
        inline constexpr uintptr_t thread = 0x28;
        inline constexpr uintptr_t weak_object_ref = 0x68;
    }

    namespace connection = signal_offsets::connection;
    namespace slot_wrapper = signal_offsets::slot_wrapper;

    namespace signal_slot
    {
        inline constexpr uintptr_t is_once = 0xA5;
    }

    namespace reflections
    {
        const uintptr_t CastArgs = Rebase(0x4102410); // was 0x40ED3F0
    }

    namespace Raknet
    {
        const uintptr_t RakPeer = 0x1E0;
    }

    namespace Instance
    {
        inline constexpr uintptr_t PropertyDescriptorBitFlags = 0x94;
        inline constexpr uintptr_t Properties = 0x28;
        inline constexpr uintptr_t ClassDescriptor = 0x18;
        inline constexpr uintptr_t Name = 0x70;
        inline constexpr uintptr_t NameString = 0x8;
        inline constexpr uintptr_t Overlap = 0x1F8;
        inline constexpr uintptr_t Parent = 0x68;
        inline constexpr uintptr_t Children = 0x78;
    }
}

namespace Hyperion {
    inline auto LuaVM_Load = (int(__fastcall*)(lua_State * thread, void* bytecode, const char* chunkName, int env))Offsets::Luau::LuaVM_Load;
    inline auto Luau_Execute = (void(__fastcall*)(lua_State*))Offsets::Luau::Luau_Execute;
    inline auto GetLuaStateForInstance = (lua_State * (__fastcall*)(uint64_t, uint64_t*, uint64_t*))Offsets::GetLuaStateForInstance;

    inline auto ScriptContextResume = (uint64_t(__fastcall*)(uint64_t, YieldState*, YieldingLuaThread**, uint32_t, uint8_t, uint64_t))Offsets::ExtraSpace::ScriptContextResume;
    inline auto LuaD_Throw = (void(__fastcall*)(lua_State*, int))Offsets::Luau::LuaD_Throw;
    inline auto GetIdentityStruct = (uintptr_t(__fastcall*)(uintptr_t))Offsets::Identity::GetIdentityStruct;
    inline auto GetCapabilities = (__int64(__fastcall*)(int*))Offsets::Capabilities::GetCapabilities;
    inline auto PushInstance = (void(__fastcall*)(lua_State*, void*))Offsets::InstanceBridge::Push;

    using InternT = std::uintptr_t(__fastcall*)(const char*);
    using FindBucketT = LookupResult * (__fastcall*)(std::uintptr_t, LookupResult*, const std::uintptr_t*, std::uint32_t);

    inline std::uintptr_t InternName(const char* name)
    {
        const auto function = reinterpret_cast<InternT>(Offsets::ClassDescriptor::GetPropertyData);
        return function(name);
    }

    namespace Signal
    {
        inline auto FireTouchInterest = (void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, bool, bool))Offsets::Signals::FireTouchInterest;
    }

    inline auto Print = (uintptr_t(*)(int, const char*, ...))Offsets::Print;

    inline LookupResult* FindBucket(std::uintptr_t table, LookupResult* output, const std::uintptr_t* key, std::uint32_t mode)
    {
        const auto function = reinterpret_cast<FindBucketT>(Offsets::ClassDescriptor::GetProperty);
        return function(table, output, key, mode);
    }

    using fireclick_t = void(__fastcall*)(void* clickDetector, float distance, void* player);
    inline auto FireMouseClick = reinterpret_cast<fireclick_t>(Offsets::Flags::FireMouseClick);
    inline auto FireRightMouseClick = reinterpret_cast<fireclick_t>(Offsets::Flags::FireRightMouseClick);

    using TaskDeferT = int(__fastcall*)(lua_State*);
    inline TaskDeferT TaskDefer = reinterpret_cast<TaskDeferT>(Offsets::TaskDefer);

    using firehover_t = void(__fastcall*)(void* clickDetector, void* player);
    inline auto FireMouseHover = reinterpret_cast<firehover_t>(Offsets::Flags::FireMouseHoverEnter);
    inline auto FireMouseHoverLeave = reinterpret_cast<firehover_t>(Offsets::Flags::FireMouseHoverLeave);
    inline auto FireProximityPrompt = (void(__thiscall*)(uintptr_t))Offsets::Flags::FireProximityPrompt;
    inline auto FireTouchInterest = (void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, bool, bool))Offsets::Signals::FireTouchInterest;

    inline auto CastArgs = reinterpret_cast<uintptr_t(__fastcall*)(lua_State*, int, void*, bool, int)>(Offsets::reflections::CastArgs);

    namespace InstanceBridge
    {
        inline auto UintPtr = (void(__fastcall*)(lua_State*, std::uintptr_t))Offsets::InstanceBridge::Push;
        inline auto Shared = (void(__fastcall*)(lua_State*, std::shared_ptr<std::uintptr_t*>))Offsets::InstanceBridge::Push;
        inline auto Void = (void(__fastcall*)(lua_State*, void**))Offsets::InstanceBridge::Push;
        inline auto Void2 = (void(__fastcall*)(lua_State*, void*))Offsets::InstanceBridge::Push;
        inline auto Weak = (void(__fastcall*)(lua_State*, std::weak_ptr<std::uintptr_t>))Offsets::InstanceBridge::Push;
    }
}

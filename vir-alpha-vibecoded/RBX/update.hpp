#pragma once

#include <Windows.h>
#include <cstdint>
#include <memory>
#include <string>

// ══════════════════════════════════════════════════════════════════════════════
// EncryptionsHelper.hpp
// ══════════════════════════════════════════════════════════════════════════════

template<typename T>
struct VMValue0
{
private:
    T Storage;

public:
    operator const T() const
    {
        return Storage;
    }

    void operator=(const T& Value)
    {
        Storage = Value;
    }

    const T operator->() const
    {
        return Storage;
    }

    T Get() const
    {
        return Storage;
    }

    void Set(const T& Value)
    {
        Storage = Value;
    }
};

template<typename T>
struct VMValue1
{
private:
    T Storage;

public:
    operator const T() const
    {
        return (T)((uintptr_t)Storage - (uintptr_t)this);
    }

    void operator=(const T& Value)
    {
        Storage = (T)((uintptr_t)Value + (uintptr_t)this);
    }

    const T operator->() const
    {
        return operator const T();
    }

    T Get() const
    {
        return operator const T();
    }

    void Set(const T& Value)
    {
        operator=(Value);
    }
};

template<typename T>
struct VMValue2
{
private:
    T Storage;

public:
    operator const T() const
    {
        return (T)((uintptr_t)this - (uintptr_t)Storage);
    }

    void operator=(const T& Value)
    {
        Storage = (T)((uintptr_t)this - (uintptr_t)Value);
    }

    const T operator->() const
    {
        return operator const T();
    }

    T Get() const
    {
        return operator const T();
    }

    void Set(const T& Value)
    {
        operator=(Value);
    }
};

template<typename T>
struct VMValue3
{
private:
    T Storage;

public:
    operator const T() const
    {
        return (T)((uintptr_t)this ^ (uintptr_t)Storage);
    }

    void operator=(const T& Value)
    {
        Storage = (T)((uintptr_t)Value ^ (uintptr_t)this);
    }

    const T operator->() const
    {
        return operator const T();
    }

    T Get() const
    {
        return operator const T();
    }

    void Set(const T& Value)
    {
        operator=(Value);
    }
};

template<typename T>
struct VMValue4
{
private:
    T Storage;

public:
    operator const T() const
    {
        return (T)((uintptr_t)this + (uintptr_t)Storage);
    }

    void operator=(const T& Value)
    {
        Storage = (T)((uintptr_t)Value - (uintptr_t)this);
    }

    const T operator->() const
    {
        return operator const T();
    }

    T Get() const
    {
        return operator const T();
    }

    void Set(const T& Value)
    {
        operator=(Value);
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// Encryptions.hpp
// ══════════════════════════════════════════════════════════════════════════════
#define CLOSURE_CONT_ENC VMValue3
#define LSTATE_STACKSIZE_ENC VMValue2
#define PROTO_ABSLINEINFO_ENC VMValue1
#define PROTO_DEBUGINSN_ENC VMValue2
#define PROTO_DEBUGNAME_ENC VMValue4
#define PROTO_LINEINFO_ENC VMValue3
#define PROTO_LOCVARS_ENC VMValue1
#define PROTO_SOURCE_ENC VMValue4
#define PROTO_TYPEINFO_ENC VMValue1
#define PROTO_UPVALUES_ENC VMValue3
#define PROTO_USERDATA_ENC VMValue2
#define TSTRING_HASH_ENC VMValue2
#define UDATA_META_ENC VMValue2

#define proto_lineinfo         PROTO_LINEINFO_ENC
#define proto_debuginsn        PROTO_DEBUGINSN_ENC
#define proto_typeinfo         PROTO_TYPEINFO_ENC
#define proto_abslineinfo      PROTO_ABSLINEINFO_ENC
#define proto_source           PROTO_SOURCE_ENC
#define proto_locvars          PROTO_LOCVARS_ENC
#define proto_upvalues         PROTO_UPVALUES_ENC
#define proto_debugname        PROTO_DEBUGNAME_ENC
#define proto_userdata         PROTO_USERDATA_ENC
#define udata_meta             UDATA_META_ENC
#define closure_cont           CLOSURE_CONT_ENC
#define tstring_hash           TSTRING_HASH_ENC
#define lstate_stacksize       LSTATE_STACKSIZE_ENC

// ══════════════════════════════════════════════════════════════════════════════
// Offsets.hpp
// ══════════════════════════════════════════════════════════════════════════════

struct lua_State;
struct YieldState;
struct YieldingLuaThread;

using LuaVMLoad_t = int(__fastcall*)(lua_State* thread, void* bytecode, const char* chunkName, int env);

#define REBASE(Address) (Address + reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))

struct LookupResult
{
    std::uint32_t index;
    std::uint8_t missing;
    std::uint8_t reserved[3];
};

namespace Offsets
{
    const uintptr_t Print = REBASE(0x1cfa0d0);
    const uintptr_t OpcodeLookupTable = REBASE(0x6f1bf50);
    const uintptr_t ScriptContextResume = REBASE(0x429b530);
    const uintptr_t GetLuaStateForInstance = REBASE(0x41F2300);
    const uintptr_t LuaVMLoad = REBASE(0x41E8BD0);
    const uintptr_t luaD_throw = REBASE(0x266EA40);
    const uintptr_t TaskDefer = REBASE(0x435A060);
    const uintptr_t CreateDataModel = REBASE(0x426BC70);

    namespace reflections
    {
        const uintptr_t CastArgs = REBASE(0x414ECD0);
    }

    namespace Signals
    {
        const uintptr_t FireTouchInterest = REBASE(0x1584bc0);
        const uintptr_t DisconnectConnect = REBASE(0x41B40E0);
    }

    namespace Flags
    {
        const uintptr_t FireMouseClick = REBASE(0x3c3edc0);
        const uintptr_t FireRightMouseClick = REBASE(0x3c3ef60);
        const uintptr_t FireMouseHoverEnter = REBASE(0x3c403b0);
        const uintptr_t FireMouseHoverLeave = REBASE(0x3c40550);
        const uintptr_t FireProximityPrompt = REBASE(0x31bd800);
        const uintptr_t TaskSchedulerTargetFps = REBASE(0x82d4a28);
    }

    namespace Luau
    {
        const uintptr_t Luau_Execute = REBASE(0x269e520);
        const uintptr_t LuaO_NilObject = REBASE(0x64ba248);
        const uintptr_t LuaH_DummyNode = REBASE(0x64b68d8);
    }

    namespace DataModel
    {
        const uintptr_t Children = 0x78;
        const uintptr_t GameLoaded = 0x5d0;//xx8
        const uintptr_t ScriptContext = 0x440;
        const uintptr_t ClassDescriptor = 0x18;
        const uintptr_t FakeDataModelToDataModel = 0x1f8;
        const uintptr_t FakeDataModelPointer = REBASE(0x8ee1728);
       
    }

    namespace signal_offsets
    {
        constexpr uintptr_t signature = 0x48;

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


    namespace Extras
    {
        const uintptr_t k_table = REBASE(0x81e9120);
    }

    namespace ExtraSpace
    {
        const uintptr_t RequireBypass = 0xaae;
        const uintptr_t ScriptContextToResume = 0xa38;
    }

    namespace Scripts
    {
        const uintptr_t LocalScriptByteCode = 0x180;
        const uintptr_t ModuleScriptByteCode = 0x128;
    }

    namespace InstanceBridge
    {
        const uintptr_t Push = REBASE(0x41D9F50);
    }

    namespace ClassDescriptor
    {
        const uintptr_t GetPropertyData = REBASE(0x2A86550);
        const uintptr_t GetProperty = REBASE(0x1D32FF0);
    }

    namespace RobloxThread
    {
        const uintptr_t IdentityPtr = REBASE(0x828CA38);
        const uintptr_t GetTlsPointer = REBASE(0x7240);
    }

    namespace Scheduler
    {
        const uintptr_t TaskScheduler = REBASE(0x8c8d108);//?
        const uintptr_t JobStart = 0xC8;
        const uintptr_t JobEnd = 0xD0;
        const uintptr_t JobTypeName = 0xf8;
        const uintptr_t Fps = 0xB0;
    }

    namespace Render
    {
        const uintptr_t RenderJobToRenderView = 0x1d8;
        const uintptr_t RenderViewToRobloxDevice = 0x8;
        const uintptr_t RobloxDeviceToSwapChain = 0xC8;
    }
    namespace Properties
    {
        const uintptr_t CallbackOffsetInDescriptor = 0x80;
        const uintptr_t HolderSelfOffset = 0x38;
        const uintptr_t CallbackOwnerOffset = 0x18;
        const uintptr_t CallbackFunctionOffset = 0x38;
        const uintptr_t LiveThreadRefOffset = 0x18;
        const uintptr_t CallbackThreadOffset = 0x28;
        const uintptr_t LuaStateGlobalOffset = 0x18;
        const uintptr_t FunctionRefIdOffset = 0x34;
        const uintptr_t AlternateHolderAdjustment = 0x1F0;
    }

    
    namespace Capabilities
    {
        const uintptr_t GetCapabilities = REBASE(0x1d34b70);
        const uintptr_t Capabilities = 0x30;
    }
    namespace Instance
    {
        const uintptr_t PropertyDescriptorBitFlags = 0x8C;
        const uintptr_t Properties = 0x28;
        const uintptr_t ScriptableMask = 0x10;
        const uintptr_t ClassDescriptor = 0x18;
        const uintptr_t ClassName = 0x8;
        const uintptr_t Primitive = 0x178;
        const uintptr_t Overlap = 0x1f8;
        const uintptr_t Parent = 0x68;
        const uintptr_t Children = 0x78;
        inline constexpr uintptr_t Name = 0x70;
        inline constexpr uintptr_t NameString = 0x8;
    }
    namespace Raknet
    {
        const uintptr_t RakPeer = 0x1D8;
        const uintptr_t RakPeerVTableSize = 0x3e0;
        const uintptr_t idx_Connect = 13;
        const uintptr_t idx_Send = 20;
        const uintptr_t idx_Receive = 25;
        //const uintptr_t convertItemEnumToString = REBASE(0x460f980);
    }
}

namespace BytecodeOffset
{
    inline constexpr uintptr_t Pointer = 0x10;
    inline constexpr uintptr_t Size = 0x28;
    inline constexpr uintptr_t LocalScriptByteCode = 0x180;
    inline constexpr uintptr_t ModuleScriptByteCode = 0x128;

}

namespace Roblox
{
    inline uintptr_t* k_table = reinterpret_cast<uintptr_t*>(Offsets::Extras::k_table);
    inline auto Print = (uintptr_t(*)(int, const char*, ...))Offsets::Print;
    inline auto Luau_Execute = (void(__fastcall*)(lua_State*))Offsets::Luau::Luau_Execute;
    inline auto GetLuaStateForInstance = (lua_State * (__fastcall*)(uint64_t, uint64_t*, uint64_t*))Offsets::GetLuaStateForInstance;
    inline auto ScriptContextResume = (uint64_t(__fastcall*)(uint64_t, YieldState*, YieldingLuaThread**, uint32_t, uint8_t, uint64_t))Offsets::ScriptContextResume;

    inline LuaVMLoad_t LuaVMLoad = reinterpret_cast<LuaVMLoad_t>(Offsets::LuaVMLoad);

    namespace Signal
    {
        inline auto FireTouchInterest = (void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, bool, bool))Offsets::Signals::FireTouchInterest;
    }

    namespace InstanceBridge
    {
        inline auto UintPtr = (void(__fastcall*)(lua_State*, std::uintptr_t))Offsets::InstanceBridge::Push;
        inline auto SharedInstance = (void(__fastcall*)(lua_State*, std::shared_ptr<std::uintptr_t*>))Offsets::InstanceBridge::Push;
        inline auto Void = (void(__fastcall*)(lua_State*, void**))Offsets::InstanceBridge::Push;
        inline auto Void2 = (void(__fastcall*)(lua_State*, void*))Offsets::InstanceBridge::Push;
        inline auto Weak = (void(__fastcall*)(lua_State*, std::weak_ptr<std::uintptr_t>))Offsets::InstanceBridge::Push;
    }
    using InternT = std::uintptr_t(__fastcall*)(const char*);
    using FindBucketT = LookupResult * (__fastcall*)(std::uintptr_t, LookupResult*, const std::uintptr_t*, std::uint32_t);

    inline std::uintptr_t InternName(const char* name)
    {
        const auto function = reinterpret_cast<InternT>(Offsets::ClassDescriptor::GetPropertyData);
        return function(name);
    }

    inline LookupResult* FindBucket(std::uintptr_t table, LookupResult* output, const std::uintptr_t* key, std::uint32_t mode)
    {
        const auto function = reinterpret_cast<FindBucketT>(Offsets::ClassDescriptor::GetProperty);
        return function(table, output, key, mode);
    }
    using fireclick_t = void(__fastcall*)(void* clickDetector, float distance, void* player);
    inline auto FireMouseClick = reinterpret_cast<fireclick_t>(Offsets::Flags::FireMouseClick);
    inline auto FireRightMouseClick = reinterpret_cast<fireclick_t>(Offsets::Flags::FireRightMouseClick);

    using firehover_t = void(__fastcall*)(void* clickDetector, void* player);
    inline auto FireMouseHover = reinterpret_cast<firehover_t>(Offsets::Flags::FireMouseHoverEnter);
    inline auto FireMouseHoverLeave = reinterpret_cast<firehover_t>(Offsets::Flags::FireMouseHoverLeave);

    inline auto CastArgs = reinterpret_cast<uintptr_t(__fastcall*)(lua_State*, int, void*, bool, int)>(Offsets::reflections::CastArgs);
    inline auto GetCapabilities = (__int64(__fastcall*)(int*))Offsets::Capabilities::GetCapabilities;

    using TGetIdentityStruct = uintptr_t(__fastcall*)(uintptr_t);
    inline auto GetIdentityStruct = reinterpret_cast<TGetIdentityStruct>(Offsets::RobloxThread::GetTlsPointer);

    inline auto FireProximityPrompt = (void(__thiscall*)(uintptr_t))Offsets::Flags::FireProximityPrompt;
    inline auto FireTouchInterest = (void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, bool, bool))Offsets::Signals::FireTouchInterest;

    inline auto PushInstance = (void(__fastcall*)(lua_State*, void*))Offsets::InstanceBridge::Push;
    inline auto DisconnectConnection = (void(__fastcall*)(uintptr_t*))Offsets::Signals::DisconnectConnect;

    using GetPropertyT = uintptr_t(*)(uintptr_t, uintptr_t*);
    using GetPropertyDataT = uintptr_t(*)(uintptr_t, uintptr_t*);

    inline GetPropertyT GetProperty = reinterpret_cast<GetPropertyT>(Offsets::ClassDescriptor::GetProperty);
    inline GetPropertyDataT GetPropertyData = reinterpret_cast<GetPropertyDataT>(Offsets::ClassDescriptor::GetPropertyData);


    inline auto Void2 = reinterpret_cast<void(__fastcall*)(lua_State*, void*)>(Offsets::InstanceBridge::Push);

    using TaskDeferT = int(__fastcall*)(lua_State*);
    inline TaskDeferT TaskDefer = reinterpret_cast<TaskDeferT>(Offsets::TaskDefer);

    using LuaD_ThrowT = void(__fastcall*)(lua_State*, int);
    inline LuaD_ThrowT LuaD_Throw = reinterpret_cast<LuaD_ThrowT>(Offsets::luaD_throw);


}

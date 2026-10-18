#pragma once

#include <lstate.h>
#include <lualib.h>
#include <string>
#include <cstdint>
#include <cstring>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <lstate.h>
#include <lualib.h>
#include <zstd.h>
#include <xxhash.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>

namespace Reflection
{
    struct CallbackRecord
    {
        int FunctionRef = LUA_NOREF;
        uint64_t Generation = 0;
    };

    inline std::unordered_map<uintptr_t, std::unordered_map<uintptr_t, bool>> scriptable_overrides;
    inline std::unordered_map<uintptr_t, std::unordered_map<std::string, CallbackRecord>> callback_values;

    inline bool can_read(uintptr_t address, size_t size = sizeof(uintptr_t)) noexcept
    {
        if (address < 0x10000)
            return false;

        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0)
            return false;

        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return address + size >= address && address + size <= regionEnd;
    }

    inline uintptr_t seh_read_ptr(uintptr_t addr) noexcept {
        __try { return *reinterpret_cast<uintptr_t*>(addr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    inline bool seh_write_u32(uintptr_t addr, uint32_t val) noexcept {
        __try { *reinterpret_cast<uint32_t*>(addr) = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline uint32_t seh_read_u32(uintptr_t addr) noexcept {
        __try { return *reinterpret_cast<uint32_t*>(addr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    template<typename T>
    static bool read_value(std::uintptr_t address, T& result)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        if (!address || !can_read(address, sizeof(T)))
            return false;

        std::memcpy(&result, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    namespace Checker {
        static bool CheckPointer(uintptr_t Address) {
            __try {
                if (Address < 0x10000) {
                    return false;
                }

                MEMORY_BASIC_INFORMATION MemoryInfo = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(Address), &MemoryInfo, sizeof(MemoryInfo)) == 0) {
                    return false;
                }

                if (MemoryInfo.State != MEM_COMMIT) {
                    return false;
                }

                if (MemoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
                    return false;
                }

                return (MemoryInfo.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }
    }

    inline const char* get_property_name(uintptr_t prop)
    {
        if (!prop) return nullptr;

        if (can_read(prop + 0x8, sizeof(uintptr_t)))
        {
            uintptr_t namePtr = seh_read_ptr(prop + 0x8);
            if (namePtr && can_read(namePtr, 1))
            {
                const char* str = reinterpret_cast<const char*>(namePtr);
                if (str[0] >= 0x20 && str[0] <= 0x7E)
                    return str;
            }

            if (can_read(prop + 0x8, 1))
            {
                const char* inlinedStr = reinterpret_cast<const char*>(prop + 0x8);
                if (inlinedStr[0] >= 0x20 && inlinedStr[0] <= 0x7E)
                    return inlinedStr;
            }
        }

        if (can_read(prop + Offsets::Instance::Name, sizeof(uintptr_t)))
        {
            uintptr_t namePtr = seh_read_ptr(prop + Offsets::Instance::Name);
            if (namePtr && can_read(namePtr, 1))
            {
                const char* str = reinterpret_cast<const char*>(namePtr);
                if (str[0] >= 0x20 && str[0] <= 0x7E)
                    return str;
            }
        }

        return nullptr;
    }

    inline uintptr_t get_instance_prop_by_name(lua_State* L, uintptr_t instance, const char* propName)
    {
        if (!instance || !propName || !can_read(instance + Offsets::Instance::ClassDescriptor, sizeof(uintptr_t)))
            return 0;

        uintptr_t classDescriptor = seh_read_ptr(instance + Offsets::Instance::ClassDescriptor);
        if (!classDescriptor)
            return 0;

        uintptr_t super = classDescriptor;
        std::unordered_set<uintptr_t> visited;

        while (super && visited.find(super) == visited.end())
        {
            visited.insert(super);

            if (can_read(super + 0x28, sizeof(uintptr_t) * 2))
            {
                uintptr_t start = seh_read_ptr(super + 0x28);
                uintptr_t end = seh_read_ptr(super + 0x30);

                if (start && end && end >= start && (end - start) <= 4096 * sizeof(uintptr_t))
                {
                    for (uintptr_t cur = start; cur < end; cur += sizeof(uintptr_t))
                    {
                        uintptr_t prop = seh_read_ptr(cur);
                        if (!prop) continue;

                        const char* name = get_property_name(prop);
                        if (name && _stricmp(name, propName) == 0)
                            return prop;
                    }
                }
                else if (start)
                {
                    for (uint32_t i = 0; i < 512; ++i)
                    {
                        uintptr_t entry = start + (i * 0x10);
                        if (!can_read(entry, sizeof(uintptr_t)))
                            break;

                        uintptr_t prop = seh_read_ptr(entry);
                        if (!prop) continue;

                        const char* name = get_property_name(prop);
                        if (name && _stricmp(name, propName) == 0)
                            return prop;
                    }
                }
            }

            if (can_read(super + Offsets::Instance::Properties, sizeof(uintptr_t) * 2))
            {
                uintptr_t start = seh_read_ptr(super + Offsets::Instance::Properties);
                uintptr_t end = seh_read_ptr(super + Offsets::Instance::Properties + 0x8);

                if (start && end && end >= start && (end - start) <= 4096 * sizeof(uintptr_t))
                {
                    for (uintptr_t cur = start; cur < end; cur += sizeof(uintptr_t))
                    {
                        uintptr_t prop = seh_read_ptr(cur);
                        if (!prop) continue;

                        const char* name = get_property_name(prop);
                        if (name && _stricmp(name, propName) == 0)
                            return prop;
                    }
                }
            }

            if (!can_read(super + 0x8, sizeof(uintptr_t)))
                break;
            uintptr_t parent = seh_read_ptr(super + 0x8);
            if (!parent || parent == super)
                break;
            super = parent;
        }

        return 0;
    }

    inline std::vector<uintptr_t> get_all_properties_for_instance(uintptr_t instance)
    {
        std::vector<uintptr_t> props;

        if (!instance || !can_read(instance + Offsets::Instance::ClassDescriptor, sizeof(uintptr_t)))
            return props;

        uintptr_t classDescriptor = seh_read_ptr(instance + Offsets::Instance::ClassDescriptor);
        if (!classDescriptor)
            return props;

        uintptr_t super = classDescriptor;
        std::unordered_set<uintptr_t> visited;

        while (super && visited.find(super) == visited.end())
        {
            visited.insert(super);

            if (can_read(super + 0x28, sizeof(uintptr_t) * 2))
            {
                uintptr_t start = seh_read_ptr(super + 0x28);
                uintptr_t end = seh_read_ptr(super + 0x30);

                if (start && end && end >= start && (end - start) <= 4096 * sizeof(uintptr_t))
                {
                    for (uintptr_t cur = start; cur < end; cur += sizeof(uintptr_t))
                    {
                        uintptr_t prop = seh_read_ptr(cur);
                        if (prop && can_read(prop + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
                        {
                            props.push_back(prop);
                        }
                    }
                }
                else if (start)
                {
                    for (uint32_t i = 0; i < 512; ++i)
                    {
                        uintptr_t entry = start + (i * 0x10);
                        if (!can_read(entry, sizeof(uintptr_t)))
                            break;

                        uintptr_t prop = seh_read_ptr(entry);
                        if (prop && can_read(prop + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
                        {
                            props.push_back(prop);
                        }
                    }
                }
            }

            if (can_read(super + Offsets::Instance::Properties, sizeof(uintptr_t) * 2))
            {
                uintptr_t start = seh_read_ptr(super + Offsets::Instance::Properties);
                uintptr_t end = seh_read_ptr(super + Offsets::Instance::Properties + 0x8);

                if (start && end && end >= start && (end - start) <= 4096 * sizeof(uintptr_t))
                {
                    for (uintptr_t cur = start; cur < end; cur += sizeof(uintptr_t))
                    {
                        uintptr_t prop = seh_read_ptr(cur);
                        if (prop && can_read(prop + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
                        {
                            props.push_back(prop);
                        }
                    }
                }
            }

            if (!can_read(super + 0x8, sizeof(uintptr_t)))
                break;
            uintptr_t parent = seh_read_ptr(super + 0x8);
            if (!parent || parent == super)
                break;
            super = parent;
        }

        return props;
    }

    inline void ResetLibraryState()
    {
        scriptable_overrides.clear();
        callback_values.clear();
    }

    inline void TrackCallbackValue(lua_State* L, int instanceIndex, int propertyIndex, int valueIndex)
    {
        if (!L || !lua_isuserdata(L, instanceIndex) || !lua_isstring(L, propertyIndex) ||
            !lua_isfunction(L, valueIndex))
            return;

        uintptr_t instance = 0;
        void* userdata = lua_touserdata(L, instanceIndex);
        if (!userdata || !can_read(reinterpret_cast<uintptr_t>(userdata)) ||
            !(instance = *reinterpret_cast<uintptr_t*>(userdata)))
            return;

        const char* property = lua_tostring(L, propertyIndex);
        if (!property || !property[0])
            return;

        lua_pushvalue(L, valueIndex);
        const int functionRef = lua_ref(L, -1);
        lua_pop(L, 1);
        callback_values[instance][property] = {
            functionRef,
            Shared::StateGeneration.load()
        };
    }

    inline bool resolve_property(lua_State* L, std::uintptr_t& out_instance, std::uintptr_t& out_prop)
    {
        constexpr std::uintptr_t ClassDescriptorOffset = 0x18;
        constexpr std::uintptr_t PropertyMapOffset = 0x1D8;
        constexpr std::uintptr_t BucketIndicesOffset = 0x1E0;
        constexpr std::uintptr_t PropertyTableOffset = 0x1E8;
        constexpr std::uintptr_t BucketMaskOffset = 0x1F0;
        constexpr std::uintptr_t PropertyEntrySize = 0x10;

        out_instance = 0;
        out_prop = 0;

        if (!lua_isuserdata(L, 1))
            return false;

        void* userdata = lua_touserdata(L, 1);
        if (!userdata || !can_read(reinterpret_cast<std::uintptr_t>(userdata), sizeof(std::uintptr_t)))
            return false;

        if (!read_value(reinterpret_cast<std::uintptr_t>(userdata), out_instance) || !out_instance)
            return false;

        const char* propertyName = lua_tostring(L, 2);
        if (!propertyName || propertyName[0] == '\0')
            return false;

        std::uintptr_t classDescriptor = 0;
        if (read_value(out_instance + ClassDescriptorOffset, classDescriptor) && classDescriptor)
        {
            __try
            {
                std::uintptr_t nameKey = Hyperion::InternName(propertyName);
                if (nameKey)
                {
                    LookupResult lookup{};
                    Hyperion::FindBucket(classDescriptor + PropertyMapOffset, &lookup, &nameKey, 0);
                    if (!lookup.missing)
                    {
                        std::uintptr_t bucketIndices = 0;
                        std::uintptr_t propertyTable = 0;
                        std::uint32_t bucketMask = 0;

                        if (read_value(classDescriptor + BucketIndicesOffset, bucketIndices) && bucketIndices &&
                            read_value(classDescriptor + PropertyTableOffset, propertyTable) && propertyTable &&
                            read_value(classDescriptor + BucketMaskOffset, bucketMask))
                        {
                            std::uint32_t mappedIndex = 0;
                            if (read_value(bucketIndices + static_cast<std::uintptr_t>(lookup.index) * sizeof(std::uint32_t), mappedIndex))
                            {
                                const std::uint32_t propertyIndex = bucketMask & mappedIndex;
                                const std::uintptr_t entry = propertyTable + static_cast<std::uintptr_t>(propertyIndex) * PropertyEntrySize;

                                std::uintptr_t propertyDescriptor = 0;
                                if (read_value(entry, propertyDescriptor) && propertyDescriptor &&
                                    can_read(propertyDescriptor + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
                                {
                                    out_prop = propertyDescriptor;
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        out_prop = get_instance_prop_by_name(L, out_instance, propertyName);
        return (out_prop != 0);
    }

    inline bool read_scriptable_flag(uintptr_t prop) noexcept
    {
        if (!can_read(prop + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
            return false;

        return (seh_read_u32(prop + Offsets::Instance::PropertyDescriptorBitFlags) & 0x10) != 0;
    }

    inline void write_scriptable_flag(uintptr_t prop, bool state) noexcept
    {
        if (!can_read(prop + Offsets::Instance::PropertyDescriptorBitFlags, sizeof(uint32_t)))
            return;

        uint32_t flags = seh_read_u32(prop + Offsets::Instance::PropertyDescriptorBitFlags);
        seh_write_u32(prop + Offsets::Instance::PropertyDescriptorBitFlags, (flags & ~0x10u) | (state ? 0x10u : 0));
    }

    inline int get_property_type(uintptr_t prop) {
        uintptr_t ttype = seh_read_ptr(prop + 0x68);
        if (!ttype || !Checker::CheckPointer(ttype))
            return -1;
        return *reinterpret_cast<int*>(ttype + 0x30);
    }

    inline uintptr_t get_property_getter(uintptr_t prop) {
        uintptr_t get_set_impl = seh_read_ptr(prop + 0x90);
        if (!get_set_impl || !Checker::CheckPointer(get_set_impl))
            return 0;

        uintptr_t get_set_vft = seh_read_ptr(get_set_impl);
        if (!get_set_vft || !Checker::CheckPointer(get_set_vft))
            return 0;

        return seh_read_ptr(get_set_vft + 0x18);
    }

    inline uintptr_t get_property_setter(uintptr_t prop) {
        uintptr_t get_set_impl = seh_read_ptr(prop + 0x90);
        if (!get_set_impl || !Checker::CheckPointer(get_set_impl))
            return 0;

        uintptr_t get_set_vft = seh_read_ptr(get_set_impl);
        if (!get_set_vft || !Checker::CheckPointer(get_set_vft))
            return 0;

        return seh_read_ptr(get_set_vft + 0x20);
    }

    inline int protected_getfield(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* property = luaL_checkstring(L, 2);
        lua_getfield(L, 1, property);
        return 1;
    }

    inline int protected_setfield(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* property = luaL_checkstring(L, 2);
        lua_pushvalue(L, 3);
        lua_setfield(L, 1, property);
        return 0;
    }

    inline bool set_property_value(lua_State* L, int instanceIndex, int propertyIndex, int valueIndex)
    {
        instanceIndex = lua_absindex(L, instanceIndex);
        propertyIndex = lua_absindex(L, propertyIndex);
        valueIndex = lua_absindex(L, valueIndex);
        const int top = lua_gettop(L);

        lua_pushcfunction(L, protected_setfield, "sethiddenproperty_set");
        lua_pushvalue(L, instanceIndex);
        lua_pushvalue(L, propertyIndex);
        lua_pushvalue(L, valueIndex);

        if (lua_pcall(L, 3, 0, 0) != LUA_OK)
        {
            lua_settop(L, top);
            return false;
        }

        lua_settop(L, top);
        return true;
    }

    inline bool call_string_getter_seh(uintptr_t getter_addr, void* getset, void* inst, std::string* output)
    {
        using Fn = void(__fastcall*)(void*, void*, void*);
        __try
        {
            reinterpret_cast<Fn>(getter_addr)(getset, output, inst);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return true;
    }

    inline bool call_content_getter_seh(uintptr_t getterAddress, void* getset, void* instance, std::string& out)
    {
        using GetterFn = void(__fastcall*)(void*, uintptr_t*, void*);

        out.clear();
        uintptr_t sharedString = 0;

        __try
        {
            reinterpret_cast<GetterFn>(getterAddress)(getset, &sharedString, instance);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (!sharedString || !Checker::CheckPointer(sharedString))
            return false;

        constexpr size_t MaximumLength = 64ull * 1024ull * 1024ull;
        size_t length = 0;
        size_t capacity = 0;

        if (!read_value(sharedString + 0x28, length) || !read_value(sharedString + 0x30, capacity))
            return false;

        if (length > MaximumLength)
            return false;

        if (length == 0)
            return true;

        uintptr_t dataAddress = sharedString + 0x18;
        if (capacity > 0xF)
        {
            if (!read_value(sharedString + 0x18, dataAddress) || !dataAddress)
                return false;
        }

        if (dataAddress > UINTPTR_MAX - (length - 1) || !can_read(dataAddress, length))
            return false;

        out.assign(reinterpret_cast<const char*>(dataAddress), length);
        return true;
    }

    inline bool push_property_value_direct_impl(lua_State* L, void* getset, void* inst, uintptr_t getter_addr, int prop_type)
    {
        __try
        {
            switch (prop_type)
            {
            case 1:
            {
                using Fn = bool(__fastcall*)(void*, void*);
                lua_pushboolean(L, reinterpret_cast<Fn>(getter_addr)(getset, inst) ? 1 : 0);
                return true;
            }
            case 2:
            {
                using Fn = int(__fastcall*)(void*, void*);
                lua_pushinteger(L, reinterpret_cast<Fn>(getter_addr)(getset, inst));
                return true;
            }
            case 4:
            {
                using Fn = float(__fastcall*)(void*, void*);
                lua_pushnumber(L, reinterpret_cast<Fn>(getter_addr)(getset, inst));
                return true;
            }
            case 5:
            {
                using Fn = double(__fastcall*)(void*, void*);
                lua_pushnumber(L, reinterpret_cast<Fn>(getter_addr)(getset, inst));
                return true;
            }
            case 29:
            {
                using Fn = void(__fastcall*)(void*, void*, void*);
                uint64_t tmp = 0;
                reinterpret_cast<Fn>(getter_addr)(getset, &tmp, inst);
                lua_pushinteger(L, static_cast<int>(static_cast<uint32_t>(tmp)));
                return true;
            }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }
    static bool string_payload_readable(const char* data, size_t length)
    {
        if (length == 0) return true;
        if (!data) return false;
        bool ok = false;
        __try {
            volatile char sink = 0;
            sink = static_cast<char>(sink ^ data[0] ^ data[length - 1] ^ data[(length - 1) / 2]);
            (void)sink;
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        return ok;
    }

    inline bool push_string_property(lua_State* L, uintptr_t getter_addr, void* getset, void* inst)
    {
        constexpr size_t kMaxStringBytes = 64ull * 1024ull * 1024ull;
        std::string str_result;
        if (!call_string_getter_seh(getter_addr, getset, inst, &str_result))
            return false;

        const size_t length = str_result.size();
        if (length > str_result.capacity() || length > kMaxStringBytes)
            return false;

        if (!string_payload_readable(str_result.data(), length))
            return false;

        lua_pushlstring(L, str_result.data(), length);
        return true;
    }
    inline bool push_property_value_direct(lua_State* L, uintptr_t instance, uintptr_t prop_desc)
    {
        uintptr_t getter_addr = get_property_getter(prop_desc);
        if (!getter_addr)
            return false;

        int prop_type = get_property_type(prop_desc);
        if (prop_type < 0)
            return false;

        uintptr_t get_set_impl = seh_read_ptr(prop_desc + 0x90);
        if (!get_set_impl || !Checker::CheckPointer(get_set_impl))
            return false;

        void* getset = reinterpret_cast<void*>(get_set_impl);
        void* inst = reinterpret_cast<void*>(instance);

        if (prop_type == 6 || prop_type == 30)
        {
            return push_string_property(L, getter_addr, getset, inst);
        }

        if (prop_type == 63)
        {
            std::string strResult;
            if (!call_content_getter_seh(getter_addr, getset, inst, strResult))
                return false;
            lua_pushlstring(L, strResult.data(), strResult.size());
            return true;
        }

        return push_property_value_direct_impl(L, getset, inst, getter_addr, prop_type);
    }

    inline int isscriptable(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* prop_name = luaL_checkstring(L, 2);

        uintptr_t inst = 0, prop = 0;
        if (!resolve_property(L, inst, prop) || !prop) {
            lua_pushboolean(L, false);
            return 1;
        }

        auto instIt = scriptable_overrides.find(inst);
        if (instIt != scriptable_overrides.end()) {
            auto propIt = instIt->second.find(prop);
            if (propIt != instIt->second.end()) {
                lua_pushboolean(L, propIt->second);
                return 1;
            }
        }

        lua_pushboolean(L, read_scriptable_flag(prop));
        return 1;
    }

    inline int setscriptable(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* prop_name = luaL_checkstring(L, 2);
        luaL_checktype(L, 3, LUA_TBOOLEAN);

        uintptr_t inst = 0, prop = 0;
        if (!resolve_property(L, inst, prop) || !prop) {
            lua_pushboolean(L, false);
            return 1;
        }

        bool wasScriptable = read_scriptable_flag(prop);
        auto instIt = scriptable_overrides.find(inst);
        if (instIt != scriptable_overrides.end()) {
            auto propIt = instIt->second.find(prop);
            if (propIt != instIt->second.end()) {
                wasScriptable = propIt->second;
            }
        }

        bool state = lua_toboolean(L, 3) != 0;
        write_scriptable_flag(prop, state);
        scriptable_overrides[inst][prop] = state;

        lua_pushboolean(L, wasScriptable);
        return 1;
    }
    int gethiddenproperty(lua_State* L)
    {

        constexpr int BinaryStringType = 30;
        constexpr int StringType = 6;

        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* propertyName = luaL_checkstring(L, 2);

        if (std::strcmp(propertyName, ("siz")) == 0)
        {
            lua_getfield(L, 1, ("Size"));
            lua_pushboolean(L, true);
            return 2;
        }

        uintptr_t instance = 0;
        uintptr_t propertyDescriptor = 0;

        if (!resolve_property(L, instance, propertyDescriptor) || !instance || !propertyDescriptor)
        {
            lua_pushnil(L);
            return 1;
        }

        const bool hidden = !read_scriptable_flag(propertyDescriptor);

        if (push_property_value_direct(L, instance, propertyDescriptor))
        {
            lua_pushboolean(L, hidden);
            return 2;
        }

        uint32_t originalFlags = 0;
        if (!read_value(propertyDescriptor + Offsets::Instance::PropertyDescriptorBitFlags, originalFlags))
        {
            lua_pushnil(L);
            lua_pushboolean(L, hidden);
            return 2;
        }

        const uintptr_t flagsAddress = propertyDescriptor + Offsets::Instance::PropertyDescriptorBitFlags;
        const uintptr_t typeDescriptor = seh_read_ptr(propertyDescriptor + 0x68);
        int originalType = -1;
        uintptr_t typeAddress = 0;
        if (typeDescriptor && Checker::CheckPointer(typeDescriptor))
        {
            typeAddress = typeDescriptor + 0x30;
            read_value(typeAddress, originalType);
        }

        bool flagsChanged = false;
        bool typeChanged = false;

        __try
        {
            *reinterpret_cast<uint32_t*>(flagsAddress) = originalFlags | 0x10;
            flagsChanged = true;

            if (originalType == BinaryStringType && typeAddress)
            {
                *reinterpret_cast<int*>(typeAddress) = StringType;
                typeChanged = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        int top = lua_gettop(L);
        lua_pushcfunction(L, protected_getfield, ("gethiddenproperty"));
        lua_pushvalue(L, 1);
        lua_pushstring(L, propertyName);

        int status = lua_pcall(L, 2, 1, 0);

        __try
        {
            if (typeChanged && typeAddress)
                *reinterpret_cast<int*>(typeAddress) = originalType;

            if (flagsChanged)
                *reinterpret_cast<uint32_t*>(flagsAddress) = originalFlags;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        if (status != LUA_OK)
        {
            lua_settop(L, top);
            lua_pushnil(L);
            lua_pushboolean(L, hidden);
            return 2;
        }

        if (lua_type(L, top + 1) == LUA_TBUFFER)
        {
            size_t bufferLength = 0;
            void* bufferData = lua_tobuffer(L, top + 1, &bufferLength);
            const char* bufferText = static_cast<const char*>(bufferData);
            if (!bufferText)
            {
                bufferText = "";
                bufferLength = 0;
            }
            lua_pushlstring(L, bufferText, bufferLength);
            lua_replace(L, top + 1);
        }

        lua_pushboolean(L, hidden);
        return 2;
    }


    inline int sethiddenproperty(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* propertyName = luaL_checkstring(L, 2);
        luaL_checkany(L, 3);

        uintptr_t instance = 0;
        uintptr_t propertyDescriptor = 0;

        if (!resolve_property(L, instance, propertyDescriptor) || !instance || !propertyDescriptor)
        {
            bool didSet = set_property_value(L, 1, 2, 3);
            lua_pushboolean(L, false);
            return 1;
        }

        uint32_t originalFlags = 0;
        if (!read_value(propertyDescriptor + Offsets::Instance::PropertyDescriptorBitFlags, originalFlags))
        {
            lua_pushboolean(L, false);
            return 1;
        }

        const bool wasScriptable = (originalFlags & 0x10) != 0;
        const uintptr_t flagsAddress = propertyDescriptor + Offsets::Instance::PropertyDescriptorBitFlags;

        bool flagsChanged = false;
        if (!wasScriptable)
        {
            __try
            {
                *reinterpret_cast<uint32_t*>(flagsAddress) = originalFlags | 0x10;
                flagsChanged = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        bool didSet = set_property_value(L, 1, 2, 3);

        if (flagsChanged)
        {
            __try
            {
                *reinterpret_cast<uint32_t*>(flagsAddress) = originalFlags;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        lua_pushboolean(L, !wasScriptable && didSet);
        return 1;
    }

    inline int gethiddenproperties(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        void* userdata = lua_touserdata(L, 1);
        if (!userdata || !can_read(reinterpret_cast<uintptr_t>(userdata)))
        {
            lua_newtable(L);
            return 1;
        }

        uintptr_t inst = *reinterpret_cast<uintptr_t*>(userdata);
        if (!inst)
        {
            lua_newtable(L);
            return 1;
        }

        std::vector<uintptr_t> all_props = get_all_properties_for_instance(inst);

        lua_newtable(L);
        int result_table = lua_gettop(L);
        std::unordered_set<std::string> added;

        for (uintptr_t prop_desc : all_props)
        {
            const char* prop_name = get_property_name(prop_desc);
            if (!prop_name || !prop_name[0])
                continue;

            if (added.find(prop_name) != added.end())
                continue;

            bool is_scriptable = read_scriptable_flag(prop_desc);
            if (is_scriptable)
                continue;

            added.insert(prop_name);

            lua_pushcfunction(L, gethiddenproperty, "gethiddenproperty");
            lua_pushvalue(L, 1);
            lua_pushstring(L, prop_name);

            if (lua_pcall(L, 2, 2, 0) == LUA_OK)
            {
                bool is_hidden = lua_toboolean(L, -1);
                lua_pop(L, 1);

                if (!lua_isnil(L, -1))
                {
                    lua_setfield(L, result_table, prop_name);
                }
                else
                {
                    lua_pop(L, 1);
                }
            }
            else
            {
                lua_pop(L, 1);
            }
        }

        return 1;
    }

    inline int getproperties(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        void* userdata = lua_touserdata(L, 1);
        if (!userdata || !can_read(reinterpret_cast<uintptr_t>(userdata)))
        {
            lua_newtable(L);
            return 1;
        }

        uintptr_t inst = *reinterpret_cast<uintptr_t*>(userdata);
        if (!inst)
        {
            lua_newtable(L);
            return 1;
        }

        std::vector<uintptr_t> all_props = get_all_properties_for_instance(inst);

        lua_newtable(L);
        int result_table = lua_gettop(L);
        std::unordered_set<std::string> added;

        for (uintptr_t prop_desc : all_props)
        {
            const char* prop_name = get_property_name(prop_desc);
            if (!prop_name || !prop_name[0])
                continue;

            if (added.find(prop_name) != added.end())
                continue;
            added.insert(prop_name);

            int top = lua_gettop(L);
            lua_pushcfunction(L, protected_getfield, "getproperties_field");
            lua_pushvalue(L, 1);
            lua_pushstring(L, prop_name);
            if (lua_pcall(L, 2, 1, 0) == LUA_OK)
            {
                if (!lua_isnil(L, -1))
                {
                    lua_setfield(L, result_table, prop_name);
                }
                else
                {
                    lua_pop(L, 1);
                }
            }
            else
            {
                lua_settop(L, top);
            }
        }

        return 1;
    }

    int getcallbackvalue(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TSTRING);

        uintptr_t TrackedInstance = 0;
        void* Userdata = lua_touserdata(L, 1);

        if (Userdata)
            read_value(reinterpret_cast<uintptr_t>(Userdata), TrackedInstance);

        const char* PropertyName = lua_tostring(L, 2);

        if (TrackedInstance && PropertyName)
        {
            const auto InstanceIt = callback_values.find(TrackedInstance);

            if (InstanceIt != callback_values.end())
            {
                const auto CallbackIt = InstanceIt->second.find(PropertyName);

                if (CallbackIt != InstanceIt->second.end() && CallbackIt->second.Generation == Shared::StateGeneration.load())
                {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, CallbackIt->second.FunctionRef);

                    if (lua_isfunction(L, -1))
                        return 1;

                    lua_pop(L, 1);
                }
            }
        }

        uintptr_t Instance = 0;
        uintptr_t Descriptor = 0;

        if (!resolve_property(L, Instance, Descriptor))
            luaL_argerror(L, 2, "callback property does not exist");

        int32_t RelativeOffset = 0;

        if (!read_value(Descriptor + Offsets::Properties::CallbackOffsetInDescriptor, RelativeOffset) || RelativeOffset == 0)
        {
            lua_pushnil(L);
            return 1;
        }

        uintptr_t Holder = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(Instance) + static_cast<std::intptr_t>(RelativeOffset));

        uintptr_t HolderSelf = 0;

        uintptr_t CurrentGlobal = 0;

        if (!read_value(Holder + Offsets::Properties::HolderSelfOffset, HolderSelf))
        {
            lua_pushnil(L);
            return 1;
        }

        if (HolderSelf != Holder)
        {
            if (Holder < Offsets::Properties::AlternateHolderAdjustment)
            {
                lua_pushnil(L);
                return 1;
            }

            Holder -= Offsets::Properties::AlternateHolderAdjustment;

            if (!read_value(Holder + Offsets::Properties::HolderSelfOffset, HolderSelf) || HolderSelf != Holder)
            {
                lua_pushnil(L);
                return 1;
            }
        }

        uintptr_t Owner = 0;
        uintptr_t Callback = 0;
        uintptr_t LiveThreadRef = 0;
        uintptr_t CallbackThread = 0;
        uintptr_t CallbackGlobal = 0;
        uint32_t FunctionRef = 0;

        if (!read_value(Holder + Offsets::Properties::CallbackOwnerOffset, Owner) || !Owner ||
            !read_value(Owner + Offsets::Properties::CallbackFunctionOffset, Callback) || !Callback ||
            !read_value(Callback + Offsets::Properties::LiveThreadRefOffset, LiveThreadRef) || !LiveThreadRef ||
            !read_value(LiveThreadRef + Offsets::Properties::CallbackThreadOffset, CallbackThread) || !CallbackThread ||
            !read_value(CallbackThread + Offsets::Properties::LuaStateGlobalOffset, CallbackGlobal) ||
            !read_value(reinterpret_cast<uintptr_t>(L) + Offsets::Properties::LuaStateGlobalOffset, CurrentGlobal) ||
            CallbackGlobal != CurrentGlobal ||
            !read_value(LiveThreadRef + Offsets::Properties::FunctionRefIdOffset, FunctionRef) || FunctionRef == 0)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, static_cast<int>(FunctionRef));

        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            lua_pushnil(L);
        }

        return 1;
    }

    inline int setcallbackvalue(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        const char* prop = luaL_checkstring(L, 2);
        luaL_checktype(L, 3, LUA_TFUNCTION);

        int top = lua_gettop(L);
        lua_pushstring(L, "__callbacks");
        lua_rawget(L, LUA_REGISTRYINDEX);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);

            lua_newtable(L);
            lua_pushstring(L, "k");
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);

            lua_pushstring(L, "__callbacks");
            lua_pushvalue(L, -2);
            lua_rawset(L, LUA_REGISTRYINDEX);
        }

        lua_pushvalue(L, 1);
        lua_rawget(L, -2);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);

            lua_pushvalue(L, 1);
            lua_pushvalue(L, -2);
            lua_rawset(L, -4);
        }

        lua_pushstring(L, prop);
        lua_pushvalue(L, 3);
        lua_rawset(L, -3);

        TrackCallbackValue(L, 1, 2, 3);

        lua_settop(L, top);
        return 0;
    }

    inline int getpcd(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TUSERDATA);


        lua_pushstring(L, "PhysicalConfigData");


        return gethiddenproperty(L);
    }

    inline void RegisterLibrary(lua_State* L)
    {
        if (!L) return;
        Utils::AddFunction(L, "getpcd", getpcd);
        Utils::AddFunction(L, "isscriptable", isscriptable);
        Utils::AddFunction(L, "is_scriptable", isscriptable);

        Utils::AddFunction(L, "setscriptable", setscriptable);
        Utils::AddFunction(L, "set_scriptable", setscriptable);

        Utils::AddFunction(L, "gethiddenproperty", gethiddenproperty);
        Utils::AddFunction(L, "get_hidden_property", gethiddenproperty);
        Utils::AddFunction(L, "gethiddenprop", gethiddenproperty);
        Utils::AddFunction(L, "get_hidden_prop", gethiddenproperty);

        Utils::AddFunction(L, "sethiddenproperty", sethiddenproperty);
        Utils::AddFunction(L, "set_hidden_property", sethiddenproperty);
        Utils::AddFunction(L, "sethiddenprop", sethiddenproperty);
        Utils::AddFunction(L, "set_hidden_prop", sethiddenproperty);

        Utils::AddFunction(L, "gethiddenproperties", gethiddenproperties);
        Utils::AddFunction(L, "get_hidden_properties", gethiddenproperties);

        Utils::AddFunction(L, "getproperties", getproperties);
        Utils::AddFunction(L, "get_properties", getproperties);

        Utils::AddFunction(L, "getcallbackvalue", getcallbackvalue);
        Utils::AddFunction(L, "get_callback_value", getcallbackvalue);
        Utils::AddFunction(L, "getcallbackmember", getcallbackvalue);
        Utils::AddFunction(L, "get_callback_member", getcallbackvalue);

        Utils::AddFunction(L, "setcallbackvalue", setcallbackvalue);
        Utils::AddFunction(L, "set_callback_value", setcallbackvalue);
        Utils::AddFunction(L, "setcallbackmember", setcallbackvalue);
        Utils::AddFunction(L, "set_callback_member", setcallbackvalue);
    }
}
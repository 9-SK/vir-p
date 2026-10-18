#pragma once

#include <lstate.h>
#include <lualib.h>
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>

namespace Reflection
{
    static int funcnameinc = 0;

    struct CallbackRecord
    {
        int FunctionRef = LUA_NOREF;
        uint64_t Generation = 0;
    };

    extern std::unordered_map<uintptr_t, std::unordered_map<uintptr_t, bool>> scriptable_overrides;
    extern std::unordered_map<uintptr_t, std::unordered_map<std::string, CallbackRecord>> callback_values;

    bool can_read(uintptr_t address, size_t size = sizeof(uintptr_t)) noexcept;

    uintptr_t seh_read_ptr(uintptr_t addr) noexcept;
    bool seh_write_u32(uintptr_t addr, uint32_t val) noexcept;
    uint32_t seh_read_u32(uintptr_t addr) noexcept;

    template<typename T>
    static bool read_value(std::uintptr_t address, T& result);

    namespace Checker {
        static bool CheckPointer(uintptr_t Address);
    }

    const char* get_property_name(uintptr_t prop);
    uintptr_t get_instance_prop_by_name(lua_State* L, uintptr_t instance, const char* propName);
    std::vector<uintptr_t> get_all_properties_for_instance(uintptr_t instance);
    void ResetLibraryState();
    void TrackCallbackValue(lua_State* L, int instanceIndex, int propertyIndex, int valueIndex);
    bool resolve_property(lua_State* L, std::uintptr_t& out_instance, std::uintptr_t& out_prop);
    bool read_scriptable_flag(uintptr_t prop) noexcept;
    void write_scriptable_flag(uintptr_t prop, bool state) noexcept;
    int get_property_type(uintptr_t prop);
    uintptr_t get_property_getter(uintptr_t prop);
    uintptr_t get_property_setter(uintptr_t prop);

    int protected_getfield(lua_State* L);
    int protected_setfield(lua_State* L);
    bool set_property_value(lua_State* L, int instanceIndex, int propertyIndex, int valueIndex);
    bool call_string_getter_seh(uintptr_t getter_addr, void* getset, void* inst, std::string* output);
    bool call_content_getter_seh(uintptr_t getterAddress, void* getset, void* instance, std::string& out);
    bool push_property_value_direct_impl(lua_State* L, void* getset, void* inst, uintptr_t getter_addr, int prop_type);
    bool push_property_value_direct(lua_State* L, uintptr_t instance, uintptr_t prop_desc);

    int isscriptable(lua_State* L);
    int setscriptable(lua_State* L);
    int gethiddenproperty(lua_State* L);
    int sethiddenproperty(lua_State* L);
    int gethiddenproperties(lua_State* L);
    int getproperties(lua_State* L);
    int getcallbackvalue(lua_State* L);
    int setcallbackvalue(lua_State* L);
    int getpcd(lua_State* L);

    void register_lib(lua_State* L);
}

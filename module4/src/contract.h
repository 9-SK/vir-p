#pragma once
#include <cstdint>

namespace rv {

    constexpr uint64_t FakeDataModel       = 0x8DC8000;
    constexpr uint64_t DataModelPtr        = 0x1F8;
    constexpr uint64_t GameLoadedSentinel  = 0x5D0;
    constexpr uint32_t GameLoadedValue     = 31;
    constexpr uint64_t ScriptContextOff    = 0x440;
    constexpr uint64_t EncStateOff         = 0x400;
    constexpr uint64_t LuaONilObject       = 0x667F848;
    constexpr uint64_t LuaHDummyNode       = 0x667F2E8;

    constexpr const char* runtime_dir      = "viridian";
    constexpr const char* file_execute     = "execute.lua";
    constexpr const char* file_status      = "status.txt";
    constexpr const char* file_bootlog     = "boot.log";

}

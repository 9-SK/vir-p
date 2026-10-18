










#pragma once
#include <cstdint>

namespace rv {

    
    
    constexpr uint64_t real_entry          = 0x31ebbc; 
    constexpr uint64_t real_crt_startup    = 0x31ea94; 
    constexpr uint64_t real_dllmain        = 0x17e650; 
    constexpr uint64_t real_threadproc     = 0x182360; 
    constexpr uint64_t real_init           = 0x17d490; 
    constexpr uint64_t real_cookie_gate    = 0x17e570; 
    constexpr uint64_t real_tls_gate       = 0x26cce0; 
    constexpr uint64_t real_blob_decrypt   = 0x17e190; 

    
    constexpr uint64_t real_bias_global    = 0x180435278; 
    constexpr uint64_t real_resolver_lo    = 0x2a00;      
    constexpr uint64_t real_resolver_hi    = 0x6630;
    constexpr uint64_t real_slot_block_lo  = 0x435128;    
    constexpr uint64_t real_slot_block_hi  = 0x434fe8 + 0x184; 
    constexpr uint64_t real_dispatch0      = 0x180435050; 
    
    

    
    
    constexpr uint64_t FakeDataModel       = 0x8DC8000;
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

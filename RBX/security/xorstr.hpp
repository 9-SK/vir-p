#pragma once

// Compile-time XOR string obfuscation (minimal implementation)
// xorstr("hello") returns a const char* with runtime-decrypted string

#include <array>
#include <cstdint>
#include <cstddef>
#include <Oxorany/Oxorany.h>

namespace xorstr_detail
{
    constexpr uint64_t key = 0x5A5A5A5A5A5A5A5Aull;

    template<size_t N>
    struct encrypted_string
    {
        mutable char data[N];
        mutable bool decrypted = false;

        constexpr encrypted_string(const char (&str)[N]) : data{}, decrypted(false)
        {
            for (size_t i = 0; i < N; ++i)
                data[i] = str[i] ^ static_cast<char>((key >> ((i % 8) * 8)) & 0xFF);
        }

        const char* decrypt() const
        {
            if (!decrypted)
            {
                for (size_t i = 0; i < N; ++i)
                    data[i] ^= static_cast<char>((key >> ((i % 8) * 8)) & 0xFF);
                decrypted = true;
            }
            return data;
        }
    };
}

#define XORSTR(str) (xorstr_detail::encrypted_string<sizeof(str)>(str).decrypt())
#define xorstr(str) XORSTR(str)

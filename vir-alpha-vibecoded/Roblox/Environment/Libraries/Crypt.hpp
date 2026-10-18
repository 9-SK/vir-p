#pragma once

#include <lstate.h>
#include <lualib.h>

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <Windows.h>
#include <wincrypt.h>
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

#pragma comment(lib, "Advapi32.lib")
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sha3_impl {

    static const uint64_t RC[24] = {
        0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808aULL,
        0x8000000080008000ULL,0x000000000000808bULL,0x0000000080000001ULL,
        0x8000000080008081ULL,0x8000000000008009ULL,0x000000000000008aULL,
        0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000aULL,
        0x000000008000808bULL,0x800000000000008bULL,0x8000000000008089ULL,
        0x8000000000008003ULL,0x8000000000008002ULL,0x8000000000000080ULL,
        0x000000000000800aULL,0x800000008000000aULL,0x8000000080008081ULL,
        0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL
    };

    static inline uint64_t rol64(uint64_t x, int n) {
        return (x << n) | (x >> (64 - n));
    }

    static void keccakf(uint64_t s[25]) {
        static const int pi[24] = { 10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1 };
        static const int ro[24] = { 1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44 };
        for (int r = 0; r < 24; r++) {
            uint64_t bc[5], t, tmp;
            for (int i = 0; i < 5; i++)
                bc[i] = s[i] ^ s[i + 5] ^ s[i + 10] ^ s[i + 15] ^ s[i + 20];
            for (int i = 0; i < 5; i++) {
                t = bc[(i + 4) % 5] ^ rol64(bc[(i + 1) % 5], 1);
                for (int j = 0; j < 25; j += 5) s[j + i] ^= t;
            }
            t = s[1];
            for (int i = 0; i < 24; i++) { tmp = s[pi[i]]; s[pi[i]] = rol64(t, ro[i]); t = tmp; }
            for (int j = 0; j < 25; j += 5) {
                uint64_t t0 = s[j], t1 = s[j + 1], t2 = s[j + 2], t3 = s[j + 3], t4 = s[j + 4];
                s[j] = t0 ^ (~t1 & t2); s[j + 1] = t1 ^ (~t2 & t3); s[j + 2] = t2 ^ (~t3 & t4);
                s[j + 3] = t3 ^ (~t4 & t0); s[j + 4] = t4 ^ (~t0 & t1);
            }
            s[0] ^= RC[r];
        }
    }

    inline std::string sha3(int bits, const char* data, size_t len) {
        int rate = (1600 - bits * 2) / 8;
        uint64_t st[25] = {};
        uint8_t* sb = reinterpret_cast<uint8_t*>(st);
        size_t off = 0;
        while (off < len) {
            size_t blk = (len - off < (size_t)rate) ? (len - off) : (size_t)rate;
            for (size_t i = 0; i < blk; i++) sb[i] ^= ((const uint8_t*)data)[off + i];
            off += blk;
            if (blk == (size_t)rate) keccakf(st);
        }
        sb[off % rate] ^= 0x06;
        sb[rate - 1] ^= 0x80;
        keccakf(st);
        int outb = bits / 8;
        static constexpr char hx[] = "0123456789abcdef";
        std::string out; out.reserve(outb * 2);
        for (int i = 0; i < outb; i++) { out += hx[sb[i] >> 4]; out += hx[sb[i] & 0xf]; }
        return out;
    }

} // namespace sha3_impl

namespace sha224_impl {

    static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static inline uint32_t S0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
    static inline uint32_t S1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
    static inline uint32_t s0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
    static inline uint32_t s1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    inline std::string sha224(const char* data, size_t len) {
        uint32_t h[8] = {
            0xc1059ed8,0x367cd507,0x3070dd17,0xf70e5939,
            0xffc00b31,0x68581511,0x64f98fa7,0xbefa4fa4
        };
        uint64_t bitlen = (uint64_t)len * 8;
        size_t padlen = (len % 64 < 56) ? (56 - len % 64) : (120 - len % 64);
        size_t total = len + padlen + 8;
        std::vector<uint8_t> msg(total, 0);
        memcpy(msg.data(), data, len);
        msg[len] = 0x80;
        for (int i = 0; i < 8; i++)
            msg[total - 8 + i] = (uint8_t)(bitlen >> (56 - i * 8));
        for (size_t i = 0; i < total; i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; j++)
                w[j] = ((uint32_t)msg[i + j * 4] << 24) | ((uint32_t)msg[i + j * 4 + 1] << 16) |
                ((uint32_t)msg[i + j * 4 + 2] << 8) | (uint32_t)msg[i + j * 4 + 3];
            for (int j = 16; j < 64; j++)
                w[j] = s1(w[j - 2]) + w[j - 7] + s0(w[j - 15]) + w[j - 16];
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int j = 0; j < 64; j++) {
                uint32_t t1 = hh + S1(e) + Ch(e, f, g) + K[j] + w[j];
                uint32_t t2 = S0(a) + Maj(a, b, c);
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a;h[1] += b;h[2] += c;h[3] += d;h[4] += e;h[5] += f;h[6] += g;h[7] += hh;
        }
        static constexpr char hx[] = "0123456789abcdef";
        std::string out; out.reserve(56);
        for (int i = 0; i < 7; i++)
            for (int j = 24; j >= 0; j -= 8) {
                out += hx[(h[i] >> (j + 4)) & 0xf];
                out += hx[(h[i] >> j) & 0xf];
            }
        return out;
    }

} // namespace sha224_impl
namespace sha_detail {
    static const uint64_t K384[80] = {
        0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
        0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
        0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
        0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL,
    };
    inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
    inline uint64_t ch(uint64_t e, uint64_t f, uint64_t g) { return (e & f) ^ (~e & g); }
    inline uint64_t maj(uint64_t a, uint64_t b, uint64_t c) { return (a & b) ^ (a & c) ^ (b & c); }
    inline uint64_t s0(uint64_t a) { return rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39); }
    inline uint64_t s1(uint64_t e) { return rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41); }
    inline uint64_t o0(uint64_t w) { return rotr64(w, 1) ^ rotr64(w, 8) ^ (w >> 7); }
    inline uint64_t o1(uint64_t w) { return rotr64(w, 19) ^ rotr64(w, 61) ^ (w >> 6); }
}

inline std::string sha384(const std::string& msg) {
    using namespace sha_detail;
    uint64_t h[8] = {
        0xcbbb9d5dc1059ed8ULL,0x629a292a367cd507ULL,0x9159015a3070dd17ULL,0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL,0x8eb44a8768581511ULL,0xdb0c2e0d64f98fa7ULL,0x47b5481dbefa4fa4ULL,
    };

    uint64_t bitlen = (uint64_t)msg.size() * 8;
    std::vector<uint8_t> data(msg.begin(), msg.end());
    data.push_back(0x80);
    while (data.size() % 128 != 112) data.push_back(0x00);
    for (int i = 7; i >= 0; --i) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back((uint8_t)(bitlen >> (i * 8)));

    for (size_t chunk = 0; chunk < data.size(); chunk += 128) {
        uint64_t w[80] = {};
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j)
                w[i] = (w[i] << 8) | data[chunk + i * 8 + j];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = o1(w[i - 2]) + w[i - 7] + o0(w[i - 15]) + w[i - 16];

        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = hh + s1(e) + ch(e, f, g) + K384[i] + w[i];
            uint64_t t2 = s0(a) + maj(a, b, c);
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    char buf[97];
    snprintf(buf, sizeof(buf),
        "%016llx%016llx%016llx%016llx%016llx%016llx",
        (unsigned long long)h[0], (unsigned long long)h[1], (unsigned long long)h[2],
        (unsigned long long)h[3], (unsigned long long)h[4], (unsigned long long)h[5]);
    return std::string(buf);
}

namespace Crypt
{
    inline bool random_bytes(std::string& out, size_t size)
    {
        out.resize(size);
        HCRYPTPROV provider = 0;
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;
        const BOOL ok = CryptGenRandom(provider, static_cast<DWORD>(size),
            reinterpret_cast<BYTE*>(out.data()));
        CryptReleaseContext(provider, 0);
        return ok != FALSE;
    }

    inline bool cryptoapi_hash(ALG_ID algorithm, const char* data, size_t size, std::string& out)
    {
        HCRYPTPROV provider = 0;
        HCRYPTHASH hashHandle = 0;
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;
        bool ok = CryptCreateHash(provider, algorithm, 0, 0, &hashHandle) != FALSE;
        ok = ok && CryptHashData(hashHandle, reinterpret_cast<const BYTE*>(data),
            static_cast<DWORD>(size), 0) != FALSE;
        DWORD hashSize = 0;
        DWORD hashSizeLength = sizeof(hashSize);
        ok = ok && CryptGetHashParam(hashHandle, HP_HASHSIZE,
            reinterpret_cast<BYTE*>(&hashSize), &hashSizeLength, 0) != FALSE;
        std::vector<BYTE> digest(hashSize);
        ok = ok && CryptGetHashParam(hashHandle, HP_HASHVAL, digest.data(), &hashSize, 0) != FALSE;
        if (ok)
        {
            static constexpr char hex[] = "0123456789abcdef";
            out.resize(hashSize * 2);
            for (DWORD i = 0; i < hashSize; ++i)
            {
                out[i * 2] = hex[digest[i] >> 4];
                out[i * 2 + 1] = hex[digest[i] & 0x0f];
            }
        }
        if (hashHandle) CryptDestroyHash(hashHandle);
        CryptReleaseContext(provider, 0);
        return ok;
    }

    inline bool aes_cbc_crypt(std::string& data, const std::string& key,
        const std::string& iv, bool encrypting)
    {
        if (key.size() != 32 || iv.size() < 16) return false;
        if (data.size() > 64 * 1024 * 1024) return false; // лимит
        if (data.size() > 0xFFFFFFFF - 16) return false; // защита DWORD

        HCRYPTPROV provider = 0;
        HCRYPTKEY cryptoKey = 0;
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;

        struct AesKeyBlob
        {
            BLOBHEADER header;
            DWORD keySize;
            BYTE key[32];
        } blob{};
        blob.header.bType = PLAINTEXTKEYBLOB;
        blob.header.bVersion = CUR_BLOB_VERSION;
        blob.header.aiKeyAlg = CALG_AES_256;
        blob.keySize = 32;
        memcpy(blob.key, key.data(), 32);

        bool ok = CryptImportKey(provider, reinterpret_cast<BYTE*>(&blob), sizeof(blob),
            0, 0, &cryptoKey) != FALSE;
        DWORD mode = CRYPT_MODE_CBC;
        ok = ok && CryptSetKeyParam(cryptoKey, KP_MODE, reinterpret_cast<BYTE*>(&mode), 0);
        ok = ok && CryptSetKeyParam(cryptoKey, KP_IV,
            reinterpret_cast<const BYTE*>(iv.data()), 0);

        if (ok && encrypting)
        {
            DWORD length = static_cast<DWORD>(data.size());
            DWORD capacity = length + 16;
            data.resize(capacity);
            ok = CryptEncrypt(cryptoKey, 0, TRUE, 0,
                reinterpret_cast<BYTE*>(data.data()), &length, capacity) != FALSE;
            if (ok) data.resize(length);
        }
        else if (ok)
        {
            DWORD length = static_cast<DWORD>(data.size());
            if (length % 16 != 0) { ok = false; }
            ok = CryptDecrypt(cryptoKey, 0, TRUE, 0,
                reinterpret_cast<BYTE*>(data.data()), &length) != FALSE;
            if (ok) data.resize(length);
        }

        if (cryptoKey) CryptDestroyKey(cryptoKey);
        CryptReleaseContext(provider, 0);
        return ok;
    }

    inline std::string base64_encode_bytes(const uint8_t* data, size_t len)
    {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);

        for (size_t i = 0; i < len; i += 3)
        {
            uint32_t triple = 0;
            int n = 0;

            triple |= uint32_t(data[i]) << 16; n = 1;
            if (i + 1 < len) { triple |= uint32_t(data[i + 1]) << 8; n = 2; }
            if (i + 2 < len) { triple |= uint32_t(data[i + 2]); n = 3; }

            out.push_back(alphabet[(triple >> 18) & 0x3F]);
            out.push_back(alphabet[(triple >> 12) & 0x3F]);
            out.push_back(n >= 2 ? alphabet[(triple >> 6) & 0x3F] : '=');
            out.push_back(n >= 3 ? alphabet[triple & 0x3F] : '=');
        }

        return out;
    }

    inline bool base64_decode_char(uint8_t& out, char c)
    {
        if (c >= 'A' && c <= 'Z') { out = uint8_t(c - 'A'); return true; }
        if (c >= 'a' && c <= 'z') { out = uint8_t(c - 'a' + 26); return true; }
        if (c >= '0' && c <= '9') { out = uint8_t(c - '0' + 52); return true; }
        if (c == '+') { out = 62; return true; }
        if (c == '/') { out = 63; return true; }
        return false;
    }

    inline bool base64_decode_string(const std::string& in, std::string& out)
    {
        out.clear();
        if (in.empty())
            return true;

        if (in.size() % 4 != 0)
            return false;

        out.reserve((in.size() / 4) * 3);

        for (size_t i = 0; i < in.size(); i += 4)
        {
            uint8_t v[4] = { 0, 0, 0, 0 };
            int pad = 0;

            for (int k = 0; k < 4; k++)
            {
                char c = in[i + k];
                if (c == '=')
                {
                    v[k] = 0;
                    pad++;
                }
                else
                {
                    if (!base64_decode_char(v[k], c))
                        return false;
                }
            }

            uint32_t triple = (uint32_t(v[0]) << 18) | (uint32_t(v[1]) << 12) | (uint32_t(v[2]) << 6) | uint32_t(v[3]);
            out.push_back(char((triple >> 16) & 0xFF));
            if (pad < 2) out.push_back(char((triple >> 8) & 0xFF));
            if (pad < 1) out.push_back(char(triple & 0xFF));
        }

        return true;
    }

    inline void lz4_write_len(std::vector<uint8_t>& out, size_t len)
    {
        while (len >= 255)
        {
            out.push_back(255);
            len -= 255;
        }
        out.push_back(uint8_t(len));
    }

    inline std::string lz4_compress_block(const uint8_t* src, size_t srcSize)
    {
        const size_t HASH_SIZE = 1u << 16;
        std::vector<int> hash(HASH_SIZE, -1);

        auto read32 = [&](size_t pos) -> uint32_t
            {
                uint32_t v = 0;
                v |= uint32_t(src[pos + 0]) << 0;
                v |= uint32_t(src[pos + 1]) << 8;
                v |= uint32_t(src[pos + 2]) << 16;
                v |= uint32_t(src[pos + 3]) << 24;
                return v;
            };

        auto hash32 = [&](uint32_t v) -> size_t
            {
                return (v * 2654435761u) >> (32 - 16);
            };

        std::vector<uint8_t> out;
        out.reserve(srcSize + srcSize / 8 + 16);

        size_t anchor = 0;
        size_t i = 0;

        while (i + 4 <= srcSize)
        {
            uint32_t seq = read32(i);
            size_t h = hash32(seq);
            int ref = hash[h];
            hash[h] = int(i);

            bool match = false;
            size_t matchPos = 0;

            if (ref >= 0)
            {
                size_t r = size_t(ref);
                if (i > r && (i - r) <= 0xFFFF)
                {
                    if (r + 4 <= srcSize && read32(r) == seq)
                    {
                        match = true;
                        matchPos = r;
                    }
                }
            }

            if (!match)
            {
                i++;
                continue;
            }

            size_t litLen = i - anchor;
            size_t mLen = 4;
            while (i + mLen < srcSize && matchPos + mLen < srcSize && src[i + mLen] == src[matchPos + mLen])
                mLen++;

            uint8_t token = 0;
            uint8_t litNib = uint8_t((std::min)(litLen, size_t(15)));
            uint8_t matchNib = uint8_t((std::min)(mLen - 4, size_t(15)));
            token = uint8_t((litNib << 4) | matchNib);
            out.push_back(token);

            if (litLen >= 15)
                lz4_write_len(out, litLen - 15);

            out.insert(out.end(), src + anchor, src + i);

            uint16_t offset = uint16_t(i - matchPos);
            out.push_back(uint8_t(offset & 0xFF));
            out.push_back(uint8_t((offset >> 8) & 0xFF));

            if (mLen - 4 >= 15)
                lz4_write_len(out, (mLen - 4) - 15);

            i += mLen;
            anchor = i;

            if (i + 4 <= srcSize)
            {
                size_t end = (std::min)(i, srcSize - 4);
                for (size_t p = i - mLen + 1; p <= end; p++)
                {
                    size_t hh = hash32(read32(p));
                    hash[hh] = int(p);
                }
            }
        }

        if (anchor < srcSize)
        {
            size_t litLen = srcSize - anchor;
            uint8_t token = uint8_t(((std::min)(litLen, size_t(15)) << 4) | 0);
            out.push_back(token);
            if (litLen >= 15)
                lz4_write_len(out, litLen - 15);
            out.insert(out.end(), src + anchor, src + srcSize);
        }

        return std::string(reinterpret_cast<const char*>(out.data()), out.size());
    }

    inline bool lz4_read_len(const uint8_t* in, size_t inSize, size_t& ip, size_t& len)
    {
        size_t s = 0;
        while (true)
        {
            if (ip >= inSize) return false;
            uint8_t v = in[ip++];
            s += v;
            if (v != 255) break;
        }
        len += s;
        return true;
    }

    inline bool lz4_decompress_block(const uint8_t* in, size_t inSize, std::string& out)
    {
        std::vector<uint8_t> dst;
        dst.reserve(inSize * 3);

        size_t ip = 0;
        while (ip < inSize)
        {
            uint8_t token = in[ip++];
            size_t litLen = (token >> 4) & 0xF;
            size_t matchLen = (token & 0xF);

            if (litLen == 15)
            {
                if (!lz4_read_len(in, inSize, ip, litLen))
                    return false;
            }

            if (ip + litLen > inSize)
                return false;

            dst.insert(dst.end(), in + ip, in + ip + litLen);
            ip += litLen;

            if (ip >= inSize)
                break;

            if (ip + 2 > inSize)
                return false;

            uint16_t offset = uint16_t(in[ip]) | (uint16_t(in[ip + 1]) << 8);
            ip += 2;
            if (offset == 0 || offset > dst.size())
                return false;

            matchLen += 4;
            if ((token & 0xF) == 15)
            {
                if (!lz4_read_len(in, inSize, ip, matchLen))
                    return false;
            }

            size_t start = dst.size() - offset;
            for (size_t k = 0; k < matchLen; k++)
                dst.push_back(dst[start + k]);
        }

        out.assign(reinterpret_cast<const char*>(dst.data()), dst.size());
        return true;
    }

    inline int base64encode(lua_State* L)
    {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 1, &len);

        // лимит 50MB на вход -> на выходе ~66MB, больше уже убивает кучу
        if (len > 50 * 1024 * 1024)
            luaL_error(L, "data too large");

        // защита от переполнения при ((len+2)/3)*4
        if (len > SIZE_MAX / 4 * 3)
            luaL_error(L, "data too large");

        std::string out;
        try {
            out = base64_encode_bytes(reinterpret_cast<const uint8_t*>(data), len);
        }
        catch (const std::bad_alloc&) {
            luaL_error(L, "out of memory");
        }
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    inline int base64decode(lua_State* L)
    {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 1, &len);

        if (len > 66 * 1024 * 1024) // 50MB * 4/3
            luaL_error(L, "data too large");

        if (len % 4 != 0)
            luaL_error(L, "invalid base64");

        // проверка итогового размера до аллокации
        size_t decodedSize = (len / 4) * 3;
        if (decodedSize > 50 * 1024 * 1024)
            luaL_error(L, "data too large");

        std::string decoded;
        try {
            decoded.reserve(decodedSize);
            if (!base64_decode_string(std::string(data, len), decoded))
                luaL_error(L, "invalid base64");
        }
        catch (const std::bad_alloc&) {
            luaL_error(L, "out of memory");
        }

        lua_pushlstring(L, decoded.data(), decoded.size());
        return 1;
    }

    inline int base64_encode(lua_State* L)
    {
        return base64encode(L);
    }

    inline int base64_decode(lua_State* L)
    {
        return base64decode(L);
    }

    inline int generatebytes(lua_State* L)
    {
        const int size = luaL_checkinteger(L, 1);
        if (size < 0 || size > 1024 * 1024)
            luaL_argerror(L, 1, "size must be between 0 and 1048576");
        std::string bytes;
        if (!random_bytes(bytes, static_cast<size_t>(size)))
            luaL_error(L, "failed to generate secure random bytes");
        const std::string encoded = base64_encode_bytes(
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        lua_pushlstring(L, encoded.data(), encoded.size());
        return 1;
    }

    inline int generatekey(lua_State* L)
    {
        lua_pushinteger(L, 32);
        return generatebytes(L);
    }

    inline int encrypt(lua_State* L)
    {
        size_t dataLength = 0, keyLength = 0;
        const char* rawData = luaL_checklstring(L, 1, &dataLength);
        const char* encodedKey = luaL_checklstring(L, 2, &keyLength);
        const char* mode = luaL_optstring(L, 4, "CBC");
        if (_stricmp(mode, "CBC") != 0)
            luaL_argerror(L, 4, "only CBC mode is supported");

        std::string key;
        if (!base64_decode_string(std::string(encodedKey, keyLength), key) || key.size() != 32)
            luaL_argerror(L, 2, "key must be a base64 encoded 256-bit key");

        std::string iv;
        if (lua_isnoneornil(L, 3))
        {
            if (!random_bytes(iv, 16))
                luaL_error(L, "failed to generate an IV");
        }
        else
        {
            size_t ivLength = 0;
            const char* encodedIv = luaL_checklstring(L, 3, &ivLength);
            if (!base64_decode_string(std::string(encodedIv, ivLength), iv) || iv.size() < 16)
                luaL_argerror(L, 3, "IV must contain at least 16 decoded bytes");
        }

        std::string encrypted(rawData, dataLength);
        if (!aes_cbc_crypt(encrypted, key, iv, true))
            luaL_error(L, "AES-CBC encryption failed");
        const std::string encoded = base64_encode_bytes(
            reinterpret_cast<const uint8_t*>(encrypted.data()), encrypted.size());
        const std::string encodedIv = base64_encode_bytes(
            reinterpret_cast<const uint8_t*>(iv.data()), 16);
        lua_pushlstring(L, encoded.data(), encoded.size());
        lua_pushlstring(L, encodedIv.data(), encodedIv.size());
        return 2;
    }

    inline int decrypt(lua_State* L)
    {
        size_t encryptedLength = 0, keyLength = 0, ivLength = 0;
        const char* encodedData = luaL_checklstring(L, 1, &encryptedLength);
        const char* encodedKey = luaL_checklstring(L, 2, &keyLength);
        const char* encodedIv = luaL_checklstring(L, 3, &ivLength);
        const char* mode = luaL_optstring(L, 4, "CBC");
        if (_stricmp(mode, "CBC") != 0)
            luaL_argerror(L, 4, "only CBC mode is supported");

        std::string encrypted, key, iv;
        if (!base64_decode_string(std::string(encodedData, encryptedLength), encrypted))
            luaL_argerror(L, 1, "encrypted data is not valid base64");
        if (!base64_decode_string(std::string(encodedKey, keyLength), key) || key.size() != 32)
            luaL_argerror(L, 2, "key must be a base64 encoded 256-bit key");
        if (!base64_decode_string(std::string(encodedIv, ivLength), iv) || iv.size() < 16)
            luaL_argerror(L, 3, "IV must contain at least 16 decoded bytes");
        if (!aes_cbc_crypt(encrypted, key, iv, false))
            luaL_error(L, "AES-CBC decryption failed");
        lua_pushlstring(L, encrypted.data(), encrypted.size());
        return 1;
    }

    inline bool software_sha3(const char* variant, const char* data, size_t dataLen, std::string& out)
    {
        if (strcmp(variant, "224") == 0)      out = sha3_impl::sha3(224, data, dataLen);
        else if (strcmp(variant, "256") == 0) out = sha3_impl::sha3(256, data, dataLen);
        else if (strcmp(variant, "384") == 0) out = sha3_impl::sha3(384, data, dataLen);
        else if (strcmp(variant, "512") == 0) out = sha3_impl::sha3(512, data, dataLen);
        else return false;
        return true;
    }

    inline int hash(lua_State* L)
    {
        size_t dataLength = 0;
        const char* data = luaL_checklstring(L, 1, &dataLength);
        const char* algorithm = luaL_optstring(L, 2, "sha384");

        std::string digest;

        if (_stricmp(algorithm, "md5") == 0)
        {
            if (!cryptoapi_hash(CALG_MD5, data, dataLength, digest))
                luaL_error(L, "hash operation failed");
        }
        else if (_stricmp(algorithm, "sha1") == 0)
        {
            if (!cryptoapi_hash(CALG_SHA1, data, dataLength, digest))
                luaL_error(L, "hash operation failed");
        }
        else if (_stricmp(algorithm, "sha224") == 0)
        {
            digest = sha224_impl::sha224(data, dataLength);
        }
        else if (_stricmp(algorithm, "sha256") == 0)
        {
            if (!cryptoapi_hash(CALG_SHA_256, data, dataLength, digest))
                luaL_error(L, "hash operation failed");
        }
        else if (_stricmp(algorithm, "sha384") == 0)
        {
            if (!cryptoapi_hash(CALG_SHA_384, data, dataLength, digest))
                luaL_error(L, "hash operation failed");
        }
        else if (_stricmp(algorithm, "sha512") == 0)
        {
            if (!cryptoapi_hash(CALG_SHA_512, data, dataLength, digest))
                luaL_error(L, "hash operation failed");
        }
        else if (_stricmp(algorithm, "sha3-224") == 0 || _stricmp(algorithm, "sha3_224") == 0)
        {
            digest = sha3_impl::sha3(224, data, dataLength);
        }
        else if (_stricmp(algorithm, "sha3-256") == 0 || _stricmp(algorithm, "sha3_256") == 0)
        {
            digest = sha3_impl::sha3(256, data, dataLength);
        }
        else if (_stricmp(algorithm, "sha3-384") == 0 || _stricmp(algorithm, "sha3_384") == 0)
        {
            digest = sha3_impl::sha3(384, data, dataLength);
        }
        else if (_stricmp(algorithm, "sha3-512") == 0 || _stricmp(algorithm, "sha3_512") == 0)
        {
            digest = sha3_impl::sha3(512, data, dataLength);
        }
        else
        {
            luaL_argerror(L, 2, "unsupported hash algorithm");
        }

        lua_pushlstring(L, digest.data(), digest.size());
        return 1;
    }

    inline int lz4compress(lua_State* L)
    {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 1, &len);
        std::string out = lz4_compress_block(reinterpret_cast<const uint8_t*>(data), len);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    inline int lz4decompress(lua_State* L)
    {
        size_t len = 0;
        const char* data = luaL_checklstring(L, 1, &len);
        std::string out;
        if (!lz4_decompress_block(reinterpret_cast<const uint8_t*>(data), len, out))
            luaL_error(L, "invalid lz4 data");
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    std::size_t calculateProtoBytesSize(const Proto* proto) {
        std::size_t size = 2 * sizeof(std::byte);

        for (auto i = 0; i < proto->sizek; i++) {
            switch (const auto currentConstant = &proto->k[i]; currentConstant->tt) {
            case LUA_TNIL:
                size += 1;
                break;
            case LUA_TSTRING: {
                const auto currentConstantString = &currentConstant->value.gc->ts;
                size += currentConstantString->len;
                break;
            }
            case LUA_TNUMBER: {
                size += sizeof(lua_Number);
                break;
            }
            case LUA_TBOOLEAN:
                size += sizeof(int);
                break;
            case LUA_TVECTOR:
                size += sizeof(float) * 3;
                break;
            case LUA_TTABLE: {
                const auto currentConstantTable = &currentConstant->value.gc->h;
                if (currentConstantTable->node != &luaH_dummynode) {
                    for (int nodeI = 0; nodeI < sizenode(currentConstantTable); nodeI++) {
                        const auto currentNode = &currentConstantTable->node[nodeI];
                        if (currentNode->key.tt != LUA_TSTRING)
                            continue;
                        const auto nodeString = &currentNode->key.value.gc->ts;
                        size += nodeString->len;
                    }
                }
                break;
            }
            case LUA_TFUNCTION: {
                const auto constantFunction = &currentConstant->value.gc->cl;
                if (constantFunction->isC) {
                    size += 1;
                    break;
                }
                size += calculateProtoBytesSize(constantFunction->l.p);
                break;
            }
            default:
                break;
            }
        }

        for (auto i = 0; i < proto->sizep; i++)
            size += calculateProtoBytesSize(proto->p[i]);

        size += proto->sizecode * sizeof(Instruction);

        return size;
    }

    std::vector<unsigned char> getProtoBytes(const Proto* proto) {
        std::vector<unsigned char> protoBytes{};
        auto protoBytesSize = calculateProtoBytesSize(proto);
        protoBytes.reserve(protoBytesSize);
        protoBytes.push_back(proto->is_vararg);
        protoBytes.push_back(proto->numparams);

        for (auto i = 0; i < proto->sizek; i++) {
            switch (const auto currentConstant = &proto->k[i]; currentConstant->tt) {
            case LUA_TNIL:
                protoBytes.push_back(0);
                break;
            case LUA_TSTRING: {
                const auto currentConstantString = &currentConstant->value.gc->ts;
                protoBytes.insert(protoBytes.end(),
                    reinterpret_cast<const unsigned char*>(currentConstantString->data),
                    reinterpret_cast<const unsigned char*>(currentConstantString->data) + currentConstantString->len
                );
                break;
            }
            case LUA_TNUMBER: {
                const lua_Number* n = &currentConstant->value.n;
                protoBytes.insert(protoBytes.end(),
                    reinterpret_cast<const unsigned char*>(n),
                    reinterpret_cast<const unsigned char*>(n) + sizeof(lua_Number)
                );
                break;
            }
            case LUA_TBOOLEAN:
                protoBytes.push_back(currentConstant->value.b);
                break;
            case LUA_TVECTOR:
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[0] & 0xff000000);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[0] & 0x00ff0000);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[0] & 0x0000ff00);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[0] & 0x000000ff);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[1] & 0xff000000);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[1] & 0x00ff0000);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[1] & 0x0000ff00);
                protoBytes.push_back(reinterpret_cast<int*>(currentConstant->value.v)[1] & 0x000000ff);
                protoBytes.push_back(currentConstant->extra[0] & 0xff000000);
                protoBytes.push_back(currentConstant->extra[0] & 0x00ff0000);
                protoBytes.push_back(currentConstant->extra[0] & 0x0000ff00);
                protoBytes.push_back(currentConstant->extra[0] & 0x000000ff);
                break;
            case LUA_TTABLE: {
                const auto currentConstantTable = &currentConstant->value.gc->h;
                if (currentConstantTable->node != &luaH_dummynode) {
                    for (int nodeI = 0; nodeI < sizenode(currentConstantTable); nodeI++) {
                        const auto currentNode = &currentConstantTable->node[nodeI];
                        if (currentNode->key.tt != LUA_TSTRING)
                            continue;
                        const auto nodeString = &currentNode->key.value.gc->ts;
                        protoBytes.insert(protoBytes.end(),
                            reinterpret_cast<const unsigned char*>(nodeString->data),
                            reinterpret_cast<const unsigned char*>(nodeString->data) + nodeString->len
                        );
                    }
                }
                break;
            }
            case LUA_TFUNCTION: {
                const auto constantFunction = &currentConstant->value.gc->cl;
                if (constantFunction->isC) {
                    protoBytes.push_back('C');
                    break;
                }

                std::vector<unsigned char> functionBytes = getProtoBytes(constantFunction->l.p);
                protoBytes.insert(protoBytes.end(), functionBytes.data(), functionBytes.data() + functionBytes.size());
                break;
            }
            default:
                break;
            }
        }

        for (auto i = 0; i < proto->sizep; i++) {
            std::vector<unsigned char> currentProtoBytes = getProtoBytes(proto->p[i]);
            protoBytes.insert(protoBytes.end(), currentProtoBytes.data(),
                currentProtoBytes.data() + currentProtoBytes.size()
            );
        }

        protoBytes.insert(protoBytes.end(), reinterpret_cast<const unsigned char*>(proto->code),
            reinterpret_cast<const unsigned char*>(proto->code) + proto->sizecode * sizeof(Instruction)
        );
        return protoBytes;
    }

    inline int random(lua_State* L)
    {
        int n = (int)luaL_checknumber(L, 1);
        if (n < 0)
            luaL_argerror(L, 1, "size must be non-negative");
        if (n == 0) {
            lua_pushlstring(L, "", 0);
            return 1;
        }
        std::vector<BYTE> buf(n);
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buf.data(), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            luaL_error(L, "failed to generate random bytes");
        lua_pushlstring(L, reinterpret_cast<const char*>(buf.data()), n);
        return 1;
    }

    inline bool hmac_sha256(const char* key, size_t keyLen,
        const char* data, size_t dataLen,
        std::vector<BYTE>& out)
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;

        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
            return false;

        DWORD hashLen = 0, cbData = 0;
        BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLen, sizeof(DWORD), &cbData, 0);

        out.resize(hashLen);
        bool ok = BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, (PBYTE)key, (ULONG)keyLen, 0))
            && BCRYPT_SUCCESS(BCryptHashData(hHash, (PBYTE)data, (ULONG)dataLen, 0))
            && BCRYPT_SUCCESS(BCryptFinishHash(hHash, out.data(), hashLen, 0));

        if (hHash) BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return ok;
    }

    inline int hmac(lua_State* L)
    {
        size_t keyLen = 0, dataLen = 0;
        const char* key = luaL_checklstring(L, 1, &keyLen);
        const char* data = luaL_checklstring(L, 2, &dataLen);
        const char* algorithm = luaL_optstring(L, 3, "sha256");

        std::vector<BYTE> digest;

        if (_stricmp(algorithm, "sha256") == 0)
        {
            if (!hmac_sha256(key, keyLen, data, dataLen, digest))
                luaL_error(L, "hmac operation failed");
        }
        else
        {
            luaL_argerror(L, 3, "unsupported hmac algorithm");
        }

        DWORD b64Len = 0;
        CryptBinaryToStringA(digest.data(), (DWORD)digest.size(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64Len);
        std::string b64(b64Len, '\0');
        CryptBinaryToStringA(digest.data(), (DWORD)digest.size(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &b64Len);
        while (!b64.empty() && b64.back() == '\0') b64.pop_back();

        lua_pushlstring(L, b64.data(), b64.size());
        return 1;
    }

    int getfunctionhash(lua_State* L) {
        luaL_checktype(L, 1, LUA_TFUNCTION);

        const auto closure = clvalue(L->top - 1);

        if (closure->isC) {
            luaL_argerror(L, 1, "lua function expected, got C closure");
            return 0;
        }

        const Proto* proto = closure->l.p;

        std::vector<unsigned char> protoBytes = getProtoBytes(proto);

        std::string dataToHash(protoBytes.begin(), protoBytes.end());

        std::string hashResult = sha384(dataToHash);

        lua_pushlstring(L, hashResult.c_str(), hashResult.size());
        return 1;
    }


    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, "getfunctionhash", getfunctionhash);
        Utils::AddFunction(L, "base64encode", base64encode);
        Utils::AddFunction(L, "base64decode", base64decode);
        Utils::AddFunction(L, "base64_encode", base64_encode);
        Utils::AddFunction(L, "base64_decode", base64_decode);
        Utils::AddFunction(L, "lz4compress", lz4compress);
        Utils::AddFunction(L, "lz4decompress", lz4decompress);

        lua_newtable(L);
        lua_setreadonly(L, -1, false);
        Utils::AddTableFunction(L, "encode", base64encode);
        Utils::AddTableFunction(L, "decode", base64decode);
        lua_setreadonly(L, -1, true);
        lua_setglobal(L, "base64");

        lua_newtable(L);
        lua_setreadonly(L, -1, false);
        Utils::AddTableFunction(L, "base64encode", base64encode);
        Utils::AddTableFunction(L, "base64decode", base64decode);
        Utils::AddTableFunction(L, "base64_encode", base64_encode);
        Utils::AddTableFunction(L, "base64_decode", base64_decode);
        Utils::AddTableFunction(L, "encrypt", encrypt);
        Utils::AddTableFunction(L, "decrypt", decrypt);
        Utils::AddTableFunction(L, "generatebytes", generatebytes);
        Utils::AddTableFunction(L, "generatekey", generatekey);
        Utils::AddTableFunction(L, "hash", hash);
        Utils::AddTableFunction(L, "hmac", hmac);
        Utils::AddTableFunction(L, "random", random);
        Utils::AddTableFunction(L, "lz4compress", lz4compress);
        Utils::AddTableFunction(L, "lz4decompress", lz4decompress);

        lua_newtable(L);
        lua_setreadonly(L, -1, false);
        Utils::AddTableFunction(L, "encode", base64encode);
        Utils::AddTableFunction(L, "decode", base64decode);
        Utils::AddTableFunction(L, "lz4compress", lz4compress);
        Utils::AddTableFunction(L, "lz4decompress", lz4decompress);
        lua_setreadonly(L, -1, true);
        lua_setfield(L, -2, "base64");

        lua_setreadonly(L, -1, true);
        lua_setglobal(L, "crypt");
    }
}


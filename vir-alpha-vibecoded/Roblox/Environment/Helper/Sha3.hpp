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
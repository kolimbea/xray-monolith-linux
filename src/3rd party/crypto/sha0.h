// SHA-0 (FIPS 180, 1993). The engine's "xr_sha256" is OpenSSL's SHA_* = SHA-0, which OpenSSL 1.1+
// removed; this keeps the digests identical to the Windows build. SHA-0 is SHA-1 without the
// one-bit rotation in the message schedule.
#pragma once
#include <cstdint>
#include <cstring>

struct xr_sha0_ctx
{
    uint32_t h[5];
    uint64_t length; // bytes
    uint8_t block[64];
    uint32_t used;
};

inline uint32_t xr_sha0_rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline void xr_sha0_block(xr_sha0_ctx& c, const uint8_t* p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; ++i)
        w[i] = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; // SHA-1 would rotate this left by 1
    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4];
    for (int i = 0; i < 80; ++i)
    {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        const uint32_t t = xr_sha0_rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = xr_sha0_rol(b, 30); b = a; a = t;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d; c.h[4] += e;
}

inline void xr_sha0_init(xr_sha0_ctx& c)
{
    c.h[0] = 0x67452301; c.h[1] = 0xEFCDAB89; c.h[2] = 0x98BADCFE; c.h[3] = 0x10325476; c.h[4] = 0xC3D2E1F0;
    c.length = 0;
    c.used = 0;
}

inline void xr_sha0_update(xr_sha0_ctx& c, const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    c.length += size;
    while (size)
    {
        const size_t n = size < 64 - c.used ? size : 64 - c.used;
        memcpy(c.block + c.used, p, n);
        c.used += uint32_t(n);
        p += n;
        size -= n;
        if (c.used == 64)
        {
            xr_sha0_block(c, c.block);
            c.used = 0;
        }
    }
}

inline void xr_sha0_final(uint8_t out[20], xr_sha0_ctx& c)
{
    const uint64_t bits = c.length * 8;
    const uint8_t pad = 0x80;
    xr_sha0_update(c, &pad, 1);
    const uint8_t zero = 0;
    while (c.used != 56)
        xr_sha0_update(c, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i)
        len[i] = uint8_t(bits >> (56 - 8 * i));
    xr_sha0_update(c, len, 8);
    for (int i = 0; i < 5; ++i)
    {
        out[i * 4] = uint8_t(c.h[i] >> 24); out[i * 4 + 1] = uint8_t(c.h[i] >> 16);
        out[i * 4 + 2] = uint8_t(c.h[i] >> 8); out[i * 4 + 3] = uint8_t(c.h[i]);
    }
}

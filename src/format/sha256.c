/*
 * Small SHA-256 implementation for FELF integrity records.
 * This is not a signature.  It detects corruption and accidental changes.
 */

#include "felf_format.h"

#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} sha256_ctx;

static uint32_t rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void sha256_transform(sha256_ctx *ctx, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++)
        w[i] = load_be32(block + i * 4);
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1];
    uint32_t c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    memcpy(ctx->state, iv, sizeof(iv));
    ctx->bits = 0;
    ctx->used = 0;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->bits += (uint64_t)len * 8;
    while (len != 0) {
        size_t room = sizeof(ctx->block) - ctx->used;
        size_t take = len < room ? len : room;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take;
        data += take;
        len -= take;
        if (ctx->used == sizeof(ctx->block)) {
            sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t out[32])
{
    uint64_t bits = ctx->bits;
    size_t used = ctx->used;
    ctx->block[used++] = 0x80;
    if (used > 56) {
        memset(ctx->block + used, 0, sizeof(ctx->block) - used);
        sha256_transform(ctx, ctx->block);
        used = 0;
    }
    memset(ctx->block + used, 0, 56 - used);
    for (unsigned i = 0; i < 8; i++)
        ctx->block[63 - i] = (uint8_t)(bits >> (i * 8));
    sha256_transform(ctx, ctx->block);
    for (unsigned i = 0; i < 8; i++)
        store_be32(out + i * 4, ctx->state[i]);
}

void felf_sha256(const void *data, size_t len, uint8_t out[32])
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)data, len);
    sha256_final(&ctx, out);
}

void felf_sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    out[64] = '\0';
}

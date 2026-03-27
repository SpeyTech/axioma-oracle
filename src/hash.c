/**
 * @file hash.c
 * @brief SHA-256 implementation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * Pure C99 SHA-256 implementation with no dynamic allocation.
 * Manual big-endian encoding ensures bit-identical results across
 * x86_64, ARM64, and RISC-V architectures (FIPS 180-4 compliant).
 *
 * @traceability SRS-004-SHALL-009, SRS-004-SHALL-046
 */

#include "axilog/hash.h"
#include "axilog/canonical.h"
#include <string.h>

/* ========================================================================
 * SHA-256 Constants
 * ======================================================================== */

static const uint32_t K[64] = {
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

/* ========================================================================
 * SHA-256 Helper Macros
 * ======================================================================== */

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* ========================================================================
 * SHA-256 Block Processing
 * ======================================================================== */

static void sha256_transform(ax_sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main loop */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Update state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* ========================================================================
 * SHA-256 Public Functions
 * ======================================================================== */

/**
 * SRS-004-SHALL-009: Prompt determinism
 * SRS-004-SHALL-046: Observation hash
 */
void ax_sha256_init(ax_sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void ax_sha256_update(ax_sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t buf_idx;

    buf_idx = (size_t)(ctx->count & 63);
    ctx->count += len;

    /* Process partial buffer */
    if (buf_idx > 0) {
        size_t space = 64 - buf_idx;
        if (len < space) {
            memcpy(&ctx->buffer[buf_idx], data, len);
            return;
        }
        memcpy(&ctx->buffer[buf_idx], data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
    }

    /* Process full blocks */
    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    /* Buffer remaining */
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

void ax_sha256_final(ax_sha256_ctx_t *ctx, uint8_t hash[32])
{
    uint64_t bit_count;
    size_t buf_idx;
    int i;

    bit_count = ctx->count * 8;
    buf_idx = (size_t)(ctx->count & 63);

    /* Padding */
    ctx->buffer[buf_idx++] = 0x80;

    if (buf_idx > 56) {
        /* Need two blocks */
        while (buf_idx < 64) {
            ctx->buffer[buf_idx++] = 0x00;
        }
        sha256_transform(ctx, ctx->buffer);
        buf_idx = 0;
    }

    while (buf_idx < 56) {
        ctx->buffer[buf_idx++] = 0x00;
    }

    /* Append bit count (big-endian) */
    ctx->buffer[56] = (uint8_t)(bit_count >> 56);
    ctx->buffer[57] = (uint8_t)(bit_count >> 48);
    ctx->buffer[58] = (uint8_t)(bit_count >> 40);
    ctx->buffer[59] = (uint8_t)(bit_count >> 32);
    ctx->buffer[60] = (uint8_t)(bit_count >> 24);
    ctx->buffer[61] = (uint8_t)(bit_count >> 16);
    ctx->buffer[62] = (uint8_t)(bit_count >> 8);
    ctx->buffer[63] = (uint8_t)(bit_count);

    sha256_transform(ctx, ctx->buffer);

    /* Output hash (big-endian) */
    for (i = 0; i < 8; i++) {
        hash[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void ax_sha256(uint8_t hash[32], const uint8_t *data, size_t len)
{
    ax_sha256_ctx_t ctx;
    ax_sha256_init(&ctx);
    ax_sha256_update(&ctx, data, len);
    ax_sha256_final(&ctx, hash);
}

/* ========================================================================
 * Observation Hashing
 * ======================================================================== */

/**
 * SRS-004-SHALL-009: Prompt determinism
 */
void ax_compute_input_hash(uint8_t hash[32], const char *input, size_t input_len)
{
    ax_sha256(hash, (const uint8_t *)input, input_len);
}

/**
 * SRS-004-SHALL-046: Observation hash
 *
 * Procedure:
 *   1. Set obs_hash = ""
 *   2. Canonicalise full record
 *   3. SHA-256 hash
 *   4. Hex encode → lowercase
 *   5. Write into obs_hash
 */
int ax_obs_compute_hash(ax_obs_record_t *obs)
{
    char canonical_buf[AX_CANONICAL_BUFFER_SIZE];
    uint8_t hash[AX_HASH_SIZE];
    int len;

    /* Step 1: Set obs_hash to empty */
    obs->obs_hash[0] = '\0';

    /* Step 2: Canonicalise with empty hash */
    len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), obs, 0);
    if (len < 0) {
        return AX_ERR_BUFFER;
    }

    /* Step 3: Compute SHA-256 */
    ax_sha256(hash, (const uint8_t *)canonical_buf, (size_t)len);

    /* Step 4-5: Hex encode into obs_hash */
    ax_format_hash_hex(obs->obs_hash, sizeof(obs->obs_hash), hash, AX_HASH_SIZE);

    return AX_OK;
}

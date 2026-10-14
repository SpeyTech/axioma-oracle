/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * axilog_crypto.c - axilog_sha256() and axilog_commit() over the platform
 * libcrypto (OpenSSL 3, EVP interface). Owner decision D5.
 *
 * New code. It replaces axioma-spec src/axilog_sha256.c, which is not used
 * by this repository. Equivalence was established before adoption (E2):
 * identical output on 7,670 cases (lengths 0 to 4160, 2,000 random
 * lengths up to 1 MiB, one 536,870,989-byte message, commitments over five
 * tags and both error paths), identical oracle and ledger fingerprints, and
 * byte-identical gateway ledgers, on x86_64 (OpenSSL 3.0.13) and aarch64
 * (OpenSSL 3.5.7).
 *
 * The one behavioural difference: libcrypto can fail internally (allocation,
 * provider loading); the self-contained implementation could not. On any
 * such failure this code aborts rather than return a wrong digest. For the
 * gateway an abort is a crash: an open intent becomes an Interrupted record
 * at the next start (CONTRACT.md section 3).
 */
#include "axilog/sha256.h"
#include "axilog/commitment.h"

#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>

void axilog_sha256(uint8_t out[32], const uint8_t *data, size_t len)
{
    static const uint8_t empty = 0u;
    unsigned int olen = 0u;

    if (EVP_Digest(data != NULL ? (const void *)data : (const void *)&empty,
                   len, out, &olen, EVP_sha256(), NULL) != 1 || olen != 32u)
        abort();
}

/* SRS-007-SHALL-003..005: commit = SHA-256(tag || LE64(len) || payload).
 * Caller errors follow commitment.h; internal libcrypto failure aborts. */
void axilog_commit(const char *tag, const uint8_t *payload,
                   uint64_t payload_len, uint8_t out_commit[32],
                   ct_fault_flags_t *faults)
{
    EVP_MD_CTX *ctx;
    uint8_t le[8];
    unsigned int olen = 0u;
    unsigned int i;
    int ok;

    if (tag == NULL || out_commit == NULL || faults == NULL) {
        /* commitment.h: on failure out_commit is zeroed. The earlier
         * implementation left it untouched when only tag was NULL. */
        if (out_commit != NULL) memset(out_commit, 0, 32u);
        if (faults != NULL) faults->domain = 1;
        return;
    }
    if (payload == NULL && payload_len > 0u) {
        faults->domain = 1;
        memset(out_commit, 0, 32u);
        return;
    }
    for (i = 0u; i < 8u; i++)
        le[i] = (uint8_t)(payload_len >> (8u * i));

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) abort();
    ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
         EVP_DigestUpdate(ctx, tag, strlen(tag)) == 1 &&
         EVP_DigestUpdate(ctx, le, 8u) == 1 &&
         (payload_len == 0u ||
          EVP_DigestUpdate(ctx, payload, (size_t)payload_len) == 1) &&
         EVP_DigestFinal_ex(ctx, out_commit, &olen) == 1 && olen == 32u;
    EVP_MD_CTX_free(ctx);
    if (!ok) abort();
}

/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * axilog/sha256.h - plain SHA-256 digest, for input_hash and the
 * configuration digest. Implemented in substrate/src/axilog_crypto.c over
 * the platform libcrypto (owner decision D5). The signature is the one the
 * oracle library and the gateway were written against.
 */
#ifndef AXILOG_SHA256_H
#define AXILOG_SHA256_H

#include <stdint.h>
#include <stddef.h>

/* out = SHA-256(data[0..len)). data may be NULL when len is 0. Never
 * fails: if libcrypto cannot compute the digest the process aborts, so a
 * caller never receives a wrong one. */
void axilog_sha256(uint8_t out[32], const uint8_t *data, size_t len);

#endif /* AXILOG_SHA256_H */

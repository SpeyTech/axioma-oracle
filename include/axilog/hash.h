/**
 * @file hash.h
 * @brief SHA-256 hashing for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-009, SRS-004-SHALL-046
 */

#ifndef AXILOG_HASH_H
#define AXILOG_HASH_H

#include <stdint.h>
#include <stddef.h>
#include "axilog/types.h"
#include "axilog/limits.h"
#include "axilog/obs.h"

/* ========================================================================
 * SHA-256 Context
 * ======================================================================== */

/**
 * @brief SHA-256 context structure.
 *
 * All state is caller-owned; no dynamic allocation.
 */
typedef struct {
    uint32_t state[8];     /**< Hash state */
    uint64_t count;        /**< Total bytes processed */
    uint8_t  buffer[64];   /**< Partial block buffer */
} ax_sha256_ctx_t;

/* ========================================================================
 * SHA-256 Functions
 * ======================================================================== */

/**
 * @brief Initialize SHA-256 context.
 *
 * @param[out] ctx  Context to initialize
 */
void ax_sha256_init(ax_sha256_ctx_t *ctx);

/**
 * @brief Update SHA-256 with data.
 *
 * @param[in,out] ctx   Context
 * @param[in]     data  Data to hash
 * @param[in]     len   Length of data in bytes
 */
void ax_sha256_update(ax_sha256_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalize SHA-256 and produce hash.
 *
 * @param[in,out] ctx   Context (invalidated after this call)
 * @param[out]    hash  Output hash (32 bytes)
 */
void ax_sha256_final(ax_sha256_ctx_t *ctx, uint8_t hash[32]);

/**
 * @brief One-shot SHA-256 hash.
 *
 * @param[out] hash  Output hash (32 bytes)
 * @param[in]  data  Data to hash
 * @param[in]  len   Length of data in bytes
 */
void ax_sha256(uint8_t hash[32], const uint8_t *data, size_t len);

/* ========================================================================
 * Observation Hashing
 * ======================================================================== */

/**
 * @brief Compute input hash for oracle input.
 *
 * Computes SHA-256 of the canonicalised input.
 *
 * SRS-004-SHALL-009: Prompt determinism
 *
 * @param[out] hash        Output hash (32 bytes)
 * @param[in]  input       Canonical input string
 * @param[in]  input_len   Length of input in bytes
 */
void ax_compute_input_hash(
    uint8_t     hash[32],
    const char *input,
    size_t      input_len
);

/**
 * @brief Compute observation hash.
 *
 * Computes SHA-256 of the canonical AX:OBS:v1 record with obs_hash
 * set to empty string, then populates the obs_hash field.
 *
 * SRS-004-SHALL-046: Observation hash
 *
 * @param[in,out] obs  Observation record (obs_hash will be populated)
 *
 * @return AX_OK on success, error code on failure
 */
int ax_obs_compute_hash(ax_obs_record_t *obs);

#endif /* AXILOG_HASH_H */

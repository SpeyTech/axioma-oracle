/**
 * @file hash.h
 * @brief Hashing interface for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * SHA-256 is provided exclusively via axilog_sha256() from libaxilog.
 * No alternative SHA-256 implementations are permitted in this module.
 *
 * Two operations are defined:
 *   axilog_sha256()  — plain digest, for provenance hashing (input_hash,
 *                      oracle chaining). No domain prefix.
 *   axilog_commit()  — domain-separated commitment per DVEC-001 §4.3,
 *                      for evidence records (obs_hash). Tagged, length-prefixed.
 *
 * Rationale:
 *   A single cryptographic substrate shared across axioma-l0, axioma-oracle,
 *   and the Rust/TypeScript/Python SDKs. Prevents divergence between commitment
 *   and internal hashing paths. Required for cross-build identity guarantees
 *   (DVEC-001) and proven by SDK golden vector test_golden_commit_obs.
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

/* libaxilog substrate — installed at sdk-root */
#include "axilog/sha256.h"      /* axilog_sha256() — plain digest */
#include "axilog/commitment.h"  /* axilog_commit() — domain-separated */

/* ========================================================================
 * Input Hashing
 * ======================================================================== */

/**
 * @brief Compute input hash for oracle input.
 *
 * Plain SHA-256 of the canonicalised input — no domain prefix.
 * input_hash is an input provenance digest, not an evidence commitment.
 * DVEC-001 §4.4 domain separation applies to evidence type tags only.
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

/* ========================================================================
 * Observation Hashing
 * ======================================================================== */

/**
 * @brief Compute domain-separated observation hash per DVEC-001 §4.3.
 *
 * commit(obs) = SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)
 * where payload = JCS(record with obs_hash="")
 *
 * Aligns with axioma-sdk commitObs() and is proven by SDK golden
 * vector test_golden_commit_obs.
 *
 * SRS-004-SHALL-046: Observation hash
 *
 * @param[in,out] obs  Observation record (obs_hash will be populated)
 *
 * @return AX_OK on success, AX_ERR_BUFFER or AX_ERR_HASH on failure
 */
int ax_obs_compute_hash(ax_obs_record_t *obs);

#endif /* AXILOG_HASH_H */

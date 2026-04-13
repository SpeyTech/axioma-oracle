/**
 * @file hash.c
 * @brief Hashing implementation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * SHA-256 and domain-separated commitment via libaxilog (sdk-root).
 * The standalone SHA-256 implementation has been replaced by:
 *   axilog_sha256()  — plain digest, for input_hash
 *   axilog_commit()  — domain-separated, for obs_hash (DVEC-001 §4.3)
 *
 * ax_format_hash_hex() is defined in canonical.c / canonical.h.
 *
 * @traceability SRS-004-SHALL-009, SRS-004-SHALL-046
 */

#include "axilog/hash.h"
#include "axilog/canonical.h"
#include <string.h>

/* ========================================================================
 * Input Hashing
 * ======================================================================== */

/*
 * SRS-004-SHALL-009: Prompt determinism
 *
 * Plain SHA-256 — no domain prefix.
 * input_hash is a provenance digest, not an evidence commitment.
 * DVEC-001 §4.4 domain separation applies to evidence type tags only.
 */
void ax_compute_input_hash(uint8_t hash[32], const char *input, size_t input_len)
{
    axilog_sha256(hash, (const uint8_t *)input, input_len);
}

/* ========================================================================
 * Observation Hashing
 * ======================================================================== */

/*
 * SRS-004-SHALL-046: Observation hash
 *
 * Domain-separated commitment per DVEC-001 §4.3:
 *   obs_hash = SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)
 *   where payload = JCS(record with obs_hash="")
 *
 * axilog_commit() signature (axilog/commitment.h):
 *   void axilog_commit(const char *tag, const uint8_t *payload,
 *                      uint64_t payload_len, uint8_t out_commit[32],
 *                      ct_fault_flags_t *faults);
 *
 * Aligns with axioma-sdk commitObs(), proven by SDK golden vector
 * test_golden_commit_obs. The former implementation used plain SHA-256;
 * this replaces it with the stack-wide domain-separated format.
 */
int ax_obs_compute_hash(ax_obs_record_t *obs)
{
    char             canonical_buf[AX_CANONICAL_BUFFER_SIZE];
    uint8_t          hash[AX_HASH_SIZE];
    ct_fault_flags_t faults;
    int              len;

    /* Step 1: Set obs_hash to empty string — breaks circular dependency */
    obs->obs_hash[0] = '\0';

    /* Step 2: Canonicalise record with obs_hash="" */
    len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), obs, 0);
    if (len < 0) {
        return AX_ERR_BUFFER;
    }

    /* Step 3: Domain-separated commitment per DVEC-001 §4.3 */
    memset(&faults, 0, sizeof(faults));
    axilog_commit(
        "AX:OBS:v1",                     /* tag — ASCII, no null terminator in hash */
        (const uint8_t *)canonical_buf,  /* payload = JCS of record with obs_hash="" */
        (uint64_t)len,                   /* payload_len */
        hash,                            /* out_commit[32] */
        &faults                          /* faults */
    );
    if (ct_fault_any(&faults)) {
        return AX_ERR_HASH;
    }

    /* Step 4: Hex-encode commitment into obs->obs_hash
     * ax_format_hash_hex() is defined in canonical.c / canonical.h */
    ax_format_hash_hex(obs->obs_hash, sizeof(obs->obs_hash), hash, AX_HASH_SIZE);

    return AX_OK;
}

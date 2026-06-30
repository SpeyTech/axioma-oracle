/**
 * @file obs.h
 * @brief AX:OBS:v1 observation record structure and operations
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D3 — Bounded Non-Deterministic (oracle layer)
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-001, SRS-004-SHALL-002, SRS-004-SHALL-003,
 *               SRS-004-SHALL-004, SRS-004-SHALL-005, SRS-004-SHALL-006,
 *               SRS-004-SHALL-016, SRS-004-SHALL-017, SRS-004-SHALL-018,
 *               SRS-004-SHALL-019, SRS-004-SHALL-020, SRS-004-SHALL-021,
 *               SRS-004-SHALL-022, SRS-004-SHALL-023, SRS-004-SHALL-024,
 *               SRS-004-SHALL-027, SRS-004-SHALL-033, SRS-004-SHALL-034,
 *               SRS-004-SHALL-035, SRS-004-SHALL-038, SRS-004-SHALL-041,
 *               SRS-004-SHALL-046, SRS-004-SHALL-047
 */

#ifndef AXILOG_OBS_H
#define AXILOG_OBS_H

#include <stdint.h>
#include <stddef.h>
#include "axilog/types.h"
#include "axilog/limits.h"
#include "axilog/oracle.h"

/* ========================================================================
 * AX:OBS:v1 Record Structure
 * ======================================================================== */

/**
 * @brief AX:OBS:v1 observation record.
 *
 * This structure represents a canonical oracle observation. All fields
 * are ordered lexicographically for RFC 8785 (JCS) compliance.
 *
 * Field order (lexicographic):
 *   completion_state, failure_type, input_hash, ledger_seq, model_id,
 *   obs_hash, oracle_id, output, output_size, params, schema_version
 *
 * SRS-004-SHALL-001: Containment principle — oracle outputs wrapped
 * SRS-004-SHALL-002: Deterministic downstream — canonical form enables replay
 * SRS-004-SHALL-004: No direct use — raw oracle output never used directly
 * SRS-004-SHALL-006: Canonicalisation — RFC 8785 (JCS) format
 * SRS-004-SHALL-035: Observation completeness
 * SRS-004-SHALL-041: Atomic output — no partial observations
 * SRS-004-SHALL-046: Observation hash
 * SRS-004-SHALL-047: Schema versioning
 */
typedef struct {
    ax_completion_state_t completion_state;  /**< COMPLETE, TRUNCATED, or ERROR */
    ax_failure_type_t     failure_type;      /**< NULL, TIMEOUT, INVALID_OUTPUT, TRANSPORT_ERROR */

    uint8_t  input_hash[AX_HASH_SIZE];       /**< SHA-256 of canonical input */
    uint64_t ledger_seq;                     /**< Ordering anchor */

    const char *model_id;                    /**< Model/version identifier */
    char        obs_hash[AX_HASH_HEX_SIZE];  /**< SHA-256 of canonical record (hex) */

    const char *oracle_id;                   /**< Identity of oracle */
    const char *output;                      /**< Canonicalised, normalised output */

    uint64_t output_size;                    /**< Byte length of output */

    ax_oracle_params_t params;               /**< Sampling parameters */

    const char *schema_version;              /**< Always "AX:OBS:v1" */
} ax_obs_record_t;

/* ========================================================================
 * Input Structure (Raw Oracle Output)
 * ======================================================================== */

/**
 * @brief Raw oracle output before admission.
 *
 * This structure holds the raw data from an oracle invocation before
 * it is canonicalised and admitted as an AX:OBS:v1 record.
 */
typedef struct {
    ax_completion_state_t completion_state;
    ax_failure_type_t     failure_type;

    const char *input;                       /**< Raw input/prompt (to be hashed) */
    size_t      input_len;                   /**< Length of input in bytes */

    uint64_t    ledger_seq;                  /**< Ledger sequence number */

    const char *model_id;                    /**< Model identifier */
    const char *oracle_id;                   /**< Oracle identifier */

    const char *output;                      /**< Raw output (to be normalised) */
    size_t      output_len;                  /**< Length of output in bytes */

    ax_oracle_params_t params;               /**< Sampling parameters */
} ax_obs_input_t;

/* ========================================================================
 * Admission Context
 * ======================================================================== */

/**
 * @brief Admission context for tracking state.
 *
 * Maintains the last seen ledger sequence for ordering enforcement.
 *
 * SRS-004-SHALL-038
 */
typedef struct {
    uint64_t last_ledger_seq;  /**< Last admitted ledger_seq */
    int      initialized;      /**< Whether context has been used */
} ax_admission_ctx_t;

/**
 * @brief Initialize admission context.
 */
static inline void ax_admission_ctx_init(ax_admission_ctx_t *ctx) {
    ctx->last_ledger_seq = 0;
    ctx->initialized = 0;
}

/* ========================================================================
 * Core Functions
 * ======================================================================== */

/**
 * @brief Admit an oracle observation.
 *
 * This is the main entry point for L3. It:
 *   1. Validates encoding (UTF-8 + NFC)
 *   2. Normalises line endings
 *   3. Enforces ordering (ledger_seq monotonicity)
 *   4. Enforces size bounds
 *   5. Computes input_hash
 *   6. Computes obs_hash
 *   7. Populates the canonical AX:OBS:v1 record
 *
 * SRS-004-SHALL-003: Mandatory admission
 * SRS-004-SHALL-038: Observation ordering guard
 * SRS-004-SHALL-040: Output size enforcement
 * SRS-004-SHALL-042: Encoding canonicality
 * SRS-004-SHALL-043: Output normalisation
 *
 * @param[out] out         Destination observation record (caller-owned)
 * @param[out] output_buf  Buffer for normalised output (caller-owned)
 * @param[in]  output_buf_size  Size of output buffer
 * @param[in]  in          Raw oracle input
 * @param[in,out] ctx      Admission context (for ordering)
 * @param[out] faults      Fault flags
 *
 * @return AX_OK on success, error code on failure
 */
int ax_obs_admit(
    ax_obs_record_t     *out,
    char                *output_buf,
    size_t               output_buf_size,
    const ax_obs_input_t *in,
    ax_admission_ctx_t  *ctx,
    ct_fault_flags_t    *faults
);

/**
 * @brief Initialize an observation record to empty state.
 */
void ax_obs_init(ax_obs_record_t *obs);

/**
 * @brief Validate an observation record.
 *
 * Checks:
 *   - schema_version == "AX:OBS:v1"
 *   - encoding valid
 *   - params canonical
 *   - obs_hash recomputation matches
 *
 * SRS-004-SHALL-047: Schema versioning
 *
 * @param[in]  obs     Observation record to validate
 * @param[out] faults  Fault flags
 *
 * @return AX_OK if valid, error code otherwise
 */
int ax_obs_validate(const ax_obs_record_t *obs, ct_fault_flags_t *faults);

#endif /* AXILOG_OBS_H */

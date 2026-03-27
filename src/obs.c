/**
 * @file obs.c
 * @brief AX:OBS:v1 observation record implementation
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D3 — Bounded Non-Deterministic (oracle layer)
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-003, SRS-004-SHALL-005, SRS-004-SHALL-009,
 *               SRS-004-SHALL-010, SRS-004-SHALL-011, SRS-004-SHALL-015,
 *               SRS-004-SHALL-020, SRS-004-SHALL-021, SRS-004-SHALL-022,
 *               SRS-004-SHALL-023, SRS-004-SHALL-024, SRS-004-SHALL-027,
 *               SRS-004-SHALL-033, SRS-004-SHALL-034, SRS-004-SHALL-035,
 *               SRS-004-SHALL-037, SRS-004-SHALL-038, SRS-004-SHALL-040,
 *               SRS-004-SHALL-042, SRS-004-SHALL-043, SRS-004-SHALL-044,
 *               SRS-004-SHALL-046, SRS-004-SHALL-047, SRS-004-SHALL-048
 */

#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"
#include "axilog/validate.h"
#include <string.h>

/* ========================================================================
 * Initialization
 * ======================================================================== */

/**
 * Initialize an observation record to empty state.
 */
void ax_obs_init(ax_obs_record_t *obs)
{
    memset(obs, 0, sizeof(*obs));
    obs->completion_state = AX_COMPLETION_COMPLETE;
    obs->failure_type = AX_FAILURE_NULL;
    obs->schema_version = AX_OBS_SCHEMA_VERSION;
    ax_oracle_params_init_null(&obs->params);
}

/* ========================================================================
 * Admission
 * ======================================================================== */

/**
 * SRS-004-SHALL-003: Mandatory admission
 * SRS-004-SHALL-038: Observation ordering guard
 * SRS-004-SHALL-040: Output size enforcement
 * SRS-004-SHALL-042: Encoding canonicality
 * SRS-004-SHALL-043: Output normalisation
 * SRS-004-SHALL-046: Observation hash
 * SRS-004-SHALL-047: Schema versioning
 * SRS-004-SHALL-048: Maximum observation size
 */
int ax_obs_admit(
    ax_obs_record_t      *out,
    char                 *output_buf,
    size_t                output_buf_size,
    const ax_obs_input_t *in,
    ax_admission_ctx_t   *ctx,
    ct_fault_flags_t     *faults)
{
    int result;
    int normalised_len;

    /* Initialize output record */
    ax_obs_init(out);

    /* ================================================================
     * SRS-004-SHALL-038: Observation ordering guard
     * ================================================================ */
    if (ctx->initialized && in->ledger_seq <= ctx->last_ledger_seq) {
        faults->ordering = 1;
        out->completion_state = AX_COMPLETION_ERROR;
        out->failure_type = AX_FAILURE_INVALID_OUTPUT;
        return AX_ERR_ORDERING;
    }

    /* ================================================================
     * SRS-004-SHALL-048: Maximum observation size
     * SRS-004-SHALL-040: Output size enforcement
     * ================================================================ */
    if (in->output_len > AX_MAX_OUTPUT_BYTES) {
        faults->size = 1;
        out->completion_state = AX_COMPLETION_TRUNCATED;
        out->output_size = in->output_len;
        /* Continue with truncated indication - still produce record */
    }

    /* ================================================================
     * SRS-004-SHALL-042: Encoding canonicality
     * SRS-004-SHALL-043: Output normalisation
     * ================================================================ */
    if (in->output != NULL && in->output_len > 0) {
        /* Validate and normalise output */
        size_t len_to_process = in->output_len;
        if (len_to_process > AX_MAX_OUTPUT_BYTES) {
            len_to_process = AX_MAX_OUTPUT_BYTES;
        }

        normalised_len = ax_validate_and_normalise(
            output_buf,
            output_buf_size,
            in->output,
            len_to_process,
            faults
        );

        if (normalised_len < 0) {
            out->completion_state = AX_COMPLETION_ERROR;
            out->failure_type = AX_FAILURE_INVALID_OUTPUT;
            return normalised_len;
        }

        out->output = output_buf;
        /* SRS-004-SHALL-040: When truncated, output_size records original size */
        if (out->completion_state != AX_COMPLETION_TRUNCATED) {
            out->output_size = (uint64_t)normalised_len;
        }
        /* If TRUNCATED, output_size was already set to in->output_len above */
    } else {
        out->output = "";
        out->output_size = 0;
    }

    /* ================================================================
     * Copy completion state and failure type from input
     * ================================================================ */
    if (out->completion_state == AX_COMPLETION_COMPLETE) {
        out->completion_state = in->completion_state;
    }
    if (out->failure_type == AX_FAILURE_NULL) {
        out->failure_type = in->failure_type;
    }

    /* ================================================================
     * SRS-004-SHALL-009: Prompt determinism (input hash)
     * ================================================================ */
    if (in->input != NULL && in->input_len > 0) {
        ax_compute_input_hash(out->input_hash, in->input, in->input_len);
    } else {
        memset(out->input_hash, 0, AX_HASH_SIZE);
    }

    /* ================================================================
     * Copy remaining fields
     * ================================================================ */
    out->ledger_seq = in->ledger_seq;
    out->model_id = in->model_id;
    out->oracle_id = in->oracle_id;
    out->params = in->params;

    /* ================================================================
     * SRS-004-SHALL-047: Schema versioning
     * ================================================================ */
    out->schema_version = AX_OBS_SCHEMA_VERSION;

    /* ================================================================
     * SRS-004-SHALL-046: Observation hash
     * ================================================================ */
    result = ax_obs_compute_hash(out);
    if (result != AX_OK) {
        faults->protocol = 1;
        return result;
    }

    /* ================================================================
     * Update admission context
     * ================================================================ */
    ctx->last_ledger_seq = in->ledger_seq;
    ctx->initialized = 1;

    return AX_OK;
}

/* ========================================================================
 * Validation
 * ======================================================================== */

/**
 * SRS-004-SHALL-047: Schema versioning
 *
 * Validate an observation record:
 *   - schema_version == "AX:OBS:v1"
 *   - encoding valid
 *   - params canonical
 *   - obs_hash recomputation matches
 */
int ax_obs_validate(const ax_obs_record_t *obs, ct_fault_flags_t *faults)
{
    char canonical_buf[AX_CANONICAL_BUFFER_SIZE];
    uint8_t recomputed_hash[AX_HASH_SIZE];
    char recomputed_hex[AX_HASH_HEX_SIZE];
    int len;

    /* Check schema version */
    if (obs->schema_version == NULL ||
        strcmp(obs->schema_version, AX_OBS_SCHEMA_VERSION) != 0) {
        faults->schema = 1;
        return AX_ERR_SCHEMA;
    }

    /* Validate output encoding */
    if (obs->output != NULL && obs->output_size > 0) {
        size_t actual_len = strlen(obs->output);
        /* For truncated records, validate actual content, not recorded original size */
        int enc_result = ax_validate_encoding(obs->output, actual_len);
        if (enc_result != AX_OK) {
            faults->encoding = 1;
            return enc_result;
        }
    }

    /* Validate output_size consistency */
    /* SRS-004-SHALL-040: For truncated records, output_size = original size */
    if (obs->output != NULL) {
        size_t actual_len = strlen(obs->output);
        if (obs->completion_state == AX_COMPLETION_TRUNCATED) {
            /* Truncated: output_size >= actual_len */
            if (obs->output_size < actual_len) {
                faults->schema = 1;
                return AX_ERR_SCHEMA;
            }
        } else {
            /* Not truncated: output_size == actual_len */
            if (actual_len != obs->output_size) {
                faults->schema = 1;
                return AX_ERR_SCHEMA;
            }
        }
    }

    /* Recompute obs_hash and verify */
    /* Step 1: Canonicalise without hash */
    ax_obs_record_t temp = *obs;
    temp.obs_hash[0] = '\0';

    len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &temp, 0);
    if (len < 0) {
        faults->protocol = 1;
        return AX_ERR_BUFFER;
    }

    /* Step 2: Hash */
    ax_sha256(recomputed_hash, (const uint8_t *)canonical_buf, (size_t)len);

    /* Step 3: Hex encode */
    ax_format_hash_hex(recomputed_hex, sizeof(recomputed_hex),
                       recomputed_hash, AX_HASH_SIZE);

    /* Step 4: Compare */
    if (strcmp(recomputed_hex, obs->obs_hash) != 0) {
        faults->protocol = 1;
        return AX_ERR_HASH;
    }

    return AX_OK;
}

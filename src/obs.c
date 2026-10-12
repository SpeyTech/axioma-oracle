/**
 * @file obs.c
 * @brief AX:OBS:v1 observation record implementation
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
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

/* Bounded strlen: 1 if s is longer than max bytes, else 0. */
static int id_too_long(const char *s, size_t max)
{
    size_t i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i <= max; i++) {
        if (s[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

/* ========================================================================
 * PCR-004 (B-07): fail-closed schema checks on the admission input
 * ======================================================================== */

/*
 * Identifier content rule: non-NULL, non-empty, valid UTF-8 (RFC 3629, the
 * same validator used for output), and no C0 control (0x00-0x1F, LF
 * included) or DEL (0x7F). NFC is not required for identifiers.
 * Precondition: the caller has already bounded the length (id_too_long),
 * so strlen() here scans at most AX_MAX_*_ID_BYTES + 1 bytes.
 */
static int id_is_well_formed(const char *s)
{
    size_t i;
    size_t n;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    n = strlen(s);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) {
            return 0;
        }
    }
    return ax_validate_utf8(s, n);
}

/*
 * Permitted (completion_state, failure_type) pairs:
 *   COMPLETE / NULL
 *   TRUNCATED / NULL
 *   ERROR / {TIMEOUT, INVALID_OUTPUT, TRANSPORT_ERROR}
 * Any value outside its declared enum is rejected by the same switch.
 * The comparisons are done on int so that out-of-range values stored in
 * the enum object are handled without relying on the enum's range.
 */
static int state_pair_is_permitted(ax_completion_state_t state,
                                   ax_failure_type_t ftype)
{
    int s = (int)state;
    int f = (int)ftype;

    if (s == (int)AX_COMPLETION_COMPLETE || s == (int)AX_COMPLETION_TRUNCATED) {
        return f == (int)AX_FAILURE_NULL;
    }
    if (s == (int)AX_COMPLETION_ERROR) {
        return f == (int)AX_FAILURE_TIMEOUT ||
               f == (int)AX_FAILURE_INVALID_OUTPUT ||
               f == (int)AX_FAILURE_TRANSPORT_ERROR;
    }
    return 0;
}

/*
 * Returns 1 when the admission input is schema-valid, else 0.
 * Empty input (input_len == 0, input NULL or not) is allowed: its
 * input_hash stays as v1 computes it (32 zero bytes). Correcting that to
 * SHA-256 of the empty string is deferred to AX:OBS:v2.
 */
static int input_is_schema_valid(const ax_obs_input_t *in)
{
    if (!id_is_well_formed(in->model_id) ||
        !id_is_well_formed(in->oracle_id)) {
        return 0;
    }
    if (!state_pair_is_permitted(in->completion_state, in->failure_type)) {
        return 0;
    }
    /* PCR-004 residual (a): an ERROR input whose output exceeds
     * AX_MAX_OUTPUT_BYTES would be relabelled TRUNCATED by the SHALL-040
     * truncation below while keeping its failure_type, giving a record
     * pair outside the permitted list (TRUNCATED/TIMEOUT and so on).
     * Reject it here, before truncation can relabel it. COMPLETE and
     * TRUNCATED inputs are not affected: SHALL-040 truncation of those
     * is unchanged. */
    if ((int)in->completion_state == (int)AX_COMPLETION_ERROR &&
        in->output_len > AX_MAX_OUTPUT_BYTES) {
        return 0;
    }
    if (in->input == NULL && in->input_len > 0) {
        return 0;
    }
    /* PCR-004 residual (b): output NULL with output_len > 0 was admitted
     * as an empty output with output_size 0 (or, over the limit, as an
     * empty TRUNCATED output with the claimed size). It mirrors the input
     * rule above. NULL output with output_len == 0 stays admissible. */
    if (in->output == NULL && in->output_len > 0) {
        return 0;
    }
    return 1;
}

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
    ax_l3_fault_flags_t     *faults)
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
     * Identifier bounds (limits.h). Unbounded identifiers were the
     * easiest trigger of the silent field drop fixed under PCR-001.
     * ================================================================ */
    if (id_too_long(in->model_id, AX_MAX_MODEL_ID_BYTES) ||
        id_too_long(in->oracle_id, AX_MAX_ORACLE_ID_BYTES)) {
        faults->size = 1;
        out->completion_state = AX_COMPLETION_ERROR;
        out->failure_type = AX_FAILURE_INVALID_OUTPUT;
        return AX_ERR_SIZE;
    }

    /* ================================================================
     * PCR-004 (B-07): fail closed on malformed input.
     *
     * Check order, chosen so that every case rejected before PCR-004
     * keeps its error code:
     *   1. ordering guard           -> AX_ERR_ORDERING (unchanged)
     *   2. identifier length bound  -> AX_ERR_SIZE     (unchanged)
     *   3. schema checks (here)     -> AX_ERR_SCHEMA   (new)
     *   4. output encoding, record size and the rest (unchanged)
     * The length bound runs before the identifier content scan, so the
     * scan is bounded. Nothing below this point has run on rejection:
     * no obs_hash is computed and the admission context is not touched.
     * ================================================================ */
    if (!input_is_schema_valid(in)) {
        faults->schema = 1;
        out->completion_state = AX_COMPLETION_ERROR;
        out->failure_type = AX_FAILURE_INVALID_OUTPUT;
        return AX_ERR_SCHEMA;
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
     * SRS-004-SHALL-048: Maximum observation size, enforced on the
     * complete record as stored (canonical form including obs_hash).
     * A record over AX_MAX_OBS_BYTES is rejected with AX_ERR_SIZE; it is
     * not truncated and not relabelled (owner decision 2026-10-12).
     * ================================================================ */
    {
        char record_buf[AX_MAX_OBS_BYTES + 1];
        if (ax_obs_canonicalise(record_buf, sizeof(record_buf), out, 1) < 0) {
            faults->size = 1;
            out->completion_state = AX_COMPLETION_ERROR;
            out->failure_type = AX_FAILURE_INVALID_OUTPUT;
            out->obs_hash[0] = '\0';
            return AX_ERR_SIZE;
        }
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
 *   - obs_hash recomputation matches (domain-separated per DVEC-001 §4.3)
 */
int ax_obs_validate(const ax_obs_record_t *obs, ax_l3_fault_flags_t *faults)
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

    /* Step 2: Domain-separated commitment per DVEC-001 §4.3
     * Matches ax_obs_compute_hash() exactly — same tag, same payload. */
    {
        ct_fault_flags_t val_faults; /* substrate layout — crosses axilog_commit (E-ABI-1 fixed) */
        ct_fault_init(&val_faults);
        axilog_commit(
            "AX:OBS:v1",
            (const uint8_t *)canonical_buf,
            (uint64_t)len,
            recomputed_hash,
            &val_faults
        );
        if (ct_fault_any(&val_faults)) {
            faults->protocol = 1;
            return AX_ERR_HASH;
        }
    }

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

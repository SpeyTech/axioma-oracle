/**
 * @file admission.c
 * @brief Admission helper functions for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D3 — Bounded Non-Deterministic (oracle layer)
 * MEMORY: Zero Dynamic Allocation
 *
 * This file provides helper functions for the admission process.
 * The main admission logic is in obs.c.
 *
 * @traceability SRS-004-SHALL-003, SRS-004-SHALL-039
 */

#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"
#include "axilog/validate.h"
#include <string.h>

/* ========================================================================
 * Input Initialisation
 * ======================================================================== */

/**
 * @brief Initialize observation input to empty state.
 *
 * Sets all fields to safe defaults.
 */
void ax_obs_input_init(ax_obs_input_t *in)
{
    memset(in, 0, sizeof(*in));
    in->completion_state = AX_COMPLETION_COMPLETE;
    in->failure_type = AX_FAILURE_NULL;
    ax_oracle_params_init_null(&in->params);
}

/* ========================================================================
 * Convenience Functions
 * ======================================================================== */

/**
 * @brief Create a complete observation from raw data.
 *
 * Helper that populates an ax_obs_input_t from raw oracle data.
 *
 * SRS-004-SHALL-035: Observation completeness
 */
void ax_obs_input_set_complete(
    ax_obs_input_t       *in,
    uint64_t              ledger_seq,
    const char           *oracle_id,
    const char           *model_id,
    const char           *input,
    size_t                input_len,
    const char           *output,
    size_t                output_len,
    const ax_oracle_params_t *params)
{
    ax_obs_input_init(in);

    in->completion_state = AX_COMPLETION_COMPLETE;
    in->failure_type = AX_FAILURE_NULL;
    in->ledger_seq = ledger_seq;
    in->oracle_id = oracle_id;
    in->model_id = model_id;
    in->input = input;
    in->input_len = input_len;
    in->output = output;
    in->output_len = output_len;

    if (params != NULL) {
        in->params = *params;
    }
}

/**
 * @brief Create a timeout observation.
 *
 * Helper for oracle timeout failures.
 *
 * SRS-004-SHALL-022: Timeout handling
 */
void ax_obs_input_set_timeout(
    ax_obs_input_t *in,
    uint64_t        ledger_seq,
    const char     *oracle_id,
    const char     *model_id,
    const char     *input,
    size_t          input_len)
{
    ax_obs_input_init(in);

    in->completion_state = AX_COMPLETION_ERROR;
    in->failure_type = AX_FAILURE_TIMEOUT;
    in->ledger_seq = ledger_seq;
    in->oracle_id = oracle_id;
    in->model_id = model_id;
    in->input = input;
    in->input_len = input_len;
    in->output = NULL;
    in->output_len = 0;
}

/**
 * @brief Create a transport error observation.
 *
 * Helper for network/protocol failures.
 *
 * SRS-004-SHALL-020: Oracle failure
 */
void ax_obs_input_set_transport_error(
    ax_obs_input_t *in,
    uint64_t        ledger_seq,
    const char     *oracle_id,
    const char     *model_id,
    const char     *input,
    size_t          input_len)
{
    ax_obs_input_init(in);

    in->completion_state = AX_COMPLETION_ERROR;
    in->failure_type = AX_FAILURE_TRANSPORT_ERROR;
    in->ledger_seq = ledger_seq;
    in->oracle_id = oracle_id;
    in->model_id = model_id;
    in->input = input;
    in->input_len = input_len;
    in->output = NULL;
    in->output_len = 0;
}

/**
 * @brief Create an invalid output observation.
 *
 * Helper for schema validation failures.
 *
 * SRS-004-SHALL-021: Invalid output
 */
void ax_obs_input_set_invalid_output(
    ax_obs_input_t *in,
    uint64_t        ledger_seq,
    const char     *oracle_id,
    const char     *model_id,
    const char     *input,
    size_t          input_len,
    const char     *output,
    size_t          output_len)
{
    ax_obs_input_init(in);

    in->completion_state = AX_COMPLETION_ERROR;
    in->failure_type = AX_FAILURE_INVALID_OUTPUT;
    in->ledger_seq = ledger_seq;
    in->oracle_id = oracle_id;
    in->model_id = model_id;
    in->input = input;
    in->input_len = input_len;
    in->output = output;
    in->output_len = output_len;
}

/* ========================================================================
 * Multi-Oracle Isolation
 * ======================================================================== */

/**
 * @brief Compute derivation hash for chained oracles.
 *
 * When Oracle B's input depends on Oracle A's output, this function
 * computes the derivation hash to ensure deterministic traceability.
 *
 * SRS-004-SHALL-039: Oracle isolation
 *
 * @param[out] hash         Output hash (32 bytes)
 * @param[in]  prev_obs     Previous observation (Oracle A)
 * @param[in]  derivation   Additional derivation context
 * @param[in]  deriv_len    Length of derivation context
 */
void ax_compute_derivation_hash(
    uint8_t                   hash[32],
    const ax_obs_record_t    *prev_obs,
    const char               *derivation,
    size_t                    deriv_len)
{
    ax_sha256_ctx_t ctx;

    ax_sha256_init(&ctx);

    /* Include previous observation's hash */
    ax_sha256_update(&ctx, (const uint8_t *)prev_obs->obs_hash,
                     strlen(prev_obs->obs_hash));

    /* Include derivation context */
    if (derivation != NULL && deriv_len > 0) {
        ax_sha256_update(&ctx, (const uint8_t *)derivation, deriv_len);
    }

    ax_sha256_final(&ctx, hash);
}

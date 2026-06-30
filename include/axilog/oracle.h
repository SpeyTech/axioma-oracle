/**
 * @file oracle.h
 * @brief Oracle types and constants for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D3 — Bounded Non-Deterministic (oracle layer)
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-007, SRS-004-SHALL-008, SRS-004-SHALL-012,
 *               SRS-004-SHALL-013, SRS-004-SHALL-014, SRS-004-SHALL-015,
 *               SRS-004-SHALL-035, SRS-004-SHALL-036, SRS-004-SHALL-047
 */

#ifndef AXILOG_ORACLE_H
#define AXILOG_ORACLE_H

#include <stdint.h>
#include "axilog/types.h"
#include "axilog/limits.h"

/* ========================================================================
 * Schema Version
 * ======================================================================== */

/**
 * @brief Schema version string for AX:OBS:v1 records.
 *
 * SRS-004-SHALL-047
 */
#define AX_OBS_SCHEMA_VERSION  "AX:OBS:v1"

/* ========================================================================
 * Completion State
 * ======================================================================== */

/**
 * @brief Completion state for oracle observations.
 *
 * Indicates whether oracle output was fully received.
 *
 * SRS-004-SHALL-013: Completion state — COMPLETE/TRUNCATED/ERROR
 * SRS-004-SHALL-035: Observation completeness
 */
typedef enum {
    AX_COMPLETION_COMPLETE   = 0,  /**< Full output received */
    AX_COMPLETION_TRUNCATED  = 1,  /**< Output exceeded size bound */
    AX_COMPLETION_ERROR      = 2   /**< Oracle invocation failed */
} ax_completion_state_t;

/* ========================================================================
 * Failure Type
 * ======================================================================== */

/**
 * @brief Failure type for oracle observations.
 *
 * Classifies the reason for oracle invocation failure.
 *
 * SRS-004-SHALL-014: Failure type — NULL/TIMEOUT/INVALID_OUTPUT/TRANSPORT_ERROR
 * SRS-004-SHALL-035: Observation completeness
 */
typedef enum {
    AX_FAILURE_NULL            = 0,  /**< No failure */
    AX_FAILURE_TIMEOUT         = 1,  /**< Oracle did not respond in time */
    AX_FAILURE_INVALID_OUTPUT  = 2,  /**< Output failed schema validation */
    AX_FAILURE_TRANSPORT_ERROR = 3   /**< Network or protocol failure */
} ax_failure_type_t;

/* ========================================================================
 * Sampling Parameters
 * ======================================================================== */

/**
 * @brief Sentinel value for null int32.
 */
#define AX_PARAMS_NULL_INT32  (-1)

/**
 * @brief Sentinel value for null int64.
 */
#define AX_PARAMS_NULL_INT64  (-1LL)

/**
 * @brief Sentinel value for null Q16.16.
 */
#define AX_PARAMS_NULL_Q16    (INT32_MIN)

/**
 * @brief Canonical sampling parameters.
 *
 * Field order: max_tokens, seed, temperature, top_p (lexicographic)
 * NULL values represented by sentinel values defined above.
 *
 * SRS-004-SHALL-012: Sampling recording — temperature/top_p/seed captured
 * SRS-004-SHALL-036: Parameter canonicalisation
 */
typedef struct {
    int32_t  max_tokens;   /**< Maximum output tokens (-1 = null) */
    int64_t  seed;         /**< Random seed (-1 = null) */
    q16_16_t temperature;  /**< Sampling temperature (INT32_MIN = null) */
    q16_16_t top_p;        /**< Nucleus sampling threshold (INT32_MIN = null) */
} ax_oracle_params_t;

/**
 * @brief Initialize params to all-null state.
 */
static inline void ax_oracle_params_init_null(ax_oracle_params_t *params) {
    params->max_tokens = AX_PARAMS_NULL_INT32;
    params->seed = AX_PARAMS_NULL_INT64;
    params->temperature = AX_PARAMS_NULL_Q16;
    params->top_p = AX_PARAMS_NULL_Q16;
}

/**
 * @brief Check if max_tokens is null.
 */
static inline int ax_params_max_tokens_is_null(const ax_oracle_params_t *p) {
    return p->max_tokens == AX_PARAMS_NULL_INT32;
}

/**
 * @brief Check if seed is null.
 */
static inline int ax_params_seed_is_null(const ax_oracle_params_t *p) {
    return p->seed == AX_PARAMS_NULL_INT64;
}

/**
 * @brief Check if temperature is null.
 */
static inline int ax_params_temperature_is_null(const ax_oracle_params_t *p) {
    return p->temperature == AX_PARAMS_NULL_Q16;
}

/**
 * @brief Check if top_p is null.
 */
static inline int ax_params_top_p_is_null(const ax_oracle_params_t *p) {
    return p->top_p == AX_PARAMS_NULL_Q16;
}

#endif /* AXILOG_ORACLE_H */

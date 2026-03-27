/**
 * @file types.h
 * @brief Core types for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-029, SRS-004-SHALL-030, SRS-004-SHALL-031,
 *               SRS-004-SHALL-025, SRS-004-SHALL-026
 */

#ifndef AXILOG_TYPES_H
#define AXILOG_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * Fixed-Point Types
 * ======================================================================== */

/**
 * @brief Q16.16 fixed-point type.
 *
 * Range: [-32768.0, 32767.99998]
 * Resolution: 1/65536 ≈ 0.000015
 */
typedef int32_t q16_16_t;

#define Q16_16_ONE   (65536)
#define Q16_16_HALF  (32768)
#define Q16_16_MAX   INT32_MAX
#define Q16_16_MIN   INT32_MIN

/**
 * @brief Convert integer to Q16.16 fixed-point.
 *
 * Uses multiplication instead of left-shift to avoid C99 undefined
 * behaviour on negative values.
 *
 * SRS-004-SHALL-029
 */
#define Q16_16_FROM_INT(x)  ((q16_16_t)((x) * Q16_16_ONE))

/* ========================================================================
 * Fault Flags
 * ======================================================================== */

/**
 * @brief Fault flag structure for deterministic error propagation.
 *
 * All arithmetic and validation operations accept a fault context.
 * Once any flag is set, the system transitions to FAILED state.
 *
 * SRS-004-SHALL-025: Failure propagation — faults propagate to L4/L5
 * SRS-004-SHALL-026: Failure totality — all failure modes handled
 * SRS-004-SHALL-030: Arithmetic wrappers — overflow detection
 */
typedef struct {
    uint32_t overflow    : 1;  /**< Saturated high */
    uint32_t underflow   : 1;  /**< Saturated low */
    uint32_t div_zero    : 1;  /**< Division by zero */
    uint32_t domain      : 1;  /**< Invalid input domain */
    uint32_t precision   : 1;  /**< Precision loss detected */
    uint32_t encoding    : 1;  /**< UTF-8 / canonicalisation failure */
    uint32_t schema      : 1;  /**< Schema validation failure */
    uint32_t ordering    : 1;  /**< Ledger sequence violation */
    uint32_t size        : 1;  /**< Size bound exceeded */
    uint32_t protocol    : 1;  /**< Protocol violation */
    uint32_t _reserved   : 22;
} ct_fault_flags_t;

/**
 * @brief Check if any fault flag is set.
 */
static inline int ct_fault_any(const ct_fault_flags_t *f) {
    return f->overflow || f->underflow || f->div_zero || f->domain ||
           f->precision || f->encoding || f->schema || f->ordering ||
           f->size || f->protocol;
}

/**
 * @brief Clear all fault flags.
 */
static inline void ct_fault_clear(ct_fault_flags_t *f) {
    f->overflow = 0;
    f->underflow = 0;
    f->div_zero = 0;
    f->domain = 0;
    f->precision = 0;
    f->encoding = 0;
    f->schema = 0;
    f->ordering = 0;
    f->size = 0;
    f->protocol = 0;
}

/* ========================================================================
 * Return Codes
 * ======================================================================== */

#define AX_OK           0
#define AX_ERR_ENCODING (-1)
#define AX_ERR_SCHEMA   (-2)
#define AX_ERR_ORDERING (-3)
#define AX_ERR_SIZE     (-4)
#define AX_ERR_HASH     (-5)
#define AX_ERR_BUFFER   (-6)
#define AX_ERR_PROTOCOL (-7)

#endif /* AXILOG_TYPES_H */

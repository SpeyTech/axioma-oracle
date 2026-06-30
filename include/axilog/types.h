/**
 * @file types.h
 * @brief Core types for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
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

typedef int32_t q16_16_t;

#define Q16_16_ONE   (65536)
#define Q16_16_HALF  (32768)
#define Q16_16_MAX   INT32_MAX
#define Q16_16_MIN   INT32_MIN

#define Q16_16_FROM_INT(x)  ((q16_16_t)((x) * Q16_16_ONE))

/* ========================================================================
 * Fault Flags
 * ======================================================================== */

/*
 * NO BITFIELDS — bitfield layout is implementation-defined in C99 and
 * breaks cross-platform determinism (DVEC-001 §12.1). Each flag is a
 * uint8_t: zero = clear, non-zero = fault.
 *
 * SRS-004-SHALL-025, SRS-004-SHALL-026, SRS-004-SHALL-030
 */
typedef struct {
    uint8_t overflow;    /* Arithmetic overflow */
    uint8_t underflow;   /* Arithmetic underflow */
    uint8_t div_zero;    /* Division by zero */
    uint8_t domain;      /* Invalid input domain */
    uint8_t precision;   /* Precision loss detected */
    uint8_t encoding;    /* UTF-8 / canonicalisation failure */
    uint8_t schema;      /* Schema validation failure */
    uint8_t ordering;    /* Ledger sequence violation */
    uint8_t size;        /* Size bound exceeded */
    uint8_t protocol;    /* Protocol violation */
    uint8_t _pad[6];     /* Reserved — zero-initialised, never read */
} ct_fault_flags_t;

/* Any non-zero byte is a fault — bitwise OR, not logical */
static inline int ct_fault_any(const ct_fault_flags_t *f) {
    return (int)(f->overflow | f->underflow | f->div_zero | f->domain |
                 f->precision | f->encoding | f->schema | f->ordering |
                 f->size | f->protocol);
}

static inline void ct_fault_clear(ct_fault_flags_t *f) {
    f->overflow  = 0; f->underflow = 0; f->div_zero  = 0;
    f->domain    = 0; f->precision = 0; f->encoding  = 0;
    f->schema    = 0; f->ordering  = 0; f->size      = 0;
    f->protocol  = 0;
    f->_pad[0] = f->_pad[1] = f->_pad[2] =
    f->_pad[3] = f->_pad[4] = f->_pad[5] = 0;
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

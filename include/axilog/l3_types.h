/**
 * @file l3_types.h
 * @brief Core types for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * E-ABI-1 closure: this header replaces the former include/axilog/types.h,
 * which redefined ct_fault_flags_t under the substrate's guard and path.
 * The substrate's type is now adopted from axioma-spec (the real
 * axilog/types.h, reached through AXILOG_SDK_ROOT), and the richer L3
 * fault set lives here under its own name, following the axioma-l0
 * Phase 4 pattern (l0_errors.h): substrate base at offset 0, extension
 * flags after it, compile-time layout asserts.
 *
 * @traceability SRS-004-SHALL-029, SRS-004-SHALL-030, SRS-004-SHALL-031,
 *               SRS-004-SHALL-025, SRS-004-SHALL-026
 */

#ifndef AXILOG_L3_TYPES_H
#define AXILOG_L3_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* The substrate's types.h — ct_fault_flags_t (8 bytes), ct_fault_any(),
 * ct_fault_init(). With the oracle's shadow copy removed, this path
 * resolves only in the sdk-root include tree. */
#include <axilog/types.h>

/* C99 compile-time assert (no _Static_assert before C11) — same form
 * as L0_STATIC_ASSERT in axioma-l0/include/l0_errors.h. */
#define AX_L3_STATIC_ASSERT(cond, msg) \
    typedef char ax_l3_static_assert_##msg[(cond) ? 1 : -1]

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
 * L3 Fault Flags
 * ======================================================================== */

/*
 * NO BITFIELDS — bitfield layout is implementation-defined in C99 and
 * breaks cross-platform determinism (DVEC-001 §12.1). Each flag is a
 * uint8_t: zero = clear, non-zero = fault.
 *
 * base at offset 0 is load-bearing: a pointer to an ax_l3_fault_flags_t
 * may pass &f->base across the axilog_commit boundary and the substrate
 * writes land in the substrate's own fields. overflow, underflow,
 * div_zero, and domain are reached through base; only the L3-specific
 * flags are declared here.
 *
 * SRS-004-SHALL-025, SRS-004-SHALL-026, SRS-004-SHALL-030
 */
typedef struct {
    ct_fault_flags_t base; /* substrate faults — offset 0, load-bearing */
    uint8_t precision;     /* Precision loss detected */
    uint8_t encoding;      /* UTF-8 / canonicalisation failure */
    uint8_t schema;        /* Schema validation failure */
    uint8_t ordering;      /* Ledger sequence violation */
    uint8_t size;          /* Size bound exceeded */
    uint8_t protocol;      /* Protocol violation */
    uint8_t _pad[2];       /* Reserved — zero-initialised, never read */
} ax_l3_fault_flags_t;

/* Any non-zero byte is a fault — bitwise OR, not logical */
static inline int ax_l3_fault_any(const ax_l3_fault_flags_t *f)
{
    return ct_fault_any(&f->base) |
           (int)(f->precision | f->encoding | f->schema |
                 f->ordering  | f->size     | f->protocol);
}

static inline void ax_l3_fault_init(ax_l3_fault_flags_t *f)
{
    ct_fault_init(&f->base);
    f->precision = 0;
    f->encoding  = 0;
    f->schema    = 0;
    f->ordering  = 0;
    f->size      = 0;
    f->protocol  = 0;
    f->_pad[0]   = 0;
    f->_pad[1]   = 0;
}

/* Compile-time layout assertions (C99 form — not _Static_assert) */
AX_L3_STATIC_ASSERT(sizeof(ct_fault_flags_t)    ==  8, substrate_fault_size);
AX_L3_STATIC_ASSERT(sizeof(ax_l3_fault_flags_t) == 16, l3_fault_size);
AX_L3_STATIC_ASSERT(offsetof(ax_l3_fault_flags_t, base) == 0, base_must_be_first);

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

#endif /* AXILOG_L3_TYPES_H */

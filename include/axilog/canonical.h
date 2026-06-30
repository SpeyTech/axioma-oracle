/**
 * @file canonical.h
 * @brief RFC 8785 (JCS) canonicalisation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-006, SRS-004-SHALL-036, SRS-004-SHALL-045
 */

#ifndef AXILOG_CANONICAL_H
#define AXILOG_CANONICAL_H

#include <stdint.h>
#include <stddef.h>
#include "axilog/types.h"
#include "axilog/obs.h"

/* ========================================================================
 * Canonicalisation Functions
 * ======================================================================== */

/**
 * @brief Canonicalise an AX:OBS:v1 record to JSON.
 *
 * Produces RFC 8785 (JCS) compliant JSON:
 *   - Lexicographic key ordering
 *   - No whitespace
 *   - Minimal string escaping (SHALL-045)
 *   - Deterministic number formatting
 *
 * SRS-004-SHALL-006: Canonicalisation
 * SRS-004-SHALL-045: String escaping canonicality
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  obs       Observation record to canonicalise
 * @param[in]  include_hash  Whether to include obs_hash (0 = empty string)
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_obs_canonicalise(
    char                   *dst,
    size_t                  dst_size,
    const ax_obs_record_t  *obs,
    int                     include_hash
);

/**
 * @brief Canonicalise params object to JSON.
 *
 * Field order: max_tokens, seed, temperature, top_p
 *
 * SRS-004-SHALL-036: Parameter canonicalisation
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  params    Parameters to canonicalise
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_params_canonicalise(
    char                     *dst,
    size_t                    dst_size,
    const ax_oracle_params_t *params
);

/**
 * @brief Canonicalise a string with minimal escaping.
 *
 * Escapes ONLY:
 *   - " (U+0022) → \"
 *   - \ (U+005C) → \\
 *   - Control characters (U+0000–U+001F) → \uXXXX
 *
 * Direct UTF-8 encoding used for all other characters.
 *
 * SRS-004-SHALL-045: String escaping canonicality
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  src       Source string (UTF-8)
 * @param[in]  src_len   Length of source string
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_string_escape(
    char       *dst,
    size_t      dst_size,
    const char *src,
    size_t      src_len
);

/**
 * @brief Format a signed 64-bit integer as decimal string.
 *
 * Deterministic formatting with no leading zeros.
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  value     Value to format
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_format_int64(char *dst, size_t dst_size, int64_t value);

/**
 * @brief Format an unsigned 64-bit integer as decimal string.
 *
 * Deterministic formatting with no leading zeros.
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  value     Value to format
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_format_uint64(char *dst, size_t dst_size, uint64_t value);

/**
 * @brief Format a Q16.16 fixed-point value as integer for JSON.
 *
 * Q16.16 values are stored as raw integers in JSON (no floating point).
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  value     Q16.16 value to format
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_format_q16(char *dst, size_t dst_size, q16_16_t value);

/**
 * @brief Format a hash as lowercase hex string.
 *
 * @param[out] dst       Destination buffer (at least 65 bytes for SHA-256)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  hash      Hash bytes
 * @param[in]  hash_len  Length of hash in bytes
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_format_hash_hex(char *dst, size_t dst_size, const uint8_t *hash, size_t hash_len);

#endif /* AXILOG_CANONICAL_H */

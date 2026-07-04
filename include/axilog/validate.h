/**
 * @file validate.h
 * @brief Encoding validation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-042, SRS-004-SHALL-043, SRS-004-SHALL-044
 */

#ifndef AXILOG_VALIDATE_H
#define AXILOG_VALIDATE_H

#include <stdint.h>
#include <stddef.h>
#include "axilog/l3_types.h"

/* ========================================================================
 * UTF-8 Validation
 * ======================================================================== */

/**
 * @brief Validate UTF-8 encoding.
 *
 * Checks that the input is valid UTF-8.
 *
 * SRS-004-SHALL-042: Encoding canonicality
 *
 * @param[in] data  Data to validate
 * @param[in] len   Length in bytes
 *
 * @return 1 if valid, 0 if invalid
 */
int ax_validate_utf8(const char *data, size_t len);

/**
 * @brief Check if string contains forbidden control characters.
 *
 * Control characters U+0000–U+001F are forbidden except U+000A (LF).
 *
 * SRS-004-SHALL-043: Output normalisation
 *
 * @param[in] data  Data to check
 * @param[in] len   Length in bytes
 *
 * @return 1 if contains forbidden characters, 0 otherwise
 */
int ax_contains_forbidden_control(const char *data, size_t len);

/* ========================================================================
 * Line Ending Normalisation
 * ======================================================================== */

/**
 * @brief Normalise line endings to LF.
 *
 * Converts:
 *   - CRLF (\r\n) → LF (\n)
 *   - CR (\r) → LF (\n)
 *
 * SRS-004-SHALL-043: Output normalisation
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  src       Source string
 * @param[in]  src_len   Length of source in bytes
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_normalise_line_endings(
    char       *dst,
    size_t      dst_size,
    const char *src,
    size_t      src_len
);

/* ========================================================================
 * NFC Normalisation
 * ======================================================================== */

/**
 * @brief Check if string is in NFC normal form.
 *
 * For simplicity in this implementation, we validate that the string
 * is already in NFC form rather than performing full normalisation.
 *
 * SRS-004-SHALL-042: Encoding canonicality
 *
 * @param[in] data  Data to check
 * @param[in] len   Length in bytes
 *
 * @return 1 if in NFC, 0 if not
 */
int ax_is_nfc(const char *data, size_t len);

/* ========================================================================
 * Combined Validation
 * ======================================================================== */

/**
 * @brief Full encoding validation.
 *
 * Validates:
 *   - Valid UTF-8
 *   - NFC normalised
 *   - No forbidden control characters
 *
 * @param[in] data  Data to validate
 * @param[in] len   Length in bytes
 *
 * @return AX_OK if valid, error code otherwise
 */
int ax_validate_encoding(const char *data, size_t len);

/**
 * @brief Validate and normalise output.
 *
 * Performs:
 *   - UTF-8 validation
 *   - Line ending normalisation
 *   - Control character rejection
 *
 * @param[out] dst       Destination buffer (caller-owned)
 * @param[in]  dst_size  Size of destination buffer
 * @param[in]  src       Source string
 * @param[in]  src_len   Length of source in bytes
 * @param[out] faults    Fault flags
 *
 * @return Number of bytes written (excluding null terminator), or negative on error
 */
int ax_validate_and_normalise(
    char             *dst,
    size_t            dst_size,
    const char       *src,
    size_t            src_len,
    ax_l3_fault_flags_t *faults
);

#endif /* AXILOG_VALIDATE_H */

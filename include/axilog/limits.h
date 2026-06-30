/**
 * @file limits.h
 * @brief Size limits and constants for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 *
 * @traceability SRS-004-SHALL-048
 */

#ifndef AXILOG_LIMITS_H
#define AXILOG_LIMITS_H

#include <stddef.h>

/* ========================================================================
 * Observation Limits
 * ======================================================================== */

/**
 * @brief Maximum size of an AX:OBS:v1 record in bytes.
 *
 * Any observation exceeding this limit SHALL be recorded with
 * completion_state = TRUNCATED.
 *
 * SRS-004-SHALL-048
 */
#define AX_MAX_OBS_BYTES  65536  /* 64 KiB */

/**
 * @brief Maximum size of output field in bytes.
 *
 * Must be less than AX_MAX_OBS_BYTES to account for other fields.
 */
#define AX_MAX_OUTPUT_BYTES  (AX_MAX_OBS_BYTES - 1024)

/**
 * @brief Maximum size of model_id field.
 */
#define AX_MAX_MODEL_ID_BYTES  256

/**
 * @brief Maximum size of oracle_id field.
 */
#define AX_MAX_ORACLE_ID_BYTES  256

/**
 * @brief Size of SHA-256 hash in bytes.
 */
#define AX_HASH_SIZE  32

/**
 * @brief Size of hex-encoded SHA-256 hash (including null terminator).
 */
#define AX_HASH_HEX_SIZE  65

/* ========================================================================
 * Canonicalisation Buffer Sizes
 * ======================================================================== */

/**
 * @brief Buffer size for canonical JSON output.
 */
#define AX_CANONICAL_BUFFER_SIZE  (AX_MAX_OBS_BYTES * 2)

/**
 * @brief Buffer size for params object.
 */
#define AX_PARAMS_BUFFER_SIZE  256

#endif /* AXILOG_LIMITS_H */

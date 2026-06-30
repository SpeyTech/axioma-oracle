/**
 * @file canonical.c
 * @brief RFC 8785 (JCS) canonicalisation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-006, SRS-004-SHALL-028, SRS-004-SHALL-031,
 *               SRS-004-SHALL-032, SRS-004-SHALL-036, SRS-004-SHALL-045
 */

#include "axilog/canonical.h"
#include "axilog/oracle.h"
#include <string.h>

/* ========================================================================
 * Helper: Append to Buffer
 * ======================================================================== */

typedef struct {
    char  *buf;
    size_t size;
    size_t pos;
    int    overflow;
} write_ctx_t;

static void write_init(write_ctx_t *ctx, char *buf, size_t size)
{
    ctx->buf = buf;
    ctx->size = size;
    ctx->pos = 0;
    ctx->overflow = 0;
}

static void write_char(write_ctx_t *ctx, char c)
{
    if (ctx->pos < ctx->size - 1) {
        ctx->buf[ctx->pos++] = c;
    } else {
        ctx->overflow = 1;
    }
}

static void write_str(write_ctx_t *ctx, const char *s)
{
    while (*s) {
        write_char(ctx, *s++);
    }
}

static void write_raw(write_ctx_t *ctx, const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        write_char(ctx, s[i]);
    }
}

static int write_finish(write_ctx_t *ctx)
{
    if (ctx->overflow) {
        return AX_ERR_BUFFER;
    }
    ctx->buf[ctx->pos] = '\0';
    return (int)ctx->pos;
}

/* ========================================================================
 * Number Formatting
 * ======================================================================== */

/**
 * Format signed 64-bit integer.
 */
int ax_format_int64(char *dst, size_t dst_size, int64_t value)
{
    char temp[24];
    int i = 0;
    int neg = 0;
    uint64_t uval;

    if (value < 0) {
        neg = 1;
        uval = (uint64_t)(-(value + 1)) + 1;
    } else {
        uval = (uint64_t)value;
    }

    /* Generate digits in reverse */
    if (uval == 0) {
        temp[i++] = '0';
    } else {
        while (uval > 0) {
            temp[i++] = (char)('0' + (int)(uval % 10));
            uval /= 10;
        }
    }

    if (neg) {
        temp[i++] = '-';
    }

    /* Check buffer size */
    if ((size_t)i >= dst_size) {
        return AX_ERR_BUFFER;
    }

    /* Reverse into destination */
    int len = i;
    while (i > 0) {
        *dst++ = temp[--i];
    }
    *dst = '\0';

    return len;
}

/**
 * Format unsigned 64-bit integer.
 */
int ax_format_uint64(char *dst, size_t dst_size, uint64_t value)
{
    char temp[24];
    int i = 0;

    if (value == 0) {
        temp[i++] = '0';
    } else {
        while (value > 0) {
            temp[i++] = (char)('0' + (int)(value % 10));
            value /= 10;
        }
    }

    if ((size_t)i >= dst_size) {
        return AX_ERR_BUFFER;
    }

    int len = i;
    while (i > 0) {
        *dst++ = temp[--i];
    }
    *dst = '\0';

    return len;
}

/**
 * Format Q16.16 as integer (raw value).
 */
int ax_format_q16(char *dst, size_t dst_size, q16_16_t value)
{
    return ax_format_int64(dst, dst_size, (int64_t)value);
}

/**
 * Format hash as lowercase hex.
 */
int ax_format_hash_hex(char *dst, size_t dst_size, const uint8_t *hash, size_t hash_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (dst_size < hash_len * 2 + 1) {
        return AX_ERR_BUFFER;
    }

    for (i = 0; i < hash_len; i++) {
        dst[i * 2 + 0] = hex[(hash[i] >> 4) & 0x0F];
        dst[i * 2 + 1] = hex[hash[i] & 0x0F];
    }
    dst[hash_len * 2] = '\0';

    return (int)(hash_len * 2);
}

/* ========================================================================
 * String Escaping
 * ======================================================================== */

/**
 * SRS-004-SHALL-045: String escaping canonicality
 *
 * Minimal escaping:
 *   - " → \"
 *   - \ → \\
 *   - Control chars (U+0000–U+001F) → \uXXXX
 *
 * Direct UTF-8 for everything else.
 */
int ax_string_escape(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    write_ctx_t ctx;
    size_t i;

    write_init(&ctx, dst, dst_size);

    for (i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c == '"') {
            write_str(&ctx, "\\\"");
        } else if (c == '\\') {
            write_str(&ctx, "\\\\");
        } else if (c < 0x20) {
            /* Control character → \uXXXX */
            char esc[7];
            static const char hex[] = "0123456789abcdef";
            esc[0] = '\\';
            esc[1] = 'u';
            esc[2] = '0';
            esc[3] = '0';
            esc[4] = hex[(c >> 4) & 0x0F];
            esc[5] = hex[c & 0x0F];
            esc[6] = '\0';
            write_str(&ctx, esc);
        } else {
            /* Direct UTF-8 */
            write_char(&ctx, (char)c);
        }
    }

    return write_finish(&ctx);
}

/* ========================================================================
 * Params Canonicalisation
 * ======================================================================== */

/**
 * SRS-004-SHALL-036: Parameter canonicalisation
 *
 * Field order: max_tokens, seed, temperature, top_p (lexicographic)
 */
int ax_params_canonicalise(char *dst, size_t dst_size, const ax_oracle_params_t *params)
{
    write_ctx_t ctx;
    char num_buf[24];

    write_init(&ctx, dst, dst_size);

    write_char(&ctx, '{');

    /* max_tokens */
    write_str(&ctx, "\"max_tokens\":");
    if (ax_params_max_tokens_is_null(params)) {
        write_str(&ctx, "null");
    } else {
        ax_format_int64(num_buf, sizeof(num_buf), params->max_tokens);
        write_str(&ctx, num_buf);
    }

    /* seed */
    write_str(&ctx, ",\"seed\":");
    if (ax_params_seed_is_null(params)) {
        write_str(&ctx, "null");
    } else {
        ax_format_int64(num_buf, sizeof(num_buf), params->seed);
        write_str(&ctx, num_buf);
    }

    /* temperature */
    write_str(&ctx, ",\"temperature\":");
    if (ax_params_temperature_is_null(params)) {
        write_str(&ctx, "null");
    } else {
        ax_format_q16(num_buf, sizeof(num_buf), params->temperature);
        write_str(&ctx, num_buf);
    }

    /* top_p */
    write_str(&ctx, ",\"top_p\":");
    if (ax_params_top_p_is_null(params)) {
        write_str(&ctx, "null");
    } else {
        ax_format_q16(num_buf, sizeof(num_buf), params->top_p);
        write_str(&ctx, num_buf);
    }

    write_char(&ctx, '}');

    return write_finish(&ctx);
}

/* ========================================================================
 * AX:OBS:v1 Canonicalisation
 * ======================================================================== */

/**
 * Helper: Write completion_state enum as string.
 */
static void write_completion_state(write_ctx_t *ctx, ax_completion_state_t state)
{
    switch (state) {
    case AX_COMPLETION_COMPLETE:
        write_str(ctx, "\"COMPLETE\"");
        break;
    case AX_COMPLETION_TRUNCATED:
        write_str(ctx, "\"TRUNCATED\"");
        break;
    case AX_COMPLETION_ERROR:
        write_str(ctx, "\"ERROR\"");
        break;
    default:
        write_str(ctx, "\"UNKNOWN\"");
        break;
    }
}

/**
 * Helper: Write failure_type enum as string or null.
 */
static void write_failure_type(write_ctx_t *ctx, ax_failure_type_t ftype)
{
    switch (ftype) {
    case AX_FAILURE_NULL:
        write_str(ctx, "null");
        break;
    case AX_FAILURE_TIMEOUT:
        write_str(ctx, "\"TIMEOUT\"");
        break;
    case AX_FAILURE_INVALID_OUTPUT:
        write_str(ctx, "\"INVALID_OUTPUT\"");
        break;
    case AX_FAILURE_TRANSPORT_ERROR:
        write_str(ctx, "\"TRANSPORT_ERROR\"");
        break;
    default:
        write_str(ctx, "\"UNKNOWN\"");
        break;
    }
}

/**
 * SRS-004-SHALL-006: Canonicalisation
 * SRS-004-SHALL-045: String escaping canonicality
 *
 * Field order (lexicographic):
 *   completion_state, failure_type, input_hash, ledger_seq, model_id,
 *   obs_hash, oracle_id, output, output_size, params, schema_version
 */
int ax_obs_canonicalise(
    char                   *dst,
    size_t                  dst_size,
    const ax_obs_record_t  *obs,
    int                     include_hash)
{
    write_ctx_t ctx;
    char num_buf[24];
    char hash_hex[AX_HASH_HEX_SIZE];
    char escaped[AX_MAX_OUTPUT_BYTES * 2];
    char params_buf[AX_PARAMS_BUFFER_SIZE];
    int esc_len;

    write_init(&ctx, dst, dst_size);

    write_char(&ctx, '{');

    /* completion_state */
    write_str(&ctx, "\"completion_state\":");
    write_completion_state(&ctx, obs->completion_state);

    /* failure_type */
    write_str(&ctx, ",\"failure_type\":");
    write_failure_type(&ctx, obs->failure_type);

    /* input_hash */
    write_str(&ctx, ",\"input_hash\":\"");
    ax_format_hash_hex(hash_hex, sizeof(hash_hex), obs->input_hash, AX_HASH_SIZE);
    write_str(&ctx, hash_hex);
    write_char(&ctx, '"');

    /* ledger_seq */
    write_str(&ctx, ",\"ledger_seq\":");
    ax_format_uint64(num_buf, sizeof(num_buf), obs->ledger_seq);
    write_str(&ctx, num_buf);

    /* model_id */
    write_str(&ctx, ",\"model_id\":\"");
    if (obs->model_id) {
        esc_len = ax_string_escape(escaped, sizeof(escaped),
                                   obs->model_id, strlen(obs->model_id));
        if (esc_len > 0) {
            write_raw(&ctx, escaped, (size_t)esc_len);
        }
    }
    write_char(&ctx, '"');

    /* obs_hash */
    write_str(&ctx, ",\"obs_hash\":\"");
    if (include_hash && obs->obs_hash[0] != '\0') {
        write_str(&ctx, obs->obs_hash);
    }
    /* Empty string if not including hash */
    write_char(&ctx, '"');

    /* oracle_id */
    write_str(&ctx, ",\"oracle_id\":\"");
    if (obs->oracle_id) {
        esc_len = ax_string_escape(escaped, sizeof(escaped),
                                   obs->oracle_id, strlen(obs->oracle_id));
        if (esc_len > 0) {
            write_raw(&ctx, escaped, (size_t)esc_len);
        }
    }
    write_char(&ctx, '"');

    /* output */
    write_str(&ctx, ",\"output\":\"");
    if (obs->output) {
        esc_len = ax_string_escape(escaped, sizeof(escaped),
                                   obs->output, strlen(obs->output));
        if (esc_len > 0) {
            write_raw(&ctx, escaped, (size_t)esc_len);
        }
    }
    write_char(&ctx, '"');

    /* output_size */
    write_str(&ctx, ",\"output_size\":");
    ax_format_uint64(num_buf, sizeof(num_buf), obs->output_size);
    write_str(&ctx, num_buf);

    /* params */
    write_str(&ctx, ",\"params\":");
    ax_params_canonicalise(params_buf, sizeof(params_buf), &obs->params);
    write_str(&ctx, params_buf);

    /* schema_version */
    write_str(&ctx, ",\"schema_version\":\"");
    if (obs->schema_version) {
        write_str(&ctx, obs->schema_version);
    }
    write_char(&ctx, '"');

    write_char(&ctx, '}');

    return write_finish(&ctx);
}

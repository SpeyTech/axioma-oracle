/**
 * @file validate.c
 * @brief Encoding validation for Axioma Oracle Boundary Gateway (L3)
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * @traceability SRS-004-SHALL-042, SRS-004-SHALL-043
 */

#include "axilog/validate.h"
#include <string.h>

/* ========================================================================
 * UTF-8 Validation
 * ======================================================================== */

/**
 * SRS-004-SHALL-042: Encoding canonicality
 *
 * Validates UTF-8 encoding per RFC 3629.
 */
int ax_validate_utf8(const char *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    const unsigned char *end = p + len;

    while (p < end) {
        unsigned char c = *p;

        if (c < 0x80) {
            /* ASCII: 0xxxxxxx */
            p++;
        } else if ((c & 0xE0) == 0xC0) {
            /* 2-byte: 110xxxxx 10xxxxxx */
            if (p + 1 >= end) return 0;
            if ((p[1] & 0xC0) != 0x80) return 0;
            /* Overlong check: must be >= 0x80 */
            if ((c & 0x1E) == 0) return 0;
            p += 2;
        } else if ((c & 0xF0) == 0xE0) {
            /* 3-byte: 1110xxxx 10xxxxxx 10xxxxxx */
            if (p + 2 >= end) return 0;
            if ((p[1] & 0xC0) != 0x80) return 0;
            if ((p[2] & 0xC0) != 0x80) return 0;
            /* Overlong check and surrogate check */
            uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            if (cp < 0x800) return 0;  /* Overlong */
            if (cp >= 0xD800 && cp <= 0xDFFF) return 0;  /* Surrogate */
            p += 3;
        } else if ((c & 0xF8) == 0xF0) {
            /* 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (p + 3 >= end) return 0;
            if ((p[1] & 0xC0) != 0x80) return 0;
            if ((p[2] & 0xC0) != 0x80) return 0;
            if ((p[3] & 0xC0) != 0x80) return 0;
            /* Range check: U+10000 to U+10FFFF */
            uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                         ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return 0;
            p += 4;
        } else {
            /* Invalid leading byte */
            return 0;
        }
    }

    return 1;
}

/* ========================================================================
 * Control Character Check
 * ======================================================================== */

/**
 * SRS-004-SHALL-044: Control character rejection
 *
 * Control characters U+0000–U+001F and U+007F (DEL) are forbidden,
 * except U+000A (LF) which is permitted for line breaks.
 */
int ax_contains_forbidden_control(const char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        /* Control chars U+0000–U+001F except U+000A (LF) */
        if (c < 0x20 && c != 0x0A) {
            return 1;  /* Found forbidden control character */
        }
        /* DEL (U+007F) is also forbidden — SRS-004-SHALL-044 */
        if (c == 0x7F) {
            return 1;  /* Found DEL control character */
        }
    }

    return 0;
}

/* ========================================================================
 * Line Ending Normalisation
 * ======================================================================== */

/**
 * SRS-004-SHALL-043: Output normalisation
 *
 * Converts CRLF and CR to LF.
 */
int ax_normalise_line_endings(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t di = 0;
    size_t si = 0;

    while (si < src_len && di < dst_size - 1) {
        char c = src[si];

        if (c == '\r') {
            /* Check for CRLF */
            if (si + 1 < src_len && src[si + 1] == '\n') {
                /* CRLF → LF */
                dst[di++] = '\n';
                si += 2;
            } else {
                /* CR alone → LF */
                dst[di++] = '\n';
                si++;
            }
        } else {
            dst[di++] = c;
            si++;
        }
    }

    /* Check if we consumed all input */
    if (si < src_len) {
        return AX_ERR_BUFFER;  /* Buffer too small */
    }

    dst[di] = '\0';
    return (int)di;
}

/* ========================================================================
 * NFC Check
 * ======================================================================== */

/**
 * SRS-004-SHALL-042: Encoding canonicality
 *
 * NFC validation. This implementation REJECTS text containing combining
 * diacritical marks that should have been precomposed.
 *
 * The Combining Diacritical Marks block (U+0300–U+036F) contains marks
 * that should typically be precomposed with their base characters in NFC.
 *
 * For safety-critical systems requiring full NFC:
 *   - Pre-process through ICU or equivalent before admission
 *   - This validation ensures determinism by rejecting non-NFC
 */
int ax_is_nfc(const char *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    const unsigned char *end = p + len;

    while (p < end) {
        unsigned char c = *p;

        if (c < 0x80) {
            /* ASCII - always NFC */
            p++;
        } else if ((c & 0xE0) == 0xC0) {
            /* 2-byte sequence */
            if (p + 1 >= end) return 0;

            /* Decode codepoint */
            uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);

            /* Reject Combining Diacritical Marks (U+0300–U+036F) */
            /* These indicate decomposed form that should be precomposed */
            if (cp >= 0x0300 && cp <= 0x036F) {
                return 0;  /* Reject: contains combining mark */
            }

            p += 2;
        } else if ((c & 0xF0) == 0xE0) {
            /* 3-byte sequence */
            if (p + 2 >= end) return 0;

            uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);

            /* Reject combining marks in extended ranges */
            /* U+1DC0–U+1DFF: Combining Diacritical Marks Supplement */
            /* U+20D0–U+20FF: Combining Diacritical Marks for Symbols */
            /* U+FE20–U+FE2F: Combining Half Marks */
            if ((cp >= 0x1DC0 && cp <= 0x1DFF) ||
                (cp >= 0x20D0 && cp <= 0x20FF) ||
                (cp >= 0xFE20 && cp <= 0xFE2F)) {
                return 0;  /* Reject: contains combining mark */
            }

            p += 3;
        } else if ((c & 0xF8) == 0xF0) {
            p += 4;
        } else {
            return 0;
        }
    }

    return 1;
}

/* ========================================================================
 * Combined Validation
 * ======================================================================== */

/**
 * Full encoding validation.
 */
int ax_validate_encoding(const char *data, size_t len)
{
    if (!ax_validate_utf8(data, len)) {
        return AX_ERR_ENCODING;
    }

    if (!ax_is_nfc(data, len)) {
        return AX_ERR_ENCODING;
    }

    return AX_OK;
}

/**
 * SRS-004-SHALL-042: Encoding canonicality
 * SRS-004-SHALL-043: Output normalisation
 *
 * Validate and normalise output:
 *   1. Check UTF-8 validity
 *   2. Normalise line endings
 *   3. Check for forbidden control characters
 */
int ax_validate_and_normalise(
    char             *dst,
    size_t            dst_size,
    const char       *src,
    size_t            src_len,
    ct_fault_flags_t *faults)
{
    int result;

    /* Step 1: Validate UTF-8 */
    if (!ax_validate_utf8(src, src_len)) {
        faults->encoding = 1;
        return AX_ERR_ENCODING;
    }

    /* Step 2: Normalise line endings */
    result = ax_normalise_line_endings(dst, dst_size, src, src_len);
    if (result < 0) {
        faults->size = 1;
        return result;
    }

    /* Step 3: Check for forbidden control characters */
    if (ax_contains_forbidden_control(dst, (size_t)result)) {
        faults->encoding = 1;
        return AX_ERR_ENCODING;
    }

    /* Step 4: NFC check */
    if (!ax_is_nfc(dst, (size_t)result)) {
        faults->encoding = 1;
        return AX_ERR_ENCODING;
    }

    return result;
}

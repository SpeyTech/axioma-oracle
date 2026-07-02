/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_json.h - minimal, strict JSON handling for exactly the Anthropic
 * Messages API shapes this gateway sends and receives. Not a general
 * JSON library by design: anything outside the expected shape maps to
 * INVALID_OUTPUT truthfully rather than being guessed at.
 */
#ifndef GW_JSON_H
#define GW_JSON_H

#include <stddef.h>

/* Escape src into dst as JSON string content (no surrounding quotes).
 * Returns bytes written, or -1 if dst too small. Input must be valid
 * UTF-8 (the caller validates before this point). */
int gwj_escape(char *dst, size_t cap, const char *src, size_t len);

/* Extract the concatenated text of every content block with
 * "type":"text" from a Messages API response body. Full unescape
 * including \uXXXX and surrogate pairs. Returns 0 on success, -1 on
 * any structural surprise. */
int gwj_content_text(const char *body, size_t body_len,
                     char *dst, size_t cap, size_t *out_len);

/* Extract a top-level string field value (e.g. "stop_reason").
 * Returns 0 on success, -1 if absent or not a string. */
int gwj_top_string(const char *body, size_t body_len, const char *key,
                   char *dst, size_t cap);

/* Extract error.message from an error body. Returns 0 or -1. */
int gwj_error_message(const char *body, size_t body_len,
                      char *dst, size_t cap);

#endif /* GW_JSON_H */

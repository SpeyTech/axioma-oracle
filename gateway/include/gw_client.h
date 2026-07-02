/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_client.h - the Anthropic Messages API client.
 *
 * The only non-deterministic element in the gateway. Retries per the
 * client contract: bounded exponential backoff with jitter on 429 and
 * 5xx, honouring retry-after; no retry on other 4xx; transport errors
 * and timeouts are single-attempt outcomes. Hard response-size cap
 * enforced before parse. The upstream-verbatim error string is
 * captured for the log; the API key never appears in it.
 */
#ifndef GW_CLIENT_H
#define GW_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "gw_config.h"

typedef enum {
    GWC_OK = 0,           /* HTTP 200, body captured */
    GWC_HTTP_FAIL,        /* non-200 after policy (incl. retries exhausted) */
    GWC_TIMEOUT,          /* connect or response timeout */
    GWC_TRANSPORT,        /* curl-level failure (DNS, TLS, conn reset) */
    GWC_OVERCAP           /* response exceeded max_response_bytes */
} gwc_outcome_t;

typedef struct {
    gwc_outcome_t outcome;
    long          http_status;    /* last status, 0 if none */
    int           attempts;       /* attempts actually made */
    char         *body;           /* heap, caller frees; NUL-terminated */
    size_t        body_len;
    char          errbuf[512];    /* upstream-verbatim, redacted */
} gwc_result_t;

/* One oracle call. request_json is the full Messages API body.
 * Returns 0 on procedural success (result populated, whatever the
 * outcome), -1 only on allocation failure. */
int gwc_call(const gw_config_t *cfg, const char *api_key,
             const char *request_json, size_t request_len,
             gwc_result_t *res);

void gwc_result_free(gwc_result_t *res);

/* Global curl init/cleanup, once per process. */
int  gwc_global_init(void);
void gwc_global_cleanup(void);

#endif /* GW_CLIENT_H */

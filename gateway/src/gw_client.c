/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_client.c - see gw_client.h. Heap-permitted per the allocation
 * posture: the library boundary stays no-heap, the HTTPS client does
 * not pretend.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_client.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;       /* hard cap from config */
    int    overcap;
} body_t;

static size_t on_body(char *ptr, size_t size, size_t nmemb, void *ud)
{
    body_t *b = (body_t *)ud;
    size_t n = size * nmemb;
    if (b->len + n > b->cap) { b->overcap = 1; return 0; }  /* abort transfer */
    {
        char *nb = realloc(b->buf, b->len + n + 1);
        if (nb == NULL) return 0;
        b->buf = nb;
        memcpy(b->buf + b->len, ptr, n);
        b->len += n;
        b->buf[b->len] = '\0';
    }
    return n;
}

typedef struct { long retry_after_s; } hdrs_t;

static size_t on_header(char *ptr, size_t size, size_t nmemb, void *ud)
{
    hdrs_t *h = (hdrs_t *)ud;
    size_t n = size * nmemb;
    if (n > 12 && strncasecmp(ptr, "retry-after:", 12) == 0) {
        long v = strtol(ptr + 12, NULL, 10);
        if (v > 0 && v <= 3600) h->retry_after_s = v;
    }
    return n;
}

static void backoff_sleep(int attempt, long retry_after_s)
{
    /* base 1s doubling, +/- jitter up to 500ms, retry-after wins, cap 60s */
    long ms;
    if (retry_after_s > 0) {
        ms = retry_after_s * 1000L;
    } else {
        ms = 1000L << (attempt - 1);
        ms += (long)(rand() % 500);
    }
    if (ms > 60000L) ms = 60000L;
    {
        struct timespec ts;
        ts.tv_sec  = ms / 1000L;
        ts.tv_nsec = (ms % 1000L) * 1000000L;
        nanosleep(&ts, NULL);
    }
}

int gwc_global_init(void)
{
    srand((unsigned int)(time(NULL) ^ (unsigned int)getpid()));
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void gwc_global_cleanup(void) { curl_global_cleanup(); }

int gwc_call(const gw_config_t *cfg, const char *api_key,
             const char *request_json, size_t request_len,
             gwc_result_t *res)
{
    int attempt;

    memset(res, 0, sizeof *res);

    for (attempt = 1; attempt <= (int)cfg->max_attempts; attempt++) {
        CURL *h;
        struct curl_slist *headers = NULL;
        char curl_err[CURL_ERROR_SIZE] = {0};
        char keyhdr[512];
        char verhdr[128];
        body_t body = {0};
        hdrs_t hd = {0};
        CURLcode rc;
        long status = 0;

        body.cap = (size_t)cfg->max_response_bytes;
        res->attempts = attempt;

        h = curl_easy_init();
        if (h == NULL) return -1;

        (void)snprintf(keyhdr, sizeof keyhdr, "x-api-key: %s", api_key);
        (void)snprintf(verhdr, sizeof verhdr, "anthropic-version: %s",
                       cfg->anthropic_version);
        headers = curl_slist_append(headers, "content-type: application/json");
        headers = curl_slist_append(headers, keyhdr);
        headers = curl_slist_append(headers, verhdr);
        /* Scrub the key from the stack copy as soon as curl owns it. */
        memset(keyhdr, 0, sizeof keyhdr);
        if (headers == NULL) { curl_easy_cleanup(h); return -1; }

        curl_easy_setopt(h, CURLOPT_URL, cfg->api_url);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, request_json);
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)request_len);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(h, CURLOPT_HEADERDATA, &hd);
        curl_easy_setopt(h, CURLOPT_ERRORBUFFER, curl_err);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, cfg->connect_timeout_s);
        curl_easy_setopt(h, CURLOPT_TIMEOUT, cfg->response_timeout_s);
        /* TLS verification on: curl defaults, never toggled. */
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);

        rc = curl_easy_perform(h);
        (void)curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(h);

        res->http_status = status;

        if (rc == CURLE_OPERATION_TIMEDOUT) {
            res->outcome = GWC_TIMEOUT;
            (void)snprintf(res->errbuf, sizeof res->errbuf, "%s",
                           curl_err[0] ? curl_err : curl_easy_strerror(rc));
            free(body.buf);
            return 0;
        }
        if (rc == CURLE_WRITE_ERROR && body.overcap) {
            res->outcome = GWC_OVERCAP;
            (void)snprintf(res->errbuf, sizeof res->errbuf,
                           "response exceeded %ld bytes", cfg->max_response_bytes);
            free(body.buf);
            return 0;
        }
        if (rc != CURLE_OK) {
            res->outcome = GWC_TRANSPORT;
            (void)snprintf(res->errbuf, sizeof res->errbuf, "%s",
                           curl_err[0] ? curl_err : curl_easy_strerror(rc));
            free(body.buf);
            return 0;
        }

        if (status == 200) {
            res->outcome = GWC_OK;
            res->body = body.buf;
            res->body_len = body.len;
            return 0;
        }

        /* Non-200: keep the body as the verbatim upstream error source. */
        if ((status == 429 || (status >= 500 && status < 600)) &&
            attempt < (int)cfg->max_attempts) {
            (void)snprintf(res->errbuf, sizeof res->errbuf,
                           "HTTP %ld%s%.*s", status,
                           body.len ? ": " : "",
                           (int)(body.len > 400 ? 400 : body.len),
                           body.buf ? body.buf : "");
            free(body.buf);
            backoff_sleep(attempt, hd.retry_after_s);
            continue;
        }

        res->outcome = GWC_HTTP_FAIL;
        (void)snprintf(res->errbuf, sizeof res->errbuf,
                       "HTTP %ld%s%.*s", status,
                       body.len ? ": " : "",
                       (int)(body.len > 400 ? 400 : body.len),
                       body.buf ? body.buf : "");
        res->body = body.buf;      /* keep for error-message extraction */
        res->body_len = body.len;
        return 0;
    }

    /* Retries exhausted on 429/5xx. */
    res->outcome = GWC_HTTP_FAIL;
    return 0;
}

void gwc_result_free(gwc_result_t *res)
{
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

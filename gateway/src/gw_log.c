/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_log.c - see gw_log.h.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *g_log = NULL;
static char  g_secret[256];
static size_t g_secret_len = 0;

int gw_log_open(const char *path, const char *secret)
{
    g_log = fopen(path, "ab");
    if (g_log == NULL) return -1;
    g_secret_len = 0;
    if (secret != NULL) {
        size_t n = strlen(secret);
        if (n > 0 && n < sizeof g_secret) {
            memcpy(g_secret, secret, n);
            g_secret_len = n;
        }
    }
    return 0;
}

void gw_log_close(void)
{
    if (g_log != NULL) { fclose(g_log); g_log = NULL; }
    memset(g_secret, 0, sizeof g_secret);
    g_secret_len = 0;
}

/* JSON-escape src into the log stream, scrubbing the secret. */
static void put_escaped(const char *src)
{
    size_t i = 0, n = strlen(src);
    while (i < n) {
        if (g_secret_len > 0 && n - i >= g_secret_len &&
            memcmp(src + i, g_secret, g_secret_len) == 0) {
            fputs("[redacted]", g_log);
            i += g_secret_len;
            continue;
        }
        {
            unsigned char c = (unsigned char)src[i];
            if      (c == '"')  fputs("\\\"", g_log);
            else if (c == '\\') fputs("\\\\", g_log);
            else if (c == '\n') fputs("\\n",  g_log);
            else if (c == '\r') fputs("\\r",  g_log);
            else if (c == '\t') fputs("\\t",  g_log);
            else if (c < 0x20)  fprintf(g_log, "\\u%04x", (unsigned int)c);
            else                fputc((int)c, g_log);
        }
        i++;
    }
}

void gw_log(const char *level, const char *event, const char *message,
            int attempt, long status, uint64_t seq)
{
    time_t t;
    struct tm tmv;
    char ts[32];

    if (g_log == NULL) return;
    t = time(NULL);
    gmtime_r(&t, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tmv);

    fprintf(g_log, "{\"ts\":\"%s\",\"level\":\"%s\",\"event\":\"%s\"", ts, level, event);
    if (message != NULL) {
        fputs(",\"message\":\"", g_log);
        put_escaped(message);
        fputc('"', g_log);
    }
    if (attempt >= 0) fprintf(g_log, ",\"attempt\":%d", attempt);
    if (status  >= 0) fprintf(g_log, ",\"status\":%ld", status);
    if (seq > 0)      fprintf(g_log, ",\"seq\":%llu", (unsigned long long)seq);
    fputs("}\n", g_log);
    fflush(g_log);
    (void)fsync(fileno(g_log));
}

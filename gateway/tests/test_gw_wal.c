/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_wal.c - the intent must round-trip, refuse a second open
 * intent, and fail closed on corruption.
 */
#include "gw_wal.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-44s PASS\n", name); \
    else { printf("  %-44s FAIL\n", name); failures++; } \
} while (0)

#define WPATH "/tmp/gw_test_wal.open"

int main(void)
{
    gw_intent_t a, b;

    printf("gateway: test_gw_wal\n");
    (void)unlink(WPATH);

    CHECK(gw_wal_recover(WPATH, &b) == 0, "no intent when absent");

    memset(&a, 0, sizeof a);
    memcpy(a.input_hash_hex,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 65);
    snprintf(a.model_id, sizeof a.model_id, "claude-test-model");
    a.max_tokens = 256;
    a.seed = -1;
    a.temperature_q16 = 0;
    a.top_p_q16 = INT32_MIN;

    CHECK(gw_wal_write(WPATH, &a) == 0, "intent written");
    CHECK(gw_wal_write(WPATH, &a) != 0, "second open intent refused");

    CHECK(gw_wal_recover(WPATH, &b) == 1, "intent recovered");
    CHECK(strcmp(a.input_hash_hex, b.input_hash_hex) == 0 &&
          strcmp(a.model_id, b.model_id) == 0 &&
          a.max_tokens == b.max_tokens &&
          a.temperature_q16 == b.temperature_q16 &&
          a.top_p_q16 == b.top_p_q16, "fields round trip");

    CHECK(gw_wal_clear(WPATH) == 0, "intent cleared");
    CHECK(gw_wal_recover(WPATH, &b) == 0, "clear leaves no intent");

    /* corrupt intent: truncated hash */
    {
        FILE *f = fopen(WPATH, "wb");
        if (f) { fputs("input_hash=abc\nmodel_id=m\n", f); fclose(f); }
        CHECK(gw_wal_recover(WPATH, &b) == -1, "corrupt intent fails closed");
        (void)unlink(WPATH);
    }

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

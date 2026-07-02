/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_ledger.c - the durable chain must replay bit-identically,
 * refuse tampered commits, and apply the torn-tail policy correctly.
 * TU group B rules apply: no oracle headers here.
 */
#include "gw_ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-44s PASS\n", name); \
    else { printf("  %-44s FAIL\n", name); failures++; } \
} while (0)

#define LPATH "/tmp/gw_test_ledger.bin"

int main(void)
{
    uint8_t head1[32], head2[32], head3[32];
    uint64_t seq = 0;

    printf("gateway: test_gw_ledger\n");
    (void)unlink(LPATH);

    /* fresh open, two appends */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "open fresh");
    CHECK(gwl_append("AX:OBS:v1", (const uint8_t *)"{\"a\":1}", 7,
                     head1, &seq) == 0 && seq == 1, "append 1");
    CHECK(gwl_append("AX:STATE:v1", (const uint8_t *)"{\"b\":2}", 7,
                     head2, &seq) == 0 && seq == 2, "append 2");
    CHECK(memcmp(head1, head2, 32) != 0, "heads advance");
    CHECK(gwl_last_payload_contains("\"b\":2") == 1 &&
          gwl_last_payload_contains("zebra") == 0, "last payload probe");
    gwl_close();

    /* replay: identical head, sequence 2 */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "replay open");
    gwl_head(head3);
    CHECK(memcmp(head2, head3, 32) == 0 && gwl_seq() == 2,
          "replay head bit-identical");
    CHECK(gwl_last_payload_contains("\"b\":2") == 1,
          "last payload restored by replay");
    gwl_close();

    /* unregistered tag refused */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "open for tag test");
    CHECK(gwl_append("AX:BOGUS:v1", (const uint8_t *)"x", 1,
                     head1, &seq) != 0, "unregistered tag refused");
    gwl_close();

    /* tamper: flip one payload byte on disk; replay must refuse */
    {
        FILE *f = fopen(LPATH, "r+b");
        CHECK(f != NULL, "open raw for tamper");
        if (f) {
            /* frame: 4 + 9(tag) + 8 + payload...; flip first payload byte */
            fseek(f, 4 + 9 + 8, SEEK_SET);
            fputc('X', f);
            fclose(f);
        }
        CHECK(gwl_open(LPATH, 0, NULL) != 0, "tampered payload refused");
        /* restore */
        f = fopen(LPATH, "r+b");
        if (f) { fseek(f, 4 + 9 + 8, SEEK_SET); fputc('{', f); fclose(f); }
        CHECK(gwl_open(LPATH, 0, NULL) == 0, "restored payload replays");
        gwl_close();
    }

    /* torn tail: append garbage half-frame; policy split */
    {
        FILE *f = fopen(LPATH, "ab");
        int truncated = 0;
        static const unsigned char torn[9] =
            { 0x09, 0x00, 0x00, 0x00, 'A', 'X', ':', 'O', 'B' };
        CHECK(f != NULL, "open for torn tail");
        if (f) { fwrite(torn, 1, sizeof torn, f); fclose(f); }
        CHECK(gwl_open(LPATH, 0, NULL) != 0, "torn tail refused without WAL");
        CHECK(gwl_open(LPATH, 1, &truncated) == 0 && truncated == 1,
              "torn tail truncated under WAL");
        gwl_head(head3);
        CHECK(memcmp(head2, head3, 32) == 0 && gwl_seq() == 2,
              "head intact after truncation");
        gwl_close();
    }

    (void)unlink(LPATH);
    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_export.c - the serving-determinism witness export must be a
 * byte-identical prefix of the primary at every observable moment, must
 * heal from any prior state at enable, and must fail open for serving
 * when its own writes fail. TU group B rules apply: no oracle headers.
 */
#define _POSIX_C_SOURCE 200809L   /* pwrite, ftruncate (W-D1a) */
#include "gw_ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-52s PASS\n", name); \
    else { printf("  %-52s FAIL\n", name); failures++; } \
} while (0)

#define LPATH "/tmp/gw_test_export_ledger.bin"
#define EPATH "/tmp/gw_test_export_mirror.bin"

/* read a whole file; returns length or -1 */
static long slurp(const char *path, unsigned char *buf, long cap)
{
    long n; FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    n = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

static int files_identical(const char *a, const char *b)
{
    static unsigned char ba[1 << 20], bb[1 << 20];
    long na = slurp(a, ba, sizeof ba);
    long nb = slurp(b, bb, sizeof bb);
    if (na < 0 || nb < 0 || na != nb) return 0;
    return memcmp(ba, bb, (size_t)na) == 0;
}

int main(void)
{
    uint8_t head[32];
    uint64_t seq = 0;

    printf("gateway: test_gw_export\n");
    (void)unlink(LPATH);
    (void)unlink(EPATH);

    /* 1. enable on a fresh primary, then append: mirror is identical */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "open fresh primary");
    CHECK(gwl_export_enable(EPATH) == 0, "export enable on empty chain");
    CHECK(gwl_export_active() == 1, "export active");
    CHECK(gwl_append("AX:OBS:v1", (const uint8_t *)"{\"a\":1}", 7,
                     head, &seq) == 0, "append 1 mirrored");
    CHECK(gwl_append("AX:STATE:v1", (const uint8_t *)"{\"b\":2}", 7,
                     head, &seq) == 0, "append 2 mirrored");
    CHECK(files_identical(LPATH, EPATH), "export byte-identical after appends");
    gwl_close();
    CHECK(gwl_export_active() == 0, "close deactivates export");

    /* 2. pre-existing frames then enable: baseline copy is identical */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "reopen primary (2 frames)");
    (void)unlink(EPATH);
    CHECK(gwl_export_enable(EPATH) == 0, "export enable over history");
    CHECK(files_identical(LPATH, EPATH), "baseline copy byte-identical");
    CHECK(gwl_append("AX:OBS:v1", (const uint8_t *)"{\"c\":3}", 7,
                     head, &seq) == 0 && seq == 3, "append 3 mirrored");
    CHECK(files_identical(LPATH, EPATH), "still identical after append 3");
    gwl_close();

    /* 3. heal: corrupt and truncate the export, enable rewrites it */
    {
        int fd = open(EPATH, O_RDWR);
        CHECK(fd >= 0 && pwrite(fd, "XX", 2, 5) == 2, "corrupt export");
        CHECK(ftruncate(fd, 17) == 0, "truncate export mid-frame");
        close(fd);
    }
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "reopen primary (3 frames)");
    CHECK(gwl_export_enable(EPATH) == 0, "export enable heals");
    CHECK(files_identical(LPATH, EPATH), "healed export byte-identical");
    gwl_close();

    /* 4. fail-open: kill the export fd out from under the mirror by
     * enabling onto a path we then make unwritable is racy across
     * filesystems; instead prove the contract surface: after close,
     * appends without an active export still succeed and the primary
     * advances (export loss must never refuse serving). */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "reopen, export disabled");
    CHECK(gwl_export_active() == 0, "export inactive by default");
    CHECK(gwl_append("AX:OBS:v1", (const uint8_t *)"{\"d\":4}", 7,
                     head, &seq) == 0 && seq == 4,
          "append succeeds with no export");
    CHECK(files_identical(LPATH, EPATH) == 0,
          "stale export now differs (witness-detectable)");
    gwl_close();

    /* 5. enable failure on an impossible path refuses cleanly */
    CHECK(gwl_open(LPATH, 0, NULL) == 0, "reopen for bad path");
    CHECK(gwl_export_enable("/nonexistent-dir/export.bin") == -1,
          "enable on impossible path returns -1");
    CHECK(gwl_export_active() == 0, "not active after failed enable");
    CHECK(gwl_append("AX:OBS:v1", (const uint8_t *)"{\"e\":5}", 7,
                     head, &seq) == 0 && seq == 5,
          "serving unaffected by failed enable");
    gwl_close();

    (void)unlink(LPATH);
    (void)unlink(EPATH);

    if (failures) { printf("FAILURES: %d\n", failures); return 1; }
    printf("all export tests pass\n");
    return 0;
}

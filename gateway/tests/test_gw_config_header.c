/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_config_header.c - anthropic_version is copied into the
 * "anthropic-version:" HTTP request header line (gw_client.c), so a value
 * holding a control character must be refused at configuration load.
 *
 * The line parser splits on LF and strips only trailing CR/LF, so a CR in
 * the middle of the value survives into the header and splits it: with
 *   anthropic_version=2023-06-01<CR>X-Injected: 1
 * the request carries a second, injected header. RFC 9110 section 5.5:
 * a field value never contains CR, LF or NUL, and its only permitted
 * control character is HTAB between visible characters. A version token
 * has no use for HTAB, so the gateway refuses every C0 control
 * (0x00-0x1F) and DEL (0x7F), the same set the AX:OBS:v1 identifier rule
 * refuses.
 *
 * Values that load today are pinned unchanged: the default, an explicit
 * version, an internal space, and a CRLF-terminated configuration file.
 */
#define _POSIX_C_SOURCE 200809L
#include "gw_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-58s PASS\n", name); \
    else { printf("  %-58s FAIL\n", name); failures++; } \
} while (0)

static const char BASE[] =
    "socket_path=/tmp/gw_hdr_test.sock\n"
    "ledger_path=/tmp/gw_hdr_test.bin\n"
    "wal_path=/tmp/gw_hdr_test.wal\n"
    "log_path=/tmp/gw_hdr_test.log\n"
    "model_id=test-model\n";

static gw_config_t cfg;

/* Write BASE plus extra (len bytes, may hold any byte) and load it. */
static int load_with(const char *extra, size_t len)
{
    char path[] = "/tmp/gw_hdr_test_XXXXXX";
    int fd = mkstemp(path);
    FILE *f;
    int rc;

    if (fd < 0) { perror("mkstemp"); exit(2); }
    f = fdopen(fd, "wb");
    if (f == NULL) { perror("fdopen"); exit(2); }
    fwrite(BASE, 1, sizeof BASE - 1, f);
    fwrite(extra, 1, len, f);
    fclose(f);
    rc = gw_config_load(&cfg, path);
    (void)unlink(path);
    return rc;
}

#define LOAD(lit) load_with((lit), sizeof(lit) - 1)

static int refused(const char *lit, size_t len)
{
    return load_with(lit, len) != 0;
}
#define REFUSED(lit) refused((lit), sizeof(lit) - 1)

int main(void)
{
    printf("gateway: test_gw_config_header\n");

    /* Unchanged: what loads today still loads, with the same value. */
    CHECK(LOAD("") == 0 && strcmp(cfg.anthropic_version, "2023-06-01") == 0,
          "default version loads");
    CHECK(LOAD("anthropic_version=2023-06-01\n") == 0 &&
          strcmp(cfg.anthropic_version, "2023-06-01") == 0,
          "explicit version loads");
    CHECK(LOAD("anthropic_version=2023-06-01 beta\n") == 0 &&
          strcmp(cfg.anthropic_version, "2023-06-01 beta") == 0,
          "internal space loads (permitted in a field value)");
    CHECK(LOAD("anthropic_version=2023-06-01\r\n") == 0 &&
          strcmp(cfg.anthropic_version, "2023-06-01") == 0,
          "CRLF-terminated line loads, trailing CR stripped");

    /* Header injection: CR inside the value. */
    CHECK(REFUSED("anthropic_version=2023-06-01\rX-Injected: 1\n"),
          "CR inside value refused (header injection)");
    CHECK(REFUSED("anthropic_version=2023-06-01\r\rX-Injected: 1\r\n"),
          "CR CR inside CRLF-terminated line refused");
    CHECK(REFUSED("anthropic_version=\r2023-06-01\n"),
          "leading CR refused");

    /* LF cannot sit inside a value: the parser ends the line there and
     * the remainder must parse as its own key=value line. Pinned. */
    CHECK(REFUSED("anthropic_version=2023-06-01\nX-Injected: 1\n"),
          "LF inside value: remainder refused as a line");

    /* Other control characters. */
    CHECK(REFUSED("anthropic_version=2023-06-01\tx\n"), "HTAB refused");
    CHECK(REFUSED("anthropic_version=2023\x01" "06-01\n"), "0x01 refused");
    CHECK(REFUSED("anthropic_version=2023-06-01\x0b\n"), "VT 0x0B refused");
    CHECK(REFUSED("anthropic_version=2023-06-01\x0c\n"), "FF 0x0C refused");
    CHECK(REFUSED("anthropic_version=2023-06-01\x1b[0m\n"), "ESC 0x1B refused");
    CHECK(REFUSED("anthropic_version=2023-06-01\x1f\n"), "0x1F refused");
    CHECK(REFUSED("anthropic_version=2023-06-01\x7f\n"), "DEL 0x7F refused");

    /* NUL: the value can never carry bytes past a NUL into the header.
     * Either the file is refused or the value ends at the NUL. */
    {
        static const char nul[] = "anthropic_version=2023-06-01\0X-Injected: 1\n";
        int rc = load_with(nul, sizeof nul - 1);
        CHECK(rc != 0 || strcmp(cfg.anthropic_version, "2023-06-01") == 0,
              "NUL: nothing after it reaches the value");
    }

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

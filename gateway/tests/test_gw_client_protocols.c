/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_client_protocols.c - a transfer carries credentials and the
 * prompt, so gwc_call must use HTTP or HTTPS only, whatever api_url says.
 *
 * Calls gwc_call directly with a configuration built in memory (bypassing
 * the startup check), so the transfer-level restriction is tested on its
 * own. A local file holds a marker string; with api_url file://<that
 * file>, an unrestricted transfer reads the file and its bytes come back
 * in the result (the error text quotes up to 400 bytes of the "response").
 * The call must instead fail as a transport error that names no file
 * content. Further non-HTTP schemes must fail the same way without any
 * transfer.
 */
#define _POSIX_C_SOURCE 200809L
#include "gw_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-60s PASS\n", name); \
    else { printf("  %-60s FAIL\n", name); failures++; } \
} while (0)

#define MARKER "LOCALFILEMARKER-5c1e"

static gw_config_t cfg;

static void base_cfg(const char *url)
{
    memset(&cfg, 0, sizeof cfg);
    snprintf(cfg.api_url, sizeof cfg.api_url, "%s", url);
    snprintf(cfg.anthropic_version, sizeof cfg.anthropic_version, "2023-06-01");
    cfg.connect_timeout_s = 2;
    cfg.response_timeout_s = 3;
    cfg.max_attempts = 1;
    cfg.max_response_bytes = 65536;
}

static int leaked(const gwc_result_t *r)
{
    return strstr(r->errbuf, MARKER) != NULL ||
           (r->body != NULL && strstr(r->body, MARKER) != NULL);
}

int main(void)
{
    char path[] = "/tmp/gw_proto_test_XXXXXX";
    char url[600];
    int fd;
    gwc_result_t r;
    static const char *other[] = {
        "dict://127.0.0.1:9/x", "gopher://127.0.0.1:9/x",
        "ftp://127.0.0.1:9/x", "tftp://127.0.0.1:9/x",
        "telnet://127.0.0.1:9", "ldap://127.0.0.1:9/", "smb://127.0.0.1:9/x",
    };
    size_t i;

    printf("gateway: test_gw_client_protocols\n");
    if (gwc_global_init() != 0) { printf("curl init failed\n"); return 2; }

    fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 2; }
    if (write(fd, MARKER "\n", sizeof MARKER) != (ssize_t)sizeof MARKER) return 2;
    close(fd);

    snprintf(url, sizeof url, "file://%s", path);
    base_cfg(url);
    CHECK(gwc_call(&cfg, "test-key-not-real", "{}", 2, &r) == 0, "file:// call returns");
    CHECK(r.outcome == GWC_TRANSPORT, "file:// is a transport error");
    CHECK(!leaked(&r), "file:// content does not reach the result");
    gwc_result_free(&r);

    for (i = 0; i < sizeof other / sizeof *other; i++) {
        char name[96];
        base_cfg(other[i]);
        (void)gwc_call(&cfg, "test-key-not-real", "{}", 2, &r);
        snprintf(name, sizeof name, "%.40s refused", other[i]);
        CHECK(r.outcome == GWC_TRANSPORT &&
              (strstr(r.errbuf, "rotocol") != NULL ||
               strstr(r.errbuf, "disabled") != NULL ||
               strstr(r.errbuf, "not supported") != NULL), name);
        gwc_result_free(&r);
    }

    (void)unlink(path);
    gwc_global_cleanup();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

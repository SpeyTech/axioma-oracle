/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_config.c - PCR-004 residual (c): model_id and oracle_id are
 * checked at configuration load with the admission identifier rules.
 *
 * Before this check a configured identifier with a control character
 * (for example a tab), DEL or invalid UTF-8 loaded, and every request then
 * failed admission with AX_ERR_SCHEMA. Now the configuration is refused
 * at load with a message naming the key. Each identifier value is loaded
 * exactly when ax_obs_admit accepts it (the gateway buffer additionally
 * caps identifiers at 255 bytes, as before), and valid identifiers
 * (ASCII, punctuation, non-ASCII UTF-8, 255 bytes) still load. Leading
 * blanks and tabs after '=' are trimmed as before.
 *
 * Fails on 44deb6d (the malformed identifiers load there); passes after.
 */
#define _POSIX_C_SOURCE 200809L   /* getpid */
#include "gw_config.h"
#include "axilog/obs.h"
#include "axilog/limits.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
static int runs = 0;
static char path[128];
static gw_config_t cfg;   /* large: keep off the stack */
static char norm[64];
static char buf[1024];

#define CHECK(cond, name) do { \
    runs++; \
    if (cond) printf("  %-66s PASS\n", name); \
    else { printf("  %-66s FAIL\n", name); failures++; } \
} while (0)

/* Write a minimal valid config ending with the given bytes. */
static int load_with(const char *tail, size_t tail_len)
{
    static const char head[] =
        "socket_path=/tmp/gwcfg.sock\n"
        "ledger_path=/tmp/gwcfg.bin\n"
        "wal_path=/tmp/gwcfg.wal\n"
        "log_path=/tmp/gwcfg.log\n";
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -99;
    fwrite(head, 1, sizeof head - 1, f);
    fwrite(tail, 1, tail_len, f);
    fclose(f);
    return gw_config_load(&cfg, path);
}

/* Would admission accept a record with this model_id / oracle_id? */
static int admits(const char *model_id, const char *oracle_id)
{
    ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
    ax_l3_fault_flags_t f;
    memset(&in, 0, sizeof in);
    in.completion_state = AX_COMPLETION_ERROR;
    in.failure_type = AX_FAILURE_TRANSPORT_ERROR;
    in.input = "p"; in.input_len = 1; in.ledger_seq = 1;
    in.model_id = model_id; in.oracle_id = oracle_id;
    ax_oracle_params_init_null(&in.params);
    ax_admission_ctx_init(&ctx); ax_obs_init(&rec); memset(&f, 0, sizeof f);
    return ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f) == AX_OK;
}

/* One identifier value (NUL-free, as written on the config line). The
 * gateway must load it exactly when admission accepts it, and must not
 * alter it. which: 0 = model_id, 1 = oracle_id. */
static void one(int which, const char *val, const char *name)
{
    char label[160];
    size_t n;
    int loaded, adm;
    if (which == 0) {
        n = (size_t)snprintf(buf, sizeof buf, "model_id=%s\n", val);
        adm = admits(val, "o");
    } else {
        n = (size_t)snprintf(buf, sizeof buf, "model_id=m\noracle_id=%s\n", val);
        adm = admits("m", val);
    }
    loaded = load_with(buf, n) == 0;
    if (loaded) {
        loaded = strcmp(which == 0 ? cfg.model_id : cfg.oracle_id, val) == 0 ? 1 : 2;
    }
    snprintf(label, sizeof label, "%s %s: %s", which == 0 ? "model_id" : "oracle_id",
             name, adm ? "loads (admissible)" : "refused (not admissible)");
    CHECK(loaded != 2 && loaded == adm, label);
}

int main(void)
{
    static const struct { const char *v; const char *name; } cases[] = {
        { "test-model",                        "plain ASCII" },
        { "model-x.y_z:1/2",                   "punctuation" },
        { "a b\"c\\d",                         "space, quote, backslash" },
        { "mod\xc3\xa9le-\xe2\x82\xac-\xf0\x9f\x98\x80", "non-ASCII UTF-8" },
        { "e\xcc\x81",                         "non-NFC UTF-8" },
        { "m\tx",                              "TAB inside" },
        { "m\t",                               "trailing TAB" },
        { "m\x01",                             "C0 0x01" },
        { "m\x1f",                             "C0 0x1F" },
        { "m\x1b[0m",                          "ESC sequence" },
        { "m\x7f",                             "DEL" },
        { "m\xff",                             "invalid UTF-8 0xFF" },
        { "m\xc0\xaf",                         "overlong UTF-8" },
        { "m\xed\xa0\x80",                     "UTF-16 surrogate" },
        { "m\xe2\x82",                         "truncated UTF-8" },
        { "m\xf4\x90\x80\x80",                 "code point above U+10FFFF" }
    };
    static char longid[300];
    size_t i;

    snprintf(path, sizeof path, "/tmp/gw_test_config.%ld.conf", (long)getpid());
    printf("gateway: test_gw_config (PCR-004 residual c)\n");

    printf("-- configured identifier loads exactly when admission accepts it\n");
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        one(0, cases[i].v, cases[i].name);
        one(1, cases[i].v, cases[i].name);
    }
    memset(longid, 'm', 255); longid[255] = '\0';
    one(0, longid, "255 bytes");
    one(1, longid, "255 bytes");

    printf("-- further load-time refusals\n");
    CHECK(load_with("model_id=\n", 10) != 0, "empty model_id refused");
    CHECK(load_with("model_id=m\noracle_id=\n", 22) != 0, "empty oracle_id refused");
    CHECK(load_with("model_id=m\x00x\n", 13) != 0, "NUL inside model_id refused");
    memset(longid, 'm', 256); longid[256] = '\0';
    CHECK(admits(longid, "o") && load_with(buf, (size_t)snprintf(buf, sizeof buf,
          "model_id=%s\n", longid)) != 0,
          "256-byte model_id refused (gateway buffer is 255; admission 256)");
    memset(longid, 'm', 257); longid[257] = '\0';
    CHECK(load_with(buf, (size_t)snprintf(buf, sizeof buf, "model_id=%s\n", longid)) != 0,
          "257-byte model_id refused (admission bound)");

    printf("-- unchanged parsing\n");
    CHECK(load_with("model_id=m\r\n", 12) == 0 && strcmp(cfg.model_id, "m") == 0,
          "CRLF line ending stripped, not part of model_id");
    CHECK(load_with("model_id= \tm\n", 13) == 0 && strcmp(cfg.model_id, "m") == 0,
          "leading blanks and tabs trimmed as before");
    CHECK(load_with("model_id=m\n", 11) == 0 && admits(cfg.model_id, cfg.oracle_id),
          "default oracle_id is admissible");

    (void)unlink(path);
    printf("%d/%d passed, %d failing: %s\n", runs - failures, runs, failures,
           failures ? "FAILURES" : "ALL PASS");
    return failures == 0 ? 0 : 1;
}

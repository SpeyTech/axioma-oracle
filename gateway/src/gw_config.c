/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_config.c - see gw_config.h.
 */
#include "gw_config.h"
#include "axilog/obs.h"
#include "axilog/limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int set_str(char *dst, size_t cap, const char *v)
{
    size_t n = strlen(v);
    if (n == 0 || n >= cap) return -1;
    memcpy(dst, v, n + 1);
    return 0;
}

/* PCR-004 residual (c): model_id and oracle_id go into every AX:OBS:v1
 * record, and admission rejects a malformed identifier with AX_ERR_SCHEMA.
 * Check them here with the admission validator itself, so a configuration
 * the gateway could never admit a record under is refused at load, not at
 * the first request. Returns 0 when admissible, else -1 with a message. */
static int set_id(char *dst, size_t cap, size_t ax_max, const char *key,
                  const char *v, int lineno)
{
    int rc = ax_obs_identifier_check(v, ax_max);
    if (rc == AX_ERR_SIZE) {
        fprintf(stderr, "config:%d: %s is longer than %zu bytes\n",
                lineno, key, ax_max);
        return -1;
    }
    if (rc != AX_OK) {
        fprintf(stderr,
            "config:%d: %s is not an admissible identifier: it must be"
            " non-empty valid UTF-8 with no control character"
            " (0x00-0x1F) or DEL (0x7F)\n", lineno, key);
        return -1;
    }
    if (strlen(v) >= cap) {
        fprintf(stderr, "config:%d: %s is longer than %zu bytes\n",
                lineno, key, cap - 1);
        return -1;
    }
    return set_str(dst, cap, v);
}

static int set_long(long *dst, const char *v, long lo, long hi)
{
    char *end = NULL;
    long x = strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > hi) return -1;
    *dst = x;
    return 0;
}

int gw_config_load(gw_config_t *cfg, const char *path)
{
    FILE *f;
    char line[1024];
    int lineno = 0;

    memset(cfg, 0, sizeof *cfg);
    /* Defaults per brief. */
    (void)set_str(cfg->api_url, sizeof cfg->api_url,
                  "https://api.anthropic.com/v1/messages");
    (void)set_str(cfg->anthropic_version, sizeof cfg->anthropic_version,
                  "2023-06-01");
    (void)set_str(cfg->oracle_id, sizeof cfg->oracle_id,
                  "anthropic-messages-api");
    cfg->connect_timeout_s  = 10;
    cfg->response_timeout_s = 300;
    cfg->max_attempts       = 3;
    cfg->max_response_bytes = 4L * 1024L * 1024L;
    cfg->call_budget        = 25;
    cfg->default_max_tokens = 1024;
    cfg->max_prompt_bytes   = 32768;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "config: cannot open %s\n", path);
        return -1;
    }
    cfg->raw_len = fread(cfg->raw, 1, sizeof cfg->raw, f);
    if (fgetc(f) != EOF) {
        fprintf(stderr, "config: %s exceeds %d bytes\n", path, GW_CONFIG_MAX);
        fclose(f);
        return -1;
    }
    /* A NUL byte would end a value early in the line parser below and
     * silently shorten it (a model_id "m<NUL>x" would load as "m"). No
     * key takes binary data: refuse the file. */
    if (memchr(cfg->raw, '\0', cfg->raw_len) != NULL) {
        fprintf(stderr, "config: %s contains a NUL byte\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);

    while (fgets(line, sizeof line, f) != NULL) {
        char *eq, *key, *val, *e;
        lineno++;
        /* strip trailing newline and CR */
        e = line + strlen(line);
        while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
        /* skip blanks and comments */
        key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (*key == '\0' || *key == '#') continue;

        eq = strchr(key, '=');
        if (eq == NULL) {
            fprintf(stderr, "config:%d: missing '='\n", lineno);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        val = eq + 1;
        /* trim key tail */
        e = eq;
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        /* trim value head */
        while (*val == ' ' || *val == '\t') val++;

        {
            int rc = -1;
            if      (strcmp(key, "socket_path") == 0) rc = set_str(cfg->socket_path, GW_PATH_MAX, val);
            else if (strcmp(key, "ledger_path") == 0) rc = set_str(cfg->ledger_path, GW_PATH_MAX, val);
            else if (strcmp(key, "wal_path")    == 0) rc = set_str(cfg->wal_path,    GW_PATH_MAX, val);
            else if (strcmp(key, "log_path")    == 0) rc = set_str(cfg->log_path,    GW_PATH_MAX, val);
            else if (strcmp(key, "api_url")     == 0) rc = set_str(cfg->api_url,     GW_PATH_MAX, val);
            else if (strcmp(key, "anthropic_version") == 0) rc = set_str(cfg->anthropic_version, sizeof cfg->anthropic_version, val);
            else if (strcmp(key, "model_id")    == 0) {
                if (set_id(cfg->model_id, GW_ID_MAX, AX_MAX_MODEL_ID_BYTES, key, val, lineno) != 0) {
                    fclose(f);
                    return -1;
                }
                rc = 0;
            }
            else if (strcmp(key, "oracle_id")   == 0) {
                if (set_id(cfg->oracle_id, GW_ID_MAX, AX_MAX_ORACLE_ID_BYTES, key, val, lineno) != 0) {
                    fclose(f);
                    return -1;
                }
                rc = 0;
            }
            else if (strcmp(key, "fixture_capture_path") == 0) rc = set_str(cfg->fixture_capture_path, GW_PATH_MAX, val);
            else if (strcmp(key, "export_path")          == 0) rc = set_str(cfg->export_path, GW_PATH_MAX, val);
            else if (strcmp(key, "connect_timeout_s")  == 0) rc = set_long(&cfg->connect_timeout_s,  val, 1, 600);
            else if (strcmp(key, "response_timeout_s") == 0) rc = set_long(&cfg->response_timeout_s, val, 1, 3600);
            else if (strcmp(key, "max_attempts")       == 0) rc = set_long(&cfg->max_attempts,       val, 1, 10);
            else if (strcmp(key, "max_response_bytes") == 0) rc = set_long(&cfg->max_response_bytes, val, 1024, 64L*1024L*1024L);
            else if (strcmp(key, "call_budget")        == 0) rc = set_long(&cfg->call_budget,        val, 0, 1000000);
            else if (strcmp(key, "default_max_tokens") == 0) rc = set_long(&cfg->default_max_tokens, val, 1, 200000);
            else if (strcmp(key, "max_prompt_bytes")   == 0) rc = set_long(&cfg->max_prompt_bytes,   val, 1, 1L*1024L*1024L);
            else {
                fprintf(stderr, "config:%d: unknown key '%s'\n", lineno, key);
                fclose(f);
                return -1;
            }
            if (rc != 0) {
                fprintf(stderr, "config:%d: bad value for '%s'\n", lineno, key);
                fclose(f);
                return -1;
            }
        }
    }
    fclose(f);

    if (cfg->socket_path[0] == '\0' || cfg->ledger_path[0] == '\0' ||
        cfg->wal_path[0]    == '\0' || cfg->log_path[0]    == '\0' ||
        cfg->model_id[0]    == '\0') {
        fprintf(stderr,
            "config: socket_path, ledger_path, wal_path, log_path, model_id required\n");
        return -1;
    }
    return 0;
}

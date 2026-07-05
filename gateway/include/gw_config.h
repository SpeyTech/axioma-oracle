/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_config.h - gateway configuration.
 *
 * key=value file, '#' comments, unknown keys rejected (fail closed).
 * The raw config bytes are retained so main can commit their hash as
 * AX:STATE:v1 evidence at startup: the config binding is chain evidence,
 * not a log line.
 */
#ifndef GW_CONFIG_H
#define GW_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define GW_PATH_MAX      512
#define GW_ID_MAX        256
#define GW_CONFIG_MAX    8192

typedef struct {
    char socket_path[GW_PATH_MAX];
    char ledger_path[GW_PATH_MAX];
    char wal_path[GW_PATH_MAX];
    char log_path[GW_PATH_MAX];

    char api_url[GW_PATH_MAX];          /* default https://api.anthropic.com/v1/messages */
    char anthropic_version[64];         /* default 2023-06-01 */
    char model_id[GW_ID_MAX];
    char oracle_id[GW_ID_MAX];          /* default anthropic-messages-api */

    long connect_timeout_s;             /* default 10 */
    long response_timeout_s;            /* default 300 */
    long max_attempts;                  /* default 3; 429/5xx retries */
    long max_response_bytes;            /* default 4 MiB */
    long call_budget;                   /* default 25; per-run spend guard */
    long default_max_tokens;            /* default 1024 */
    long max_prompt_bytes;              /* default 32768 */

    /* Optional: write the raw body of the first 200 response to this
     * path (O_EXCL, exactly one capture) for cross-ISA fixture pinning.
     * Presence of this key is recorded via the config commitment. */
    char fixture_capture_path[GW_PATH_MAX];

    /* Optional: mirror every ledger frame to a read-only export at this
     * path (serving-determinism witness, Chair ruling 2026-07-05). The
     * export is a byte-identical prefix of the primary, rewritten from
     * the primary at startup and appended per commit. Empty = disabled.
     * Presence and value are recorded via the config commitment. */
    char export_path[GW_PATH_MAX];

    /* Raw file bytes for the startup AX:STATE:v1 commitment. */
    uint8_t raw[GW_CONFIG_MAX];
    size_t  raw_len;
} gw_config_t;

/* Load and validate. Returns 0 on success, -1 on any error (message to
 * stderr; the service must not start on a config it cannot fully parse). */
int gw_config_load(gw_config_t *cfg, const char *path);

#endif /* GW_CONFIG_H */

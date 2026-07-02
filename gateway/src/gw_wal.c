/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_wal.c - see gw_wal.h.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>

static int fsync_parent_dir(const char *path)
{
    char tmp[512];
    char *dir;
    int fd, rc;
    if (strlen(path) >= sizeof tmp) return -1;
    strcpy(tmp, path);
    dir = dirname(tmp);
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    rc = fsync(fd);
    close(fd);
    return rc;
}

int gw_wal_write(const char *path, const gw_intent_t *in)
{
    int fd;
    char buf[768];
    int n;

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0640);
    if (fd < 0) return -1;   /* exists: sequencing bug, refuse */

    n = snprintf(buf, sizeof buf,
        "input_hash=%s\nmodel_id=%s\nmax_tokens=%ld\nseed=%lld\n"
        "temperature_q16=%ld\ntop_p_q16=%ld\n",
        in->input_hash_hex, in->model_id,
        (long)in->max_tokens, (long long)in->seed,
        (long)in->temperature_q16, (long)in->top_p_q16);
    if (n < 0 || (size_t)n >= sizeof buf) { close(fd); return -1; }

    {
        ssize_t w = write(fd, buf, (size_t)n);
        if (w != (ssize_t)n) { close(fd); return -1; }
    }
    if (fsync(fd) != 0) { close(fd); return -1; }
    close(fd);
    return fsync_parent_dir(path);
}

int gw_wal_clear(const char *path)
{
    if (unlink(path) != 0) return -1;
    return fsync_parent_dir(path);
}

static int parse_field(const char *line, const char *key, char *dst, size_t cap)
{
    size_t kl = strlen(key);
    if (strncmp(line, key, kl) != 0 || line[kl] != '=') return -1;
    {
        size_t vl = strlen(line + kl + 1);
        if (vl == 0 || vl >= cap) return -1;
        memcpy(dst, line + kl + 1, vl + 1);
    }
    return 0;
}

int gw_wal_recover(const char *path, gw_intent_t *out)
{
    FILE *f;
    char line[600];
    char num[64];
    int have = 0;

    f = fopen(path, "rb");
    if (f == NULL) return 0;   /* no intent */

    memset(out, 0, sizeof *out);
    out->temperature_q16 = INT32_MIN;
    out->top_p_q16 = INT32_MIN;
    out->seed = -1;
    out->max_tokens = -1;

    while (fgets(line, sizeof line, f) != NULL) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (parse_field(line, "input_hash", out->input_hash_hex,
                        sizeof out->input_hash_hex) == 0) { have |= 1; continue; }
        if (parse_field(line, "model_id", out->model_id,
                        sizeof out->model_id) == 0)       { have |= 2; continue; }
        if (parse_field(line, "max_tokens", num, sizeof num) == 0)
            { out->max_tokens = (int32_t)strtol(num, NULL, 10); continue; }
        if (parse_field(line, "seed", num, sizeof num) == 0)
            { out->seed = strtoll(num, NULL, 10); continue; }
        if (parse_field(line, "temperature_q16", num, sizeof num) == 0)
            { out->temperature_q16 = (int32_t)strtol(num, NULL, 10); continue; }
        if (parse_field(line, "top_p_q16", num, sizeof num) == 0)
            { out->top_p_q16 = (int32_t)strtol(num, NULL, 10); continue; }
    }
    fclose(f);

    if ((have & 3) != 3 || strlen(out->input_hash_hex) != 64) return -1;
    return 1;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_wal.h - write-ahead intent.
 *
 * The obs schema has no intent state, so the WAL is the design, not a
 * fallback: a durable intent entry exists on disk before any network
 * I/O, and an unclosed intent found at startup is replayed into a
 * TRANSPORT_ERROR observation. Serial service means at most one open
 * intent ever exists, so the WAL is a single file whose existence is
 * the open/closed bit.
 *
 * ledger_seq binds the intent to the chain position its observation
 * will occupy (B-02): recovery decides "already recorded" by that
 * position, never by the prompt, because a prompt can repeat.
 */
#ifndef GW_WAL_H
#define GW_WAL_H

#include <stdint.h>

typedef struct {
    char     input_hash_hex[65];
    char     model_id[256];
    int32_t  max_tokens;
    int64_t  seed;
    int32_t  temperature_q16;   /* INT32_MIN = null */
    int32_t  top_p_q16;         /* INT32_MIN = null */
    uint64_t ledger_seq;        /* expected seq of the obs; 0 = absent (legacy) */
} gw_intent_t;

/* Write the intent durably (write, fsync, fsync dir). Fails if an
 * intent is already open, which would be a sequencing bug. */
int gw_wal_write(const char *path, const gw_intent_t *in);

/* Close the intent (unlink, fsync dir). */
int gw_wal_clear(const char *path);

/* If an intent file exists, load it into *out and return 1.
 * Return 0 if none, -1 on a corrupt intent (refuse to start).
 * An intent written before ledger_seq existed loads with ledger_seq 0. */
int gw_wal_recover(const char *path, gw_intent_t *out);

#endif /* GW_WAL_H */

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_ledger.h - the durable L6 chain seam.
 *
 * This header deliberately uses only plain C types. The implementation
 * compiles against axioma-audit and the axilog substrate; the rest of
 * the gateway compiles against axioma-oracle, whose axilog/types.h is
 * a different type under the same name. No axilog type crosses this
 * seam. See the ABI note in the delivery report.
 *
 * Durable form: an append-only evidence file of framed records
 * (u32le tag_len, tag, u64le payload_len, payload, 32-byte commit).
 * L6 (ax_ledger_ctx_t) provides the chain arithmetic in memory;
 * startup replays the file from genesis, recomputing every commit and
 * every link. The file is the truth; a divergent stored commit refuses
 * service. Never a throwaway ledger: the file persists and grows.
 */
#ifndef GW_LEDGER_H
#define GW_LEDGER_H

#include <stddef.h>
#include <stdint.h>

/* Open (creating if absent) and replay the evidence file.
 * torn_tail_ok: if non-zero, a trailing partial frame is truncated and
 * reported via *truncated (the WAL recovery path will account for it);
 * if zero, a torn tail refuses to open. Returns 0 or -1. */
int gwl_open(const char *path, int torn_tail_ok, int *truncated);

/* Commit evidence and append: recomputes the domain-separated commit
 * on the substrate, writes the frame, fsyncs, extends the chain.
 * tag must be a registered AX_TAG_* string. Returns 0 or -1. */
int gwl_append(const char *tag, const uint8_t *payload, uint64_t payload_len,
               uint8_t head_out[32], uint64_t *seq_out);

/* Current state. */
uint64_t gwl_seq(void);
void     gwl_head(uint8_t out[32]);

/* Does the most recent frame's payload contain the given substring?
 * Used by WAL recovery to detect an intent whose observation did land
 * before the crash (payload carries the input_hash hex). */
int gwl_last_payload_contains(const char *needle);

void gwl_close(void);

#endif /* GW_LEDGER_H */

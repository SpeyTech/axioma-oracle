/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_ledger.h - the durable L6 chain seam.
 *
 * This header deliberately uses only plain C types. The implementation
 * compiles against axioma-audit and the axilog substrate; the rest of
 * the gateway compiles against axioma-oracle. E-ABI-1 (the oracle's
 * former shadow of axilog/types.h) is fixed and ct_fault_flags_t is
 * one type estate-wide, but the seam keeps passing bare bytes: the TU
 * partition is defence in depth, not a workaround for the healed
 * wound. See the ABI note in the delivery report and CONFORMANCE.md
 * section 12.
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

/* Serving-determinism witness export (Chair ruling 2026-07-05).
 * Enable after a successful gwl_open: (re)writes the export file as a
 * byte-identical copy of the replayed primary (healing any torn or
 * stale export from a prior run), mode 0644, then mirrors every
 * subsequent frame appended by gwl_append. The export carries no
 * authority: it is a checkable projection of the primary, and the
 * checker verifies its chain integrity and its head against the
 * serve-time chain_head echoes. Returns 0 or -1 (a configured export
 * that cannot be enabled should refuse service; the caller decides).
 *
 * Mirror-write failure at append time is fail-open for serving and
 * fail-loud for witnessing: the export fd is closed, gwl_export_active
 * drops to 0 (the caller logs the transition), and the stale export is
 * NOT-DISCHARGEABLE at witness time by the checker's staleness check.
 * The export is not fsynced per frame; a torn export tail is detected
 * by the checker and healed at the next enable. */
int gwl_export_enable(const char *path);
int gwl_export_active(void);

/* Does the most recent frame's payload contain the given substring?
 * Used by WAL recovery to detect an intent whose observation did land
 * before the crash (payload carries the input_hash hex). */
int gwl_last_payload_contains(const char *needle);

void gwl_close(void);

#endif /* GW_LEDGER_H */

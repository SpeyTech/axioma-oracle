/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_log.h - structured error/event log.
 *
 * One JSON object per line. Redaction happens here, at the formatting
 * layer, not by caller discipline: the API key value is scrubbed from
 * every message field, and no code path passes request headers in.
 * The message field carries upstream-verbatim text (curl error string,
 * HTTP status line, Anthropic error body message); paraphrased errors
 * would make the EXP-1 corpus synthetic again (confound C1).
 */
#ifndef GW_LOG_H
#define GW_LOG_H

#include <stdint.h>
#include <stddef.h>

/* Open the log file (append). secret may be NULL; if set, every message
 * is scrubbed of it before write. Returns 0 or -1. */
int  gw_log_open(const char *path, const char *secret);
void gw_log_close(void);

/* Emit one structured line. event is a fixed token (startup, intent,
 * attempt, oracle_ok, oracle_fail, obs_committed, encoding_reject,
 * budget_refused, wal_recovered, shutdown, fault). message is
 * upstream-verbatim where one exists, may be NULL. attempt/status are
 * -1 when not applicable. seq is the chain sequence or 0. */
void gw_log(const char *level, const char *event, const char *message,
            int attempt, long status, uint64_t seq);

#endif /* GW_LOG_H */

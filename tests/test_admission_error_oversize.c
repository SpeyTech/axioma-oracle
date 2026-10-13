/**
 * @file test_admission_error_oversize.c
 * @brief PCR-004 residual (a): an ERROR input with output over
 *        AX_MAX_OUTPUT_BYTES is rejected at admission
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Before this correction, admission truncated such output under
 * SRS-004-SHALL-040, set completion_state to TRUNCATED and kept the input
 * failure_type, so the stored record read TRUNCATED/TIMEOUT,
 * TRUNCATED/INVALID_OUTPUT or TRUNCATED/TRANSPORT_ERROR: a pair outside
 * the permitted list. Owner direction 2026-10-12: reject such input with
 * AX_ERR_SCHEMA (faults->schema, no obs_hash, sequence not advanced)
 * before truncation can relabel it. COMPLETE and TRUNCATED inputs keep the
 * SHALL-040 truncation exactly as before.
 *
 * Section A: the rejection. Section B: unchanged boundaries (ERROR at
 * exactly AX_MAX_OUTPUT_BYTES, COMPLETE and TRUNCATED over the limit).
 * Section C: invariant, every admitted record carries a permitted pair.
 * Section D: a rejection does not advance the sequence.
 *
 * Fails on 44deb6d (sections A, C, D); passes after.
 *
 * @traceability SRS-004-SHALL-003, SRS-004-SHALL-040, SRS-004-SHALL-047
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/limits.h"

static char norm[AX_MAX_OUTPUT_BYTES + 1];
static char big[AX_MAX_OUTPUT_BYTES + 4096];
static int fails;
static int runs;

static void report(int ok, const char *name, int rc)
{
    printf("%s  %-66s rc=%d\n", ok ? "PASS" : "FAIL", name, rc);
    runs++;
    fails += !ok;
}

static void base(ax_obs_input_t *in, ax_completion_state_t s,
                 ax_failure_type_t f, size_t olen)
{
    memset(in, 0, sizeof *in);
    in->completion_state = s;
    in->failure_type = f;
    in->input = "prompt"; in->input_len = 6;
    in->ledger_seq = 1; in->model_id = "m"; in->oracle_id = "o";
    in->output = big; in->output_len = olen;
    ax_oracle_params_init_null(&in->params);
}

static int pair_permitted(ax_completion_state_t s, ax_failure_type_t f)
{
    if (s == AX_COMPLETION_COMPLETE || s == AX_COMPLETION_TRUNCATED) {
        return f == AX_FAILURE_NULL;
    }
    if (s == AX_COMPLETION_ERROR) {
        return f == AX_FAILURE_TIMEOUT || f == AX_FAILURE_INVALID_OUTPUT ||
               f == AX_FAILURE_TRANSPORT_ERROR;
    }
    return 0;
}

static int admit(const ax_obs_input_t *in, ax_obs_record_t *rec,
                 ax_admission_ctx_t *ctx, ax_l3_fault_flags_t *f)
{
    ax_admission_ctx_init(ctx); ax_obs_init(rec); memset(f, 0, sizeof *f);
    return ax_obs_admit(rec, norm, sizeof norm, in, ctx, f);
}

static const ax_failure_type_t err_types[3] = {
    AX_FAILURE_TIMEOUT, AX_FAILURE_INVALID_OUTPUT, AX_FAILURE_TRANSPORT_ERROR
};
static const char *const err_names[3] = {
    "TIMEOUT", "INVALID_OUTPUT", "TRANSPORT_ERROR"
};

static void section_a(void)
{
    static const size_t lens[3] = {
        AX_MAX_OUTPUT_BYTES + 1, AX_MAX_OUTPUT_BYTES + 4000,
        AX_MAX_OUTPUT_BYTES + 4096
    };
    char name[128];
    size_t i, j;

    printf("-- A: ERROR input with output over AX_MAX_OUTPUT_BYTES rejected\n");
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) {
        ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
        ax_l3_fault_flags_t f; int rc;
        base(&in, AX_COMPLETION_ERROR, err_types[j], lens[i]);
        rc = admit(&in, &rec, &ctx, &f);
        snprintf(name, sizeof name, "ERROR/%s output_len %zu rejected (schema)",
                 err_names[j], lens[i]);
        report(rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
               !ctx.initialized && ctx.last_ledger_seq == 0, name, rc);
    }
}

static void section_b(void)
{
    ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
    ax_l3_fault_flags_t f; int rc;
    char name[128];
    size_t j;

    printf("-- B: unchanged boundaries\n");
    for (j = 0; j < 3; j++) {
        base(&in, AX_COMPLETION_ERROR, err_types[j], AX_MAX_OUTPUT_BYTES);
        rc = admit(&in, &rec, &ctx, &f);
        snprintf(name, sizeof name, "ERROR/%s output_len == limit admitted, not truncated",
                 err_names[j]);
        report(rc == AX_OK && rec.completion_state == AX_COMPLETION_ERROR &&
               rec.failure_type == err_types[j] &&
               rec.output_size == AX_MAX_OUTPUT_BYTES && !f.size, name, rc);
    }

    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, AX_MAX_OUTPUT_BYTES + 1);
    rc = admit(&in, &rec, &ctx, &f);
    report(rc == AX_OK && rec.completion_state == AX_COMPLETION_TRUNCATED &&
           rec.failure_type == AX_FAILURE_NULL &&
           rec.output_size == AX_MAX_OUTPUT_BYTES + 1 &&
           strlen(rec.output) == AX_MAX_OUTPUT_BYTES && f.size,
           "COMPLETE over limit: TRUNCATED/NULL, SHALL-040 unchanged", rc);

    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, AX_MAX_OUTPUT_BYTES + 4000);
    rc = admit(&in, &rec, &ctx, &f);
    report(rc == AX_OK && rec.completion_state == AX_COMPLETION_TRUNCATED &&
           rec.failure_type == AX_FAILURE_NULL &&
           rec.output_size == AX_MAX_OUTPUT_BYTES + 4000,
           "COMPLETE far over limit: TRUNCATED/NULL, original size kept", rc);

    base(&in, AX_COMPLETION_TRUNCATED, AX_FAILURE_NULL, AX_MAX_OUTPUT_BYTES + 1);
    rc = admit(&in, &rec, &ctx, &f);
    report(rc == AX_OK && rec.completion_state == AX_COMPLETION_TRUNCATED &&
           rec.failure_type == AX_FAILURE_NULL &&
           rec.output_size == AX_MAX_OUTPUT_BYTES + 1,
           "TRUNCATED over limit: TRUNCATED/NULL unchanged", rc);
}

/* Every permitted input pair, at lengths around the limit: an admitted
 * record never carries a pair outside the permitted list. */
static void section_c(void)
{
    static const ax_completion_state_t st[5] = {
        AX_COMPLETION_COMPLETE, AX_COMPLETION_TRUNCATED,
        AX_COMPLETION_ERROR, AX_COMPLETION_ERROR, AX_COMPLETION_ERROR
    };
    static const ax_failure_type_t ft[5] = {
        AX_FAILURE_NULL, AX_FAILURE_NULL, AX_FAILURE_TIMEOUT,
        AX_FAILURE_INVALID_OUTPUT, AX_FAILURE_TRANSPORT_ERROR
    };
    static const size_t lens[4] = {
        0, AX_MAX_OUTPUT_BYTES, AX_MAX_OUTPUT_BYTES + 1, AX_MAX_OUTPUT_BYTES + 4000
    };
    size_t i, j;
    int bad = 0, admitted = 0;

    printf("-- C: every admitted record carries a permitted pair\n");
    for (i = 0; i < 5; i++) for (j = 0; j < 4; j++) {
        ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
        ax_l3_fault_flags_t f; int rc;
        base(&in, st[i], ft[i], lens[j]);
        rc = admit(&in, &rec, &ctx, &f);
        if (rc == AX_OK) {
            admitted++;
            if (!pair_permitted(rec.completion_state, rec.failure_type)) {
                bad++;
                printf("      input %d/%d len %zu stored as %d/%d\n",
                       (int)st[i], (int)ft[i], lens[j],
                       (int)rec.completion_state, (int)rec.failure_type);
            }
        }
    }
    report(bad == 0 && admitted == 14,
           "20 inputs: 14 admitted, none with a forbidden stored pair", bad);
}

static void section_d(void)
{
    ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
    ax_l3_fault_flags_t f; int rc;

    printf("-- D: rejection does not advance the sequence\n");
    ax_admission_ctx_init(&ctx);
    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, 2); in.ledger_seq = 1;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && ctx.last_ledger_seq == 1, "seq 1 admitted", rc);

    base(&in, AX_COMPLETION_ERROR, AX_FAILURE_TIMEOUT, AX_MAX_OUTPUT_BYTES + 1);
    in.ledger_seq = 2;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
           ctx.last_ledger_seq == 1,
           "seq 2 oversized ERROR rejected, context still at seq 1", rc);

    base(&in, AX_COMPLETION_ERROR, AX_FAILURE_TIMEOUT, 0); in.ledger_seq = 2;
    in.output = NULL;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && ctx.last_ledger_seq == 2,
           "seq 2 ERROR/TIMEOUT without output admitted after the rejection", rc);
}

int main(void)
{
    memset(big, 'a', sizeof big);
    printf("PCR-004 residual (a): oversized ERROR output\n");
    section_a();
    section_b();
    section_c();
    section_d();
    printf("%d/%d passed, %d failing: %s\n", runs - fails, runs, fails,
           fails ? "RESIDUAL (a) NOT CORRECTED" : "RESIDUAL (a) CORRECTED");
    return fails ? 1 : 0;
}

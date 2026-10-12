/**
 * @file test_admission_null_output.c
 * @brief PCR-004 residual (b): NULL output with output_len > 0 is rejected
 *        at admission; NULL output with output_len == 0 is unchanged
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Before this correction, output == NULL with output_len > 0 was admitted
 * as an empty output with output_size 0, and over AX_MAX_OUTPUT_BYTES as
 * an empty TRUNCATED output carrying the claimed size. The record then
 * committed to an output that was never supplied. Owner direction
 * 2026-10-12: reject with AX_ERR_SCHEMA (faults->schema, no obs_hash,
 * sequence not advanced). NULL output with output_len == 0 stays
 * admissible exactly as before: section B pins the full v1 record and
 * obs_hash for every permitted pair, values taken from 44deb6d.
 *
 * Fails on 44deb6d (sections A and C); passes after.
 *
 * @traceability SRS-004-SHALL-003, SRS-004-SHALL-040, SRS-004-SHALL-047
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"
#include "axilog/limits.h"

static char norm[AX_MAX_OUTPUT_BYTES + 1];
static char canon[AX_CANONICAL_BUFFER_SIZE];
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
    in->output = NULL; in->output_len = olen;
    ax_oracle_params_init_null(&in->params);
}

static const ax_completion_state_t st[5] = {
    AX_COMPLETION_COMPLETE, AX_COMPLETION_TRUNCATED,
    AX_COMPLETION_ERROR, AX_COMPLETION_ERROR, AX_COMPLETION_ERROR
};
static const ax_failure_type_t ft[5] = {
    AX_FAILURE_NULL, AX_FAILURE_NULL, AX_FAILURE_TIMEOUT,
    AX_FAILURE_INVALID_OUTPUT, AX_FAILURE_TRANSPORT_ERROR
};
static const char *const pair_names[5] = {
    "COMPLETE/NULL", "TRUNCATED/NULL", "ERROR/TIMEOUT",
    "ERROR/INVALID_OUTPUT", "ERROR/TRANSPORT_ERROR"
};

/* v1 records for the base input with output NULL, output_len 0, as
 * admitted by 44deb6d. Must not change. */
#define REC_HEAD(state, fail) \
    "{\"completion_state\":\"" state "\",\"failure_type\":" fail \
    ",\"input_hash\":\"cf07194ee232eb531e15f690000d19846dea69cf05504782658afcfacb9228a2\"," \
    "\"ledger_seq\":1,\"model_id\":\"m\",\"obs_hash\":\""
#define REC_TAIL \
    "\",\"oracle_id\":\"o\",\"output\":\"\",\"output_size\":0,\"params\":" \
    "{\"max_tokens\":null,\"seed\":null,\"temperature\":null,\"top_p\":null}," \
    "\"schema_version\":\"AX:OBS:v1\"}"
#define H0 "ec316210eed467d7b70db165686ec48b33223da5d338405dba834da9e7d4bf26"
#define H1 "d9711f69bf702e0d646b38f1df5ea435c699cc71c82cf9839572b11511fc25c1"
#define H2 "094f368c2d4564e0f629288b292247e074faab8fe1fe6aa209f568a7e77755a6"
#define H3 "54fcdad6e709115884481fa5d6c8d89c512c9a4d8d0ece5aa4b1a3ddf4af9542"
#define H4 "073505f67ef431b139e92454fb73ad58945d59b41d34192f8e2e9d9c77f8c2bc"
static const char *const pin_hash[5] = { H0, H1, H2, H3, H4 };
static const char *const pin_rec[5] = {
    REC_HEAD("COMPLETE", "null") H0 REC_TAIL,
    REC_HEAD("TRUNCATED", "null") H1 REC_TAIL,
    REC_HEAD("ERROR", "\"TIMEOUT\"") H2 REC_TAIL,
    REC_HEAD("ERROR", "\"INVALID_OUTPUT\"") H3 REC_TAIL,
    REC_HEAD("ERROR", "\"TRANSPORT_ERROR\"") H4 REC_TAIL
};

static void section_a(void)
{
    static const size_t lens[4] = {
        1, 5, AX_MAX_OUTPUT_BYTES, AX_MAX_OUTPUT_BYTES + 1
    };
    char name[128];
    size_t i, j;

    printf("-- A: NULL output with output_len > 0 rejected\n");
    for (i = 0; i < 5; i++) for (j = 0; j < 4; j++) {
        ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
        ax_l3_fault_flags_t f; int rc;
        base(&in, st[i], ft[i], lens[j]);
        ax_admission_ctx_init(&ctx); ax_obs_init(&rec); memset(&f, 0, sizeof f);
        rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
        snprintf(name, sizeof name, "%s output NULL, output_len %zu rejected (schema)",
                 pair_names[i], lens[j]);
        report(rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
               !ctx.initialized && ctx.last_ledger_seq == 0, name, rc);
    }
}

static void section_b(void)
{
    char name[128];
    size_t i;

    printf("-- B: NULL output with output_len 0 admitted, v1 record pinned\n");
    for (i = 0; i < 5; i++) {
        ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
        ax_l3_fault_flags_t f; int rc, len, ok;
        base(&in, st[i], ft[i], 0);
        ax_admission_ctx_init(&ctx); ax_obs_init(&rec); memset(&f, 0, sizeof f);
        rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
        len = (rc == AX_OK) ? ax_obs_canonicalise(canon, sizeof canon, &rec, 1) : 0;
        ok = rc == AX_OK && strcmp(rec.obs_hash, pin_hash[i]) == 0 &&
             len == (int)strlen(pin_rec[i]) && strcmp(canon, pin_rec[i]) == 0;
        if (!ok) {
            printf("      got obs_hash=%s\n", rec.obs_hash);
        }
        snprintf(name, sizeof name, "%s output NULL, output_len 0: pinned record",
                 pair_names[i]);
        report(ok, name, rc);
    }
}

static void section_c(void)
{
    ax_obs_input_t in; ax_obs_record_t rec; ax_admission_ctx_t ctx;
    ax_l3_fault_flags_t f; int rc;

    printf("-- C: rejection does not advance the sequence\n");
    ax_admission_ctx_init(&ctx);
    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, 0); in.ledger_seq = 1;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && ctx.last_ledger_seq == 1, "seq 1 admitted", rc);

    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, 3); in.ledger_seq = 2;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
           ctx.last_ledger_seq == 1,
           "seq 2 NULL output with length 3 rejected, context still at seq 1", rc);

    base(&in, AX_COMPLETION_COMPLETE, AX_FAILURE_NULL, 2); in.ledger_seq = 2;
    in.output = "ok";
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && ctx.last_ledger_seq == 2,
           "seq 2 well-formed admitted after the rejection", rc);
}

int main(void)
{
    printf("PCR-004 residual (b): NULL output with output_len > 0\n");
    section_a();
    section_b();
    section_c();
    printf("%d/%d passed, %d failing: %s\n", runs - fails, runs, fails,
           fails ? "RESIDUAL (b) NOT CORRECTED" : "RESIDUAL (b) CORRECTED");
    return fails ? 1 : 0;
}

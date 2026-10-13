/**
 * @file test_admission_fail_closed.c
 * @brief PCR-004 (B-07): admission fails closed on malformed identifiers,
 *        enums, enum pairs and inputs
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Section A is the PCR-004 regression test written ahead of implementation
 * (axioma-stage1 proposals/tests/pcr004_identifier_fail_closed.c): the
 * guard plus the 12 negative cases, unchanged in substance. Each malformed
 * admission must return AX_ERR_SCHEMA with faults->schema set, no obs_hash
 * and no admission context change.
 *
 * Section B adds boundary cases around the same rules.
 * Section C pins the v1 empty-input hash: input_len == 0 stays admissible
 * and its input_hash stays 32 zero bytes, exactly as v1 computed it before
 * PCR-004 (owner decision 2026-10-12: correction deferred to AX:OBS:v2).
 * Section D shows a rejected admission does not advance the sequence.
 *
 * Fails on 57ec02b and bec7f03 (sections A, B negatives, D); passes after.
 *
 * @traceability SRS-004-SHALL-003, SRS-004-SHALL-038, SRS-004-SHALL-047
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"

static char norm[4096];
static char canon[AX_CANONICAL_BUFFER_SIZE];
static int fails;
static int runs;

static void report(int ok, const char *name, int rc)
{
    printf("%s  %-62s rc=%d\n", ok ? "PASS" : "FAIL", name, rc);
    runs++;
    fails += !ok;
}

static void base(ax_obs_input_t *in)
{
    memset(in, 0, sizeof *in);
    in->completion_state = AX_COMPLETION_COMPLETE;
    in->failure_type = AX_FAILURE_NULL;
    in->input = "prompt"; in->input_len = 6;
    in->ledger_seq = 1; in->model_id = "m"; in->oracle_id = "o";
    in->output = "ok"; in->output_len = 2;
    ax_oracle_params_init_null(&in->params);
}

/* want: AX_OK for admitted; AX_ERR_SCHEMA (or another code) for rejected.
 * A rejection must also leave no obs_hash and an untouched fresh context. */
static void expect_rc(const char *name, const ax_obs_input_t *in, int want)
{
    ax_admission_ctx_t ctx; ax_obs_record_t rec; ax_l3_fault_flags_t f; int rc, ok;
    ax_admission_ctx_init(&ctx); ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, in, &ctx, &f);
    if (want == AX_OK) {
        ok = rc == AX_OK && rec.obs_hash[0] != '\0' && ctx.initialized;
    } else if (want == AX_ERR_SCHEMA) {
        ok = rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
             !ctx.initialized && ctx.last_ledger_seq == 0;
    } else {
        ok = rc == want && rec.obs_hash[0] == '\0' && !ctx.initialized;
    }
    report(ok, name, rc);
}

static void expect(const char *name, const ax_obs_input_t *in, int want_ok)
{
    expect_rc(name, in, want_ok ? AX_OK : AX_ERR_SCHEMA);
}

static void section_a(void)
{
    ax_obs_input_t in;
    printf("-- A: PCR-004 regression cases\n");
    base(&in); expect("guard: well-formed record admitted", &in, 1);
    base(&in); in.model_id = NULL;   expect("1  model_id NULL rejected", &in, 0);
    base(&in); in.model_id = "";     expect("1  model_id empty rejected", &in, 0);
    base(&in); in.oracle_id = NULL;  expect("1  oracle_id NULL rejected", &in, 0);
    base(&in); in.oracle_id = "";    expect("1  oracle_id empty rejected", &in, 0);
    base(&in); in.model_id = "m\x01"; expect("2  C0 control in model_id rejected", &in, 0);
    base(&in); in.oracle_id = "o\x7f"; expect("2  DEL in oracle_id rejected", &in, 0);
    base(&in); in.model_id = "m\xff"; expect("2  invalid UTF-8 in model_id rejected", &in, 0);
    base(&in); in.completion_state = (ax_completion_state_t)7; expect("3  completion_state out of enum rejected", &in, 0);
    base(&in); in.failure_type = (ax_failure_type_t)9; expect("3  failure_type out of enum rejected", &in, 0);
    base(&in); in.failure_type = AX_FAILURE_TIMEOUT; expect("4  COMPLETE with TIMEOUT rejected", &in, 0);
    base(&in); in.completion_state = AX_COMPLETION_ERROR; in.output = ""; in.output_len = 0;
               expect("4  ERROR with failure NULL rejected", &in, 0);
    base(&in); in.input = NULL; in.input_len = 5; expect("5  input NULL with input_len > 0 rejected", &in, 0);
}

static void section_b(void)
{
    ax_obs_input_t in;
    static char long_id[AX_MAX_MODEL_ID_BYTES + 2];
    printf("-- B: boundaries\n");

    /* every permitted pair is admitted */
    base(&in); in.completion_state = AX_COMPLETION_TRUNCATED;
               expect("pair TRUNCATED/NULL admitted", &in, 1);
    base(&in); in.completion_state = AX_COMPLETION_ERROR; in.failure_type = AX_FAILURE_TIMEOUT;
               in.output = NULL; in.output_len = 0;
               expect("pair ERROR/TIMEOUT admitted", &in, 1);
    base(&in); in.completion_state = AX_COMPLETION_ERROR; in.failure_type = AX_FAILURE_INVALID_OUTPUT;
               expect("pair ERROR/INVALID_OUTPUT admitted", &in, 1);
    base(&in); in.completion_state = AX_COMPLETION_ERROR; in.failure_type = AX_FAILURE_TRANSPORT_ERROR;
               in.output = NULL; in.output_len = 0;
               expect("pair ERROR/TRANSPORT_ERROR admitted", &in, 1);

    /* further forbidden pairs and enum values */
    base(&in); in.completion_state = AX_COMPLETION_TRUNCATED; in.failure_type = AX_FAILURE_INVALID_OUTPUT;
               expect("pair TRUNCATED/INVALID_OUTPUT rejected", &in, 0);
    base(&in); in.failure_type = AX_FAILURE_TRANSPORT_ERROR;
               expect("pair COMPLETE/TRANSPORT_ERROR rejected", &in, 0);
    base(&in); in.completion_state = (ax_completion_state_t)-1;
               expect("completion_state -1 rejected", &in, 0);
    base(&in); in.completion_state = AX_COMPLETION_ERROR; in.failure_type = (ax_failure_type_t)4;
               expect("ERROR with failure_type 4 rejected", &in, 0);

    /* identifier content */
    base(&in); in.oracle_id = "o\n";   expect("LF in oracle_id rejected", &in, 0);
    base(&in); in.model_id = "\tm";    expect("TAB in model_id rejected", &in, 0);
    base(&in); in.model_id = "m\xed\xa0\x80"; expect("UTF-16 surrogate in model_id rejected", &in, 0);
    base(&in); in.oracle_id = "o\xc0\xaf"; expect("overlong UTF-8 in oracle_id rejected", &in, 0);
    base(&in); in.oracle_id = "o\xe2\x82"; expect("truncated UTF-8 in oracle_id rejected", &in, 0);
    base(&in); in.model_id = "mod\xc3\xa9le-\xe2\x9c\x93"; expect("valid non-ASCII UTF-8 identifier admitted", &in, 1);
    base(&in); in.model_id = "e\xcc\x81"; expect("non-NFC identifier admitted (NFC not required)", &in, 1);
    base(&in); in.model_id = "a b\"c\\d"; expect("space, quote, backslash in identifier admitted", &in, 1);

    /* existing error codes keep precedence */
    memset(long_id, 'x', AX_MAX_MODEL_ID_BYTES);
    long_id[AX_MAX_MODEL_ID_BYTES] = '\0';
    base(&in); in.model_id = long_id; expect("256-byte identifier admitted", &in, 1);
    long_id[AX_MAX_MODEL_ID_BYTES] = '\x01';
    long_id[AX_MAX_MODEL_ID_BYTES + 1] = '\0';
    base(&in); in.model_id = long_id;
               expect_rc("257-byte identifier with C0 keeps AX_ERR_SIZE", &in, AX_ERR_SIZE);
    {
        ax_admission_ctx_t ctx; ax_obs_record_t rec; ax_l3_fault_flags_t f; int rc;
        ax_admission_ctx_init(&ctx); ctx.initialized = 1; ctx.last_ledger_seq = 5;
        base(&in); in.ledger_seq = 5; in.model_id = NULL;
        ax_obs_init(&rec); memset(&f, 0, sizeof f);
        rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
        report(rc == AX_ERR_ORDERING && f.ordering && !f.schema &&
               ctx.last_ledger_seq == 5,
               "ordering violation with NULL model_id keeps AX_ERR_ORDERING", rc);
    }

    /* input */
    base(&in); in.input = NULL; in.input_len = 1; expect("input NULL with input_len 1 rejected", &in, 0);
}

/* v1 empty-input hash as computed by bec7f03 (and 57ec02b) for the base
 * record with input_len == 0. Must not change under AX:OBS:v1. */
#define PIN_EMPTY_INPUT_OBS_HASH \
    "f6b87c0e472e2dba5fb9135645972906ae48fa4aa00e404d4bb5ce279c58f814"
#define PIN_EMPTY_INPUT_RECORD \
    "{\"completion_state\":\"COMPLETE\",\"failure_type\":null,\"input_hash\":" \
    "\"0000000000000000000000000000000000000000000000000000000000000000\"," \
    "\"ledger_seq\":1,\"model_id\":\"m\",\"obs_hash\":\"" PIN_EMPTY_INPUT_OBS_HASH \
    "\",\"oracle_id\":\"o\",\"output\":\"ok\",\"output_size\":2,\"params\":" \
    "{\"max_tokens\":null,\"seed\":null,\"temperature\":null,\"top_p\":null}," \
    "\"schema_version\":\"AX:OBS:v1\"}"

static void section_c(void)
{
    static const uint8_t zero[AX_HASH_SIZE];
    const char *forms[2];
    const char *names[2];
    int i;

    forms[0] = NULL; names[0] = "empty input (NULL, 0): admitted, v1 hash pinned";
    forms[1] = "";   names[1] = "empty input (\"\", 0): admitted, v1 hash pinned";
    printf("-- C: empty input, v1 behaviour retained\n");
    for (i = 0; i < 2; i++) {
        ax_obs_input_t in; ax_admission_ctx_t ctx; ax_obs_record_t rec;
        ax_l3_fault_flags_t f; int rc, len, ok;
        base(&in); in.input = forms[i]; in.input_len = 0;
        ax_admission_ctx_init(&ctx); ax_obs_init(&rec); memset(&f, 0, sizeof f);
        rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
        len = ax_obs_canonicalise(canon, sizeof canon, &rec, 1);
        ok = rc == AX_OK &&
             memcmp(rec.input_hash, zero, AX_HASH_SIZE) == 0 &&
             strcmp(rec.obs_hash, PIN_EMPTY_INPUT_OBS_HASH) == 0 &&
             len == (int)strlen(PIN_EMPTY_INPUT_RECORD) &&
             strcmp(canon, PIN_EMPTY_INPUT_RECORD) == 0;
        if (!ok) {
            printf("      got obs_hash=%s\n      got record=%s\n", rec.obs_hash, canon);
        }
        report(ok, names[i], rc);
    }
}

static void section_d(void)
{
    ax_obs_input_t in; ax_admission_ctx_t ctx; ax_obs_record_t rec;
    ax_l3_fault_flags_t f; int rc;

    printf("-- D: rejected admission does not advance the sequence\n");
    ax_admission_ctx_init(&ctx);

    base(&in); in.ledger_seq = 1;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && ctx.initialized && ctx.last_ledger_seq == 1,
           "seq 1 well-formed admitted", rc);

    base(&in); in.ledger_seq = 2; in.oracle_id = "";
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_ERR_SCHEMA && f.schema && rec.obs_hash[0] == '\0' &&
           ctx.initialized && ctx.last_ledger_seq == 1,
           "seq 2 malformed rejected, context still at seq 1", rc);

    base(&in); in.ledger_seq = 2; in.failure_type = AX_FAILURE_TIMEOUT;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_ERR_SCHEMA && f.schema && ctx.last_ledger_seq == 1,
           "seq 2 forbidden pair rejected, context still at seq 1", rc);

    base(&in); in.ledger_seq = 2;
    ax_obs_init(&rec); memset(&f, 0, sizeof f);
    rc = ax_obs_admit(&rec, norm, sizeof norm, &in, &ctx, &f);
    report(rc == AX_OK && rec.obs_hash[0] != '\0' && ctx.last_ledger_seq == 2,
           "seq 2 well-formed admitted after the rejections", rc);
}

int main(void)
{
    printf("PCR-004 admission fail-closed (B-07)\n");
    section_a();
    section_b();
    section_c();
    section_d();
    printf("%d/%d passed, %d failing: %s\n", runs - fails, runs, fails,
           fails ? "PCR-004 NOT SATISFIED" : "PCR-004 SATISFIED");
    return fails ? 1 : 0;
}

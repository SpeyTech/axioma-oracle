/**
 * @file test_replay_identity.c
 * @brief Replay identity tests for AX:OBS:v1
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-017, SRS-004-SHALL-018
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"
#include "axilog/types.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  [TEST] %s... ", name); \
        tests_run++; \
    } while(0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while(0)

/* Static buffers */
static char output_buf1[4096];
static char output_buf2[4096];
static char canonical_buf1[8192];
static char canonical_buf2[8192];

/* ========================================================================
 * Test: Identical Input → Identical Observation
 * ======================================================================== */

static void test_replay_identical_input(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;

    TEST("replay_identical_input");

    /* Setup identical input */
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 42;
    in.oracle_id = "azure-openai-prod";
    in.model_id = "gpt-4-turbo-2024-04-09";
    in.input = "What is the meaning of life?";
    in.input_len = 28;
    in.output = "The answer is 42.";
    in.output_len = 17;
    in.params.max_tokens = 4096;
    in.params.seed = AX_PARAMS_NULL_INT64;
    in.params.temperature = 45875;  /* 0.7 */
    in.params.top_p = 58982;        /* 0.9 */

    /* First admission */
    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    /* Second admission (simulating replay) */
    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    /* Compare hashes */
    if (strcmp(obs1.obs_hash, obs2.obs_hash) == 0) {
        PASS();
    } else {
        FAIL("obs_hash mismatch on replay");
        printf("    Hash1: %s\n", obs1.obs_hash);
        printf("    Hash2: %s\n", obs2.obs_hash);
    }
}

static void test_replay_canonical_identity(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;
    int len1, len2;

    TEST("replay_canonical_identity");

    /* Setup input */
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 100;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "Test prompt";
    in.input_len = 11;
    in.output = "Test response with UTF-8: café";
    in.output_len = 31;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    /* Canonicalise both */
    len1 = ax_obs_canonicalise(canonical_buf1, sizeof(canonical_buf1), &obs1, 1);
    len2 = ax_obs_canonicalise(canonical_buf2, sizeof(canonical_buf2), &obs2, 1);

    if (len1 == len2 && strcmp(canonical_buf1, canonical_buf2) == 0) {
        PASS();
    } else {
        FAIL("canonical form mismatch");
        printf("    Len1=%d, Len2=%d\n", len1, len2);
    }
}

/* ========================================================================
 * Test: Input Hash Stability
 * ======================================================================== */

static void test_input_hash_replay_stable(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;

    TEST("input_hash_replay_stable");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "Deterministic input for hashing";
    in.input_len = 31;
    in.output = "output";
    in.output_len = 6;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    if (memcmp(obs1.input_hash, obs2.input_hash, AX_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("input_hash mismatch");
    }
}

/* ========================================================================
 * Test: Different Input → Different Observation
 * ======================================================================== */

static void test_different_input_different_hash(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in1, in2;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;

    TEST("different_input_different_hash");

    /* First input */
    memset(&in1, 0, sizeof(in1));
    in1.completion_state = AX_COMPLETION_COMPLETE;
    in1.failure_type = AX_FAILURE_NULL;
    in1.ledger_seq = 1;
    in1.oracle_id = "oracle";
    in1.model_id = "model";
    in1.input = "Input A";
    in1.input_len = 7;
    in1.output = "Output A";
    in1.output_len = 8;
    ax_oracle_params_init_null(&in1.params);

    /* Second input - different */
    memset(&in2, 0, sizeof(in2));
    in2.completion_state = AX_COMPLETION_COMPLETE;
    in2.failure_type = AX_FAILURE_NULL;
    in2.ledger_seq = 1;
    in2.oracle_id = "oracle";
    in2.model_id = "model";
    in2.input = "Input B";
    in2.input_len = 7;
    in2.output = "Output B";
    in2.output_len = 8;
    ax_oracle_params_init_null(&in2.params);

    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in1, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in2, &ctx2, &faults2);

    if (strcmp(obs1.obs_hash, obs2.obs_hash) != 0) {
        PASS();
    } else {
        FAIL("different inputs produced same hash");
    }
}

/* ========================================================================
 * Test: Validation Replay
 * ======================================================================== */

static void test_validation_on_replay(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;

    TEST("validation_on_replay");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "test output";
    in.output_len = 11;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    ax_obs_admit(&obs, output_buf1, sizeof(output_buf1), &in, &ctx, &faults);

    /* Validate the admitted observation */
    ct_fault_clear(&faults);
    result = ax_obs_validate(&obs, &faults);

    if (result == AX_OK && !ct_fault_any(&faults)) {
        PASS();
    } else {
        FAIL("validation failed on valid observation");
    }
}

static void test_tampered_obs_fails_validation(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;

    TEST("tampered_obs_fails_validation");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "test output";
    in.output_len = 11;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    ax_obs_admit(&obs, output_buf1, sizeof(output_buf1), &in, &ctx, &faults);

    /* Tamper with output */
    /* Note: output points to output_buf1, so we modify that */
    output_buf1[0] = 'X';  /* Change first character */

    /* Validation should fail due to hash mismatch */
    ct_fault_clear(&faults);
    result = ax_obs_validate(&obs, &faults);

    if (result == AX_ERR_HASH && faults.protocol) {
        PASS();
    } else {
        FAIL("tampered observation should fail validation");
        printf("    result=%d, protocol_fault=%d\n", result, faults.protocol);
    }
}

/* ========================================================================
 * Test: Params Replay
 * ======================================================================== */

static void test_params_replay_identity(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;
    char params1[256], params2[256];

    TEST("params_replay_identity");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "test";
    in.output_len = 4;
    in.params.max_tokens = 8192;
    in.params.seed = 42;
    in.params.temperature = 45875;
    in.params.top_p = 65536;

    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    ax_params_canonicalise(params1, sizeof(params1), &obs1.params);
    ax_params_canonicalise(params2, sizeof(params2), &obs2.params);

    if (strcmp(params1, params2) == 0) {
        PASS();
    } else {
        FAIL("params canonicalisation not deterministic");
    }
}

/* ========================================================================
 * Test: Multi-byte UTF-8 Replay
 * ======================================================================== */

static void test_utf8_replay_identity(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults1, faults2;

    TEST("utf8_replay_identity");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "UTF-8 test: Héllo Wörld 日本語 🎉";
    in.input_len = strlen(in.input);
    in.output = "Response: Ça va bien! Spëçïäl çhàrâctërs.";
    in.output_len = strlen(in.output);
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    if (strcmp(obs1.obs_hash, obs2.obs_hash) == 0 &&
        memcmp(obs1.input_hash, obs2.input_hash, AX_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("UTF-8 content not replaying identically");
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Replay Identity Tests ===\n\n");

    printf("[Identical Input Replay]\n");
    test_replay_identical_input();
    test_replay_canonical_identity();
    test_input_hash_replay_stable();

    printf("\n[Different Input]\n");
    test_different_input_different_hash();

    printf("\n[Validation Replay]\n");
    test_validation_on_replay();
    test_tampered_obs_fails_validation();

    printf("\n[Params Replay]\n");
    test_params_replay_identity();

    printf("\n[UTF-8 Replay]\n");
    test_utf8_replay_identity();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

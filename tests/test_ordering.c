/**
 * @file test_ordering.c
 * @brief Ledger sequence ordering tests for AX:OBS:v1
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-038
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
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

/* Helper to create input */
static void create_test_input(ax_obs_input_t *in, uint64_t seq)
{
    memset(in, 0, sizeof(*in));
    in->completion_state = AX_COMPLETION_COMPLETE;
    in->failure_type = AX_FAILURE_NULL;
    in->ledger_seq = seq;
    in->oracle_id = "test-oracle";
    in->model_id = "test-model";
    in->input = "test input";
    in->input_len = 10;
    in->output = "test output";
    in->output_len = 11;
    ax_oracle_params_init_null(&in->params);
}

/* ========================================================================
 * Test: Ordering Enforcement
 * ======================================================================== */

static void test_ordering_first_admission(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int result;

    TEST("ordering_first_admission");

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    create_test_input(&in, 1);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (result == AX_OK && !ct_fault_any(&faults)) {
        PASS();
    } else {
        FAIL("first admission should succeed");
    }
}

static void test_ordering_sequential(void)
{
    ax_obs_record_t obs1, obs2, obs3;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int r1, r2, r3;

    TEST("ordering_sequential");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 1);
    r1 = ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    ct_fault_clear(&faults);
    create_test_input(&in, 2);
    r2 = ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    ct_fault_clear(&faults);
    create_test_input(&in, 3);
    r3 = ax_obs_admit(&obs3, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (r1 == AX_OK && r2 == AX_OK && r3 == AX_OK) {
        PASS();
    } else {
        FAIL("sequential admissions should succeed");
    }
}

static void test_ordering_gap_allowed(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int r1, r2;

    TEST("ordering_gap_allowed");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 1);
    r1 = ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Skip to sequence 100 - gaps should be allowed */
    ct_fault_clear(&faults);
    create_test_input(&in, 100);
    r2 = ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (r1 == AX_OK && r2 == AX_OK) {
        PASS();
    } else {
        FAIL("sequence gaps should be allowed");
    }
}

static void test_ordering_regression_fail(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int r1, r2;

    TEST("ordering_regression_fail");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 10);
    r1 = ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Try to go backwards - MUST fail */
    ct_fault_clear(&faults);
    create_test_input(&in, 5);
    r2 = ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (r1 == AX_OK && r2 == AX_ERR_ORDERING && faults.ordering) {
        PASS();
    } else {
        FAIL("regression should fail with ordering fault");
        printf("    r1=%d, r2=%d, ordering_fault=%d\n", r1, r2, faults.ordering);
    }
}

static void test_ordering_duplicate_fail(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int r1, r2;

    TEST("ordering_duplicate_fail");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 10);
    r1 = ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Try same sequence again - MUST fail */
    ct_fault_clear(&faults);
    create_test_input(&in, 10);
    r2 = ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (r1 == AX_OK && r2 == AX_ERR_ORDERING && faults.ordering) {
        PASS();
    } else {
        FAIL("duplicate sequence should fail");
    }
}

static void test_ordering_large_sequence(void)
{
    ax_obs_record_t obs1, obs2;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int r1, r2;

    TEST("ordering_large_sequence");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 0xFFFFFFFFFFFFFFFEULL);
    r1 = ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    ct_fault_clear(&faults);
    create_test_input(&in, 0xFFFFFFFFFFFFFFFFULL);
    r2 = ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (r1 == AX_OK && r2 == AX_OK) {
        PASS();
    } else {
        FAIL("large sequences should work");
    }
}

static void test_ordering_context_update(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];

    TEST("ordering_context_update");

    ax_admission_ctx_init(&ctx);

    ct_fault_clear(&faults);
    create_test_input(&in, 42);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (ctx.initialized && ctx.last_ledger_seq == 42) {
        PASS();
    } else {
        FAIL("context not updated correctly");
        printf("    initialized=%d, last_seq=%llu\n",
               ctx.initialized, (unsigned long long)ctx.last_ledger_seq);
    }
}

/* ========================================================================
 * Test: Error State After Ordering Violation
 * ======================================================================== */

static void test_ordering_error_state(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];

    TEST("ordering_error_state");

    ax_admission_ctx_init(&ctx);

    /* First admission */
    ct_fault_clear(&faults);
    create_test_input(&in, 10);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Regression attempt */
    ct_fault_clear(&faults);
    create_test_input(&in, 5);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Check error state is set correctly */
    if (obs.completion_state == AX_COMPLETION_ERROR &&
        obs.failure_type == AX_FAILURE_INVALID_OUTPUT) {
        PASS();
    } else {
        FAIL("error state not set correctly");
        printf("    completion=%d, failure=%d\n",
               obs.completion_state, obs.failure_type);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Ordering Tests ===\n\n");

    printf("[Ordering Enforcement]\n");
    test_ordering_first_admission();
    test_ordering_sequential();
    test_ordering_gap_allowed();
    test_ordering_regression_fail();
    test_ordering_duplicate_fail();
    test_ordering_large_sequence();
    test_ordering_context_update();

    printf("\n[Error State]\n");
    test_ordering_error_state();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

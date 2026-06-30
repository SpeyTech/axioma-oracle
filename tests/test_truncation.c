/**
 * @file test_truncation.c
 * @brief Output size and truncation tests for AX:OBS:v1
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-040, SRS-004-SHALL-048
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "axilog/obs.h"
#include "axilog/limits.h"
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

/* Static buffers to avoid malloc */
static char large_output[AX_MAX_OBS_BYTES * 2];
static char output_buf[AX_MAX_OBS_BYTES * 2];

/* ========================================================================
 * Test: Normal Size Output
 * ======================================================================== */

static void test_normal_size_output(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;

    TEST("normal_size_output");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test input";
    in.input_len = 10;
    in.output = "This is a normal sized output.";
    in.output_len = 30;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (result == AX_OK &&
        obs.completion_state == AX_COMPLETION_COMPLETE &&
        obs.output_size == 30) {
        PASS();
    } else {
        FAIL("normal output not handled correctly");
        printf("    result=%d, state=%d, size=%llu\n",
               result, obs.completion_state, (unsigned long long)obs.output_size);
    }
}

static void test_max_boundary_output(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    size_t i;

    TEST("max_boundary_output");

    /* Create output exactly at max size */
    for (i = 0; i < AX_MAX_OUTPUT_BYTES; i++) {
        large_output[i] = (char)('A' + (int)(i % 26));
    }
    large_output[AX_MAX_OUTPUT_BYTES] = '\0';

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test";
    in.input_len = 4;
    in.output = large_output;
    in.output_len = AX_MAX_OUTPUT_BYTES;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (result == AX_OK && obs.completion_state == AX_COMPLETION_COMPLETE) {
        PASS();
    } else {
        FAIL("max boundary output not handled");
        printf("    result=%d, state=%d\n", result, obs.completion_state);
    }
}

/* ========================================================================
 * Test: Oversized Output
 * ======================================================================== */

static void test_oversized_output_truncated(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    size_t i;

    TEST("oversized_output_truncated");

    /* Create output larger than max */
    size_t oversize = AX_MAX_OUTPUT_BYTES + 1000;
    for (i = 0; i < oversize; i++) {
        large_output[i] = 'X';
    }
    large_output[oversize] = '\0';

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test";
    in.input_len = 4;
    in.output = large_output;
    in.output_len = oversize;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Should succeed but mark as TRUNCATED */
    if (obs.completion_state == AX_COMPLETION_TRUNCATED &&
        faults.size == 1) {
        PASS();
    } else {
        FAIL("oversized output should be marked TRUNCATED");
        printf("    result=%d, state=%d, size_fault=%d\n",
               result, obs.completion_state, faults.size);
    }
}

static void test_oversized_records_original_size(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    size_t i;

    TEST("oversized_records_original_size");

    size_t oversize = AX_MAX_OUTPUT_BYTES + 5000;
    for (i = 0; i < oversize; i++) {
        large_output[i] = 'Y';
    }
    large_output[oversize] = '\0';

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test";
    in.input_len = 4;
    in.output = large_output;
    in.output_len = oversize;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* output_size should record original size for audit trail */
    if (obs.output_size == oversize) {
        PASS();
    } else {
        FAIL("original size not recorded");
        printf("    expected=%zu, got=%llu\n",
               oversize, (unsigned long long)obs.output_size);
    }
}

/* ========================================================================
 * Test: Empty Output
 * ======================================================================== */

static void test_empty_output(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;

    TEST("empty_output");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test input";
    in.input_len = 10;
    in.output = "";
    in.output_len = 0;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (result == AX_OK && obs.output_size == 0) {
        PASS();
    } else {
        FAIL("empty output not handled");
    }
}

static void test_null_output(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;

    TEST("null_output");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_ERROR;
    in.failure_type = AX_FAILURE_TIMEOUT;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test input";
    in.input_len = 10;
    in.output = NULL;  /* No output due to error */
    in.output_len = 0;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    if (result == AX_OK && obs.output_size == 0) {
        PASS();
    } else {
        FAIL("null output not handled");
    }
}

/* ========================================================================
 * Test: Size Consistency
 * ======================================================================== */

static void test_output_size_consistency(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;

    TEST("output_size_consistency");

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test-oracle";
    in.model_id = "test-model";
    in.input = "test";
    in.input_len = 4;
    in.output = "Exact length test";
    in.output_len = 17;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);

    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* output_size should match actual output length */
    size_t actual_len = strlen(obs.output);
    if (obs.output_size == actual_len) {
        PASS();
    } else {
        FAIL("size mismatch");
        printf("    recorded=%llu, actual=%zu\n",
               (unsigned long long)obs.output_size, actual_len);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Truncation Tests ===\n\n");

    printf("[Normal Size Output]\n");
    test_normal_size_output();
    test_max_boundary_output();

    printf("\n[Oversized Output]\n");
    test_oversized_output_truncated();
    test_oversized_records_original_size();

    printf("\n[Empty Output]\n");
    test_empty_output();
    test_null_output();

    printf("\n[Size Consistency]\n");
    test_output_size_consistency();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

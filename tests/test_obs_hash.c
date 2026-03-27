/**
 * @file test_obs_hash.c
 * @brief Hash computation tests for AX:OBS:v1
 *
 * Copyright (c) 2026 The Murray Family Innovation Trust
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-009, SRS-004-SHALL-046
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"

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

/* ========================================================================
 * Test: SHA-256 Known Vectors
 * ======================================================================== */

static void test_sha256_empty(void)
{
    uint8_t hash[32];
    char hex[65];

    TEST("sha256_empty");

    /* SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    ax_sha256(hash, (const uint8_t *)"", 0);
    ax_format_hash_hex(hex, sizeof(hex), hash, 32);

    const char *expected = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    if (strcmp(hex, expected) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", hex);
    }
}

static void test_sha256_abc(void)
{
    uint8_t hash[32];
    char hex[65];

    TEST("sha256_abc");

    /* SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    ax_sha256(hash, (const uint8_t *)"abc", 3);
    ax_format_hash_hex(hex, sizeof(hex), hash, 32);

    const char *expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    if (strcmp(hex, expected) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", hex);
    }
}

static void test_sha256_long(void)
{
    uint8_t hash[32];
    char hex[65];

    TEST("sha256_long");

    /* SHA-256("The quick brown fox jumps over the lazy dog") */
    const char *input = "The quick brown fox jumps over the lazy dog";
    ax_sha256(hash, (const uint8_t *)input, strlen(input));
    ax_format_hash_hex(hex, sizeof(hex), hash, 32);

    const char *expected = "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592";

    if (strcmp(hex, expected) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", hex);
    }
}

/* ========================================================================
 * Test: Input Hash Computation
 * ======================================================================== */

static void test_input_hash_deterministic(void)
{
    uint8_t hash1[32], hash2[32];

    TEST("input_hash_deterministic");

    const char *prompt = "What is the meaning of life?";
    ax_compute_input_hash(hash1, prompt, strlen(prompt));
    ax_compute_input_hash(hash2, prompt, strlen(prompt));

    if (memcmp(hash1, hash2, 32) == 0) {
        PASS();
    } else {
        FAIL("non-deterministic hash");
    }
}

static void test_input_hash_different(void)
{
    uint8_t hash1[32], hash2[32];

    TEST("input_hash_different");

    const char *prompt1 = "Question A";
    const char *prompt2 = "Question B";
    ax_compute_input_hash(hash1, prompt1, strlen(prompt1));
    ax_compute_input_hash(hash2, prompt2, strlen(prompt2));

    if (memcmp(hash1, hash2, 32) != 0) {
        PASS();
    } else {
        FAIL("different inputs produced same hash");
    }
}

/* ========================================================================
 * Test: Observation Hash Computation
 * ======================================================================== */

static void test_obs_hash_computation(void)
{
    ax_obs_record_t obs;
    int result;

    TEST("obs_hash_computation");

    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    memset(obs.input_hash, 0x42, AX_HASH_SIZE);
    obs.ledger_seq = 1;
    obs.model_id = "test-model";
    obs.oracle_id = "test-oracle";
    obs.output = "test output";
    obs.output_size = 11;
    ax_oracle_params_init_null(&obs.params);
    obs.schema_version = AX_OBS_SCHEMA_VERSION;

    result = ax_obs_compute_hash(&obs);

    if (result == AX_OK && obs.obs_hash[0] != '\0' && strlen(obs.obs_hash) == 64) {
        PASS();
    } else {
        FAIL("hash computation failed");
        printf("    Result: %d, Hash: %s\n", result, obs.obs_hash);
    }
}

static void test_obs_hash_deterministic(void)
{
    ax_obs_record_t obs1, obs2;

    TEST("obs_hash_deterministic");

    /* Create two identical observations */
    ax_obs_init(&obs1);
    obs1.completion_state = AX_COMPLETION_COMPLETE;
    obs1.failure_type = AX_FAILURE_NULL;
    memset(obs1.input_hash, 0xAA, AX_HASH_SIZE);
    obs1.ledger_seq = 100;
    obs1.model_id = "gpt-4";
    obs1.oracle_id = "azure-prod";
    obs1.output = "The answer is 42.";
    obs1.output_size = 17;
    obs1.params.max_tokens = 4096;
    obs1.params.seed = AX_PARAMS_NULL_INT64;
    obs1.params.temperature = 45875;
    obs1.params.top_p = 58982;
    obs1.schema_version = AX_OBS_SCHEMA_VERSION;

    obs2 = obs1;  /* Copy */
    obs2.obs_hash[0] = '\0';

    ax_obs_compute_hash(&obs1);
    ax_obs_compute_hash(&obs2);

    if (strcmp(obs1.obs_hash, obs2.obs_hash) == 0) {
        PASS();
    } else {
        FAIL("non-deterministic obs_hash");
        printf("    Hash1: %s\n", obs1.obs_hash);
        printf("    Hash2: %s\n", obs2.obs_hash);
    }
}

static void test_obs_hash_changes_with_content(void)
{
    ax_obs_record_t obs1, obs2;

    TEST("obs_hash_changes_with_content");

    /* Create two observations differing only in output */
    ax_obs_init(&obs1);
    obs1.completion_state = AX_COMPLETION_COMPLETE;
    obs1.failure_type = AX_FAILURE_NULL;
    memset(obs1.input_hash, 0xBB, AX_HASH_SIZE);
    obs1.ledger_seq = 50;
    obs1.model_id = "test-model";
    obs1.oracle_id = "test-oracle";
    obs1.output = "Output A";
    obs1.output_size = 8;
    ax_oracle_params_init_null(&obs1.params);
    obs1.schema_version = AX_OBS_SCHEMA_VERSION;

    obs2 = obs1;
    obs2.output = "Output B";  /* Different output */
    obs2.obs_hash[0] = '\0';

    ax_obs_compute_hash(&obs1);
    ax_obs_compute_hash(&obs2);

    if (strcmp(obs1.obs_hash, obs2.obs_hash) != 0) {
        PASS();
    } else {
        FAIL("different content produced same hash");
    }
}

/* ========================================================================
 * Test: Hash Stability Across Calls
 * ======================================================================== */

static void test_obs_hash_stability(void)
{
    ax_obs_record_t obs;
    char hash1[65], hash2[65], hash3[65];

    TEST("obs_hash_stability");

    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    memset(obs.input_hash, 0xCC, AX_HASH_SIZE);
    obs.ledger_seq = 999;
    obs.model_id = "stable-model";
    obs.oracle_id = "stable-oracle";
    obs.output = "Stable output for testing hash reproducibility.";
    obs.output_size = 47;
    obs.params.max_tokens = 1000;
    obs.params.seed = 12345;
    obs.params.temperature = 32768;  /* 0.5 */
    obs.params.top_p = 65536;        /* 1.0 */
    obs.schema_version = AX_OBS_SCHEMA_VERSION;

    /* Compute hash three times */
    ax_obs_compute_hash(&obs);
    strcpy(hash1, obs.obs_hash);

    obs.obs_hash[0] = '\0';
    ax_obs_compute_hash(&obs);
    strcpy(hash2, obs.obs_hash);

    obs.obs_hash[0] = '\0';
    ax_obs_compute_hash(&obs);
    strcpy(hash3, obs.obs_hash);

    if (strcmp(hash1, hash2) == 0 && strcmp(hash2, hash3) == 0) {
        PASS();
    } else {
        FAIL("hash not stable across calls");
        printf("    Hash1: %s\n", hash1);
        printf("    Hash2: %s\n", hash2);
        printf("    Hash3: %s\n", hash3);
    }
}

/* ========================================================================
 * Test: Multi-block SHA-256
 * ======================================================================== */

static void test_sha256_multiblock(void)
{
    uint8_t hash[32];
    char hex[65];

    TEST("sha256_multiblock");

    /* 64 bytes exactly = one full block */
    const char *input64 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01";
    ax_sha256(hash, (const uint8_t *)input64, 64);
    ax_format_hash_hex(hex, sizeof(hex), hash, 32);

    /* Verify it produces 64 hex chars consistently */
    if (strlen(hex) == 64) {
        PASS();
    } else {
        FAIL("multiblock hash length incorrect");
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Hash Tests ===\n\n");

    printf("[SHA-256 Known Vectors]\n");
    test_sha256_empty();
    test_sha256_abc();
    test_sha256_long();

    printf("\n[Input Hash]\n");
    test_input_hash_deterministic();
    test_input_hash_different();

    printf("\n[Observation Hash]\n");
    test_obs_hash_computation();
    test_obs_hash_deterministic();
    test_obs_hash_changes_with_content();
    test_obs_hash_stability();

    printf("\n[Multi-block SHA-256]\n");
    test_sha256_multiblock();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

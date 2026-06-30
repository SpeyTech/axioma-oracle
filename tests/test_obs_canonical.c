/**
 * @file test_obs_canonical.c
 * @brief Canonicalisation tests for AX:OBS:v1
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-006, SRS-004-SHALL-036, SRS-004-SHALL-045
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"
#include "axilog/hash.h"

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
 * Test: Params Canonicalisation
 * ======================================================================== */

static void test_params_canonical_all_null(void)
{
    ax_oracle_params_t params;
    char buf[256];
    int len;

    TEST("params_canonical_all_null");

    ax_oracle_params_init_null(&params);
    len = ax_params_canonicalise(buf, sizeof(buf), &params);

    const char *expected = "{\"max_tokens\":null,\"seed\":null,\"temperature\":null,\"top_p\":null}";

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("output mismatch");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

static void test_params_canonical_with_values(void)
{
    ax_oracle_params_t params;
    char buf[256];
    int len;

    TEST("params_canonical_with_values");

    params.max_tokens = 4096;
    params.seed = AX_PARAMS_NULL_INT64;  /* null */
    params.temperature = 45875;  /* 0.7 in Q16.16 */
    params.top_p = 58982;        /* 0.9 in Q16.16 */

    len = ax_params_canonicalise(buf, sizeof(buf), &params);

    const char *expected = "{\"max_tokens\":4096,\"seed\":null,\"temperature\":45875,\"top_p\":58982}";

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("output mismatch");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

/* ========================================================================
 * Test: String Escaping
 * ======================================================================== */

static void test_string_escape_basic(void)
{
    char buf[256];
    int len;

    TEST("string_escape_basic");

    const char *input = "Hello, World!";
    len = ax_string_escape(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, input) == 0) {
        PASS();
    } else {
        FAIL("should not escape normal text");
    }
}

static void test_string_escape_quotes(void)
{
    char buf[256];
    int len;

    TEST("string_escape_quotes");

    const char *input = "He said \"Hello\"";
    const char *expected = "He said \\\"Hello\\\"";

    len = ax_string_escape(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("quote escaping failed");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

static void test_string_escape_backslash(void)
{
    char buf[256];
    int len;

    TEST("string_escape_backslash");

    const char *input = "path\\to\\file";
    const char *expected = "path\\\\to\\\\file";

    len = ax_string_escape(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("backslash escaping failed");
    }
}

static void test_string_escape_utf8_direct(void)
{
    char buf[256];
    int len;

    TEST("string_escape_utf8_direct");

    /* UTF-8 should NOT be escaped - direct encoding per SHALL-045 */
    const char *input = "Héllo wörld";
    const char *expected = "Héllo wörld";  /* Same - no \uXXXX */

    len = ax_string_escape(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("UTF-8 should use direct encoding");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

static void test_string_escape_control_char(void)
{
    char buf[256];
    int len;

    TEST("string_escape_control_char");

    /* Tab (0x09) should be escaped as \u0009 */
    const char input[] = "line1\tline2";
    const char *expected = "line1\\u0009line2";

    len = ax_string_escape(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("control char escaping failed");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

/* ========================================================================
 * Test: Full Observation Canonicalisation
 * ======================================================================== */

static void test_obs_canonical_structure(void)
{
    ax_obs_record_t obs;
    char buf[4096];
    int len;

    TEST("obs_canonical_structure");

    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    memset(obs.input_hash, 0xAB, AX_HASH_SIZE);  /* Test pattern */
    obs.ledger_seq = 42;
    obs.model_id = "test-model";
    obs.obs_hash[0] = '\0';  /* Empty for canonicalisation */
    obs.oracle_id = "test-oracle";
    obs.output = "test output";
    obs.output_size = 11;
    ax_oracle_params_init_null(&obs.params);
    obs.schema_version = AX_OBS_SCHEMA_VERSION;

    len = ax_obs_canonicalise(buf, sizeof(buf), &obs, 0);

    if (len < 0) {
        FAIL("canonicalisation returned error");
        return;
    }

    /* Verify lexicographic key order */
    const char *keys[] = {
        "\"completion_state\"",
        "\"failure_type\"",
        "\"input_hash\"",
        "\"ledger_seq\"",
        "\"model_id\"",
        "\"obs_hash\"",
        "\"oracle_id\"",
        "\"output\"",
        "\"output_size\"",
        "\"params\"",
        "\"schema_version\""
    };

    char *prev_pos = buf;
    int order_ok = 1;

    for (int i = 0; i < 11; i++) {
        char *pos = strstr(prev_pos, keys[i]);
        if (pos == NULL) {
            printf("    Missing key: %s\n", keys[i]);
            order_ok = 0;
            break;
        }
        if (pos < prev_pos) {
            printf("    Key out of order: %s\n", keys[i]);
            order_ok = 0;
            break;
        }
        prev_pos = pos + 1;
    }

    if (order_ok) {
        PASS();
    } else {
        FAIL("key ordering incorrect");
        printf("    Output: %s\n", buf);
    }
}

static void test_obs_canonical_deterministic(void)
{
    ax_obs_record_t obs;
    char buf1[4096];
    char buf2[4096];
    int len1, len2;

    TEST("obs_canonical_deterministic");

    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    memset(obs.input_hash, 0x12, AX_HASH_SIZE);
    obs.ledger_seq = 100;
    obs.model_id = "gpt-4";
    obs.oracle_id = "azure-prod";
    obs.output = "The answer is 42.";
    obs.output_size = 17;
    obs.params.max_tokens = 4096;
    obs.params.seed = AX_PARAMS_NULL_INT64;
    obs.params.temperature = 45875;
    obs.params.top_p = 58982;
    obs.schema_version = AX_OBS_SCHEMA_VERSION;

    /* Canonicalise twice */
    len1 = ax_obs_canonicalise(buf1, sizeof(buf1), &obs, 0);
    len2 = ax_obs_canonicalise(buf2, sizeof(buf2), &obs, 0);

    if (len1 > 0 && len1 == len2 && strcmp(buf1, buf2) == 0) {
        PASS();
    } else {
        FAIL("non-deterministic canonicalisation");
    }
}

/* ========================================================================
 * Test: Number Formatting
 * ======================================================================== */

static void test_format_int64_positive(void)
{
    char buf[32];
    int len;

    TEST("format_int64_positive");

    len = ax_format_int64(buf, sizeof(buf), 12345);

    if (len == 5 && strcmp(buf, "12345") == 0) {
        PASS();
    } else {
        FAIL("incorrect formatting");
    }
}

static void test_format_int64_negative(void)
{
    char buf[32];
    int len;

    TEST("format_int64_negative");

    len = ax_format_int64(buf, sizeof(buf), -12345);

    if (len == 6 && strcmp(buf, "-12345") == 0) {
        PASS();
    } else {
        FAIL("incorrect formatting");
    }
}

static void test_format_int64_zero(void)
{
    char buf[32];
    int len;

    TEST("format_int64_zero");

    len = ax_format_int64(buf, sizeof(buf), 0);

    if (len == 1 && strcmp(buf, "0") == 0) {
        PASS();
    } else {
        FAIL("incorrect formatting");
    }
}

static void test_format_uint64_large(void)
{
    char buf[32];
    int len;

    TEST("format_uint64_large");

    len = ax_format_uint64(buf, sizeof(buf), 18446744073709551615ULL);

    if (len == 20 && strcmp(buf, "18446744073709551615") == 0) {
        PASS();
    } else {
        FAIL("incorrect formatting");
        printf("    Got: %s\n", buf);
    }
}

/* ========================================================================
 * Test: Hash Hex Formatting
 * ======================================================================== */

static void test_format_hash_hex(void)
{
    uint8_t hash[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    char buf[16];
    int len;

    TEST("format_hash_hex");

    len = ax_format_hash_hex(buf, sizeof(buf), hash, 4);

    if (len == 8 && strcmp(buf, "deadbeef") == 0) {
        PASS();
    } else {
        FAIL("incorrect hex formatting");
        printf("    Got: %s\n", buf);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Canonicalisation Tests ===\n\n");

    printf("[Params Canonicalisation]\n");
    test_params_canonical_all_null();
    test_params_canonical_with_values();

    printf("\n[String Escaping]\n");
    test_string_escape_basic();
    test_string_escape_quotes();
    test_string_escape_backslash();
    test_string_escape_utf8_direct();
    test_string_escape_control_char();

    printf("\n[Observation Canonicalisation]\n");
    test_obs_canonical_structure();
    test_obs_canonical_deterministic();

    printf("\n[Number Formatting]\n");
    test_format_int64_positive();
    test_format_int64_negative();
    test_format_int64_zero();
    test_format_uint64_large();

    printf("\n[Hash Formatting]\n");
    test_format_hash_hex();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

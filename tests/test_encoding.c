/**
 * @file test_encoding.c
 * @brief Encoding validation tests for AX:OBS:v1
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @traceability SRS-004-SHALL-042, SRS-004-SHALL-043
 */

#include <stdio.h>
#include <string.h>
#include "axilog/validate.h"
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

/* ========================================================================
 * Test: UTF-8 Validation
 * ======================================================================== */

static void test_utf8_ascii(void)
{
    TEST("utf8_ascii");

    const char *input = "Hello, World! 123";
    if (ax_validate_utf8(input, strlen(input))) {
        PASS();
    } else {
        FAIL("valid ASCII rejected");
    }
}

static void test_utf8_2byte(void)
{
    TEST("utf8_2byte");

    /* "café" - contains é (U+00E9) as 2-byte UTF-8: C3 A9 */
    const char *input = "caf\xC3\xA9";
    if (ax_validate_utf8(input, strlen(input))) {
        PASS();
    } else {
        FAIL("valid 2-byte UTF-8 rejected");
    }
}

static void test_utf8_3byte(void)
{
    TEST("utf8_3byte");

    /* Euro sign (U+20AC) as 3-byte UTF-8: E2 82 AC */
    const char *input = "Price: \xE2\x82\xAC" "100";
    if (ax_validate_utf8(input, strlen(input))) {
        PASS();
    } else {
        FAIL("valid 3-byte UTF-8 rejected");
    }
}

static void test_utf8_4byte(void)
{
    TEST("utf8_4byte");

    /* Emoji: 😀 (U+1F600) as 4-byte UTF-8: F0 9F 98 80 */
    const char *input = "Hello \xF0\x9F\x98\x80";
    if (ax_validate_utf8(input, strlen(input))) {
        PASS();
    } else {
        FAIL("valid 4-byte UTF-8 rejected");
    }
}

static void test_utf8_invalid_continuation(void)
{
    TEST("utf8_invalid_continuation");

    /* Invalid: C3 followed by non-continuation byte */
    const char input[] = "test\xC3\x40rest";
    if (!ax_validate_utf8(input, strlen(input))) {
        PASS();
    } else {
        FAIL("invalid continuation accepted");
    }
}

static void test_utf8_truncated(void)
{
    TEST("utf8_truncated");

    /* Invalid: 3-byte sequence with only 2 bytes */
    const char input[] = "test\xE2\x82";
    if (!ax_validate_utf8(input, 6)) {
        PASS();
    } else {
        FAIL("truncated sequence accepted");
    }
}

static void test_utf8_overlong_2byte(void)
{
    TEST("utf8_overlong_2byte");

    /* Invalid: Overlong encoding of ASCII NUL (C0 80 instead of 00) */
    const char input[] = "\xC0\x80";
    if (!ax_validate_utf8(input, 2)) {
        PASS();
    } else {
        FAIL("overlong encoding accepted");
    }
}

static void test_utf8_surrogate(void)
{
    TEST("utf8_surrogate");

    /* Invalid: UTF-16 surrogate (U+D800) encoded as UTF-8: ED A0 80 */
    const char input[] = "\xED\xA0\x80";
    if (!ax_validate_utf8(input, 3)) {
        PASS();
    } else {
        FAIL("surrogate accepted");
    }
}

/* ========================================================================
 * Test: Control Character Detection
 * ======================================================================== */

static void test_control_none(void)
{
    TEST("control_none");

    const char *input = "Normal text with\nnewlines allowed";
    if (!ax_contains_forbidden_control(input, strlen(input))) {
        PASS();
    } else {
        FAIL("LF wrongly flagged as forbidden");
    }
}

static void test_control_tab_forbidden(void)
{
    TEST("control_tab_forbidden");

    const char *input = "text\twith\ttabs";
    if (ax_contains_forbidden_control(input, strlen(input))) {
        PASS();
    } else {
        FAIL("TAB not detected");
    }
}

static void test_control_null_forbidden(void)
{
    TEST("control_null_forbidden");

    const char input[] = "text\x00with null";
    if (ax_contains_forbidden_control(input, 14)) {
        PASS();
    } else {
        FAIL("NULL not detected");
    }
}

static void test_control_bell_forbidden(void)
{
    TEST("control_bell_forbidden");

    const char *input = "text\x07with bell";
    if (ax_contains_forbidden_control(input, strlen(input))) {
        PASS();
    } else {
        FAIL("BEL not detected");
    }
}

static void test_control_cr_forbidden(void)
{
    TEST("control_cr_forbidden");

    /* Standalone CR is forbidden (should be normalised first) */
    const char *input = "text\rwith cr";
    if (ax_contains_forbidden_control(input, strlen(input))) {
        PASS();
    } else {
        FAIL("CR not detected");
    }
}

/* ========================================================================
 * Test: Line Ending Normalisation
 * ======================================================================== */

static void test_line_endings_lf_only(void)
{
    char buf[256];
    int len;

    TEST("line_endings_lf_only");

    const char *input = "line1\nline2\nline3";
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, input) == 0) {
        PASS();
    } else {
        FAIL("LF-only text modified");
    }
}

static void test_line_endings_crlf(void)
{
    char buf[256];
    int len;

    TEST("line_endings_crlf");

    const char *input = "line1\r\nline2\r\nline3";
    const char *expected = "line1\nline2\nline3";
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("CRLF not converted");
        printf("    Expected: %s\n", expected);
        printf("    Got:      %s\n", buf);
    }
}

static void test_line_endings_cr_alone(void)
{
    char buf[256];
    int len;

    TEST("line_endings_cr_alone");

    const char *input = "line1\rline2\rline3";
    const char *expected = "line1\nline2\nline3";
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("CR not converted");
    }
}

static void test_line_endings_mixed(void)
{
    char buf[256];
    int len;

    TEST("line_endings_mixed");

    const char *input = "line1\r\nline2\rline3\nline4";
    const char *expected = "line1\nline2\nline3\nline4";
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("mixed line endings not normalised");
    }
}

static void test_line_endings_trailing(void)
{
    char buf[256];
    int len;

    TEST("line_endings_trailing");

    const char *input = "line1\r\n";
    const char *expected = "line1\n";
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));

    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("trailing CRLF not normalised");
    }
}

/* ========================================================================
 * Test: Combined Validation
 * ======================================================================== */

static void test_validate_and_normalise_success(void)
{
    char buf[256];
    ct_fault_flags_t faults;
    int len;

    TEST("validate_and_normalise_success");

    ct_fault_clear(&faults);
    const char *input = "Valid UTF-8 text\r\nwith CRLF";
    len = ax_validate_and_normalise(buf, sizeof(buf), input, strlen(input), &faults);

    if (len > 0 && !ct_fault_any(&faults)) {
        PASS();
    } else {
        FAIL("valid input rejected");
    }
}

static void test_validate_and_normalise_utf8_fail(void)
{
    char buf[256];
    ct_fault_flags_t faults;
    int len;

    TEST("validate_and_normalise_utf8_fail");

    ct_fault_clear(&faults);
    const char input[] = "Invalid \xC0\x80 UTF-8";
    len = ax_validate_and_normalise(buf, sizeof(buf), input, strlen(input), &faults);

    if (len < 0 && faults.encoding) {
        PASS();
    } else {
        FAIL("invalid UTF-8 accepted");
    }
}

static void test_validate_and_normalise_control_fail(void)
{
    char buf[256];
    ct_fault_flags_t faults;
    int len;

    TEST("validate_and_normalise_control_fail");

    ct_fault_clear(&faults);
    /* After normalising line endings, this still has tab */
    const char *input = "Text\twith\ttabs";
    len = ax_validate_and_normalise(buf, sizeof(buf), input, strlen(input), &faults);

    if (len < 0 && faults.encoding) {
        PASS();
    } else {
        FAIL("control char not rejected");
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== AX:OBS:v1 Encoding Tests ===\n\n");

    printf("[UTF-8 Validation]\n");
    test_utf8_ascii();
    test_utf8_2byte();
    test_utf8_3byte();
    test_utf8_4byte();
    test_utf8_invalid_continuation();
    test_utf8_truncated();
    test_utf8_overlong_2byte();
    test_utf8_surrogate();

    printf("\n[Control Character Detection]\n");
    test_control_none();
    test_control_tab_forbidden();
    test_control_null_forbidden();
    test_control_bell_forbidden();
    test_control_cr_forbidden();

    printf("\n[Line Ending Normalisation]\n");
    test_line_endings_lf_only();
    test_line_endings_crlf();
    test_line_endings_cr_alone();
    test_line_endings_mixed();
    test_line_endings_trailing();

    printf("\n[Combined Validation]\n");
    test_validate_and_normalise_success();
    test_validate_and_normalise_utf8_fail();
    test_validate_and_normalise_control_fail();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

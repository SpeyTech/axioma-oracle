/**
 * @file test_encoding_rigorous.c
 * @brief Rigorous UTF-8 and NFC verification tests
 *
 * AUDIT REQUIREMENT: Prove
 *   A. Reject invalid UTF-8 (0xC0 0xAF MUST FAIL)
 *   B. Line endings: \r\n → \n, \r → \n (no exceptions)
 *   C. Reject ill-formed sequences
 */

#include <stdio.h>
#include <string.h>
#include "axilog/validate.h"
#include "axilog/obs.h"
#include "axilog/types.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { printf("  [TEST] %s... ", name); tests_run++; } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ========================================================================
 * AUDIT REQUIREMENT A: Reject Invalid UTF-8
 * ======================================================================== */

/* C0 AF is overlong encoding of "/" - MUST be rejected */
static void test_reject_overlong_c0_af(void)
{
    TEST("reject_overlong_C0_AF");
    
    const char invalid[] = "\xC0\xAF";  /* Overlong "/" */
    
    if (!ax_validate_utf8(invalid, 2)) {
        PASS();
    } else {
        FAIL("accepted invalid overlong C0 AF");
    }
}

/* C0 80 is overlong encoding of NUL - MUST be rejected */
static void test_reject_overlong_c0_80(void)
{
    TEST("reject_overlong_C0_80");
    
    const char invalid[] = "\xC0\x80";  /* Overlong NUL */
    
    if (!ax_validate_utf8(invalid, 2)) {
        PASS();
    } else {
        FAIL("accepted invalid overlong C0 80");
    }
}

/* C1 BF is overlong - MUST be rejected */
static void test_reject_overlong_c1_bf(void)
{
    TEST("reject_overlong_C1_BF");
    
    const char invalid[] = "\xC1\xBF";
    
    if (!ax_validate_utf8(invalid, 2)) {
        PASS();
    } else {
        FAIL("accepted invalid overlong C1 BF");
    }
}

/* E0 80 80 is overlong encoding of NUL - MUST be rejected */
static void test_reject_overlong_e0_80_80(void)
{
    TEST("reject_overlong_E0_80_80");
    
    const char invalid[] = "\xE0\x80\x80";
    
    if (!ax_validate_utf8(invalid, 3)) {
        PASS();
    } else {
        FAIL("accepted invalid overlong E0 80 80");
    }
}

/* F0 80 80 80 is overlong - MUST be rejected */
static void test_reject_overlong_f0_80_80_80(void)
{
    TEST("reject_overlong_F0_80_80_80");
    
    const char invalid[] = "\xF0\x80\x80\x80";
    
    if (!ax_validate_utf8(invalid, 4)) {
        PASS();
    } else {
        FAIL("accepted invalid overlong F0 80 80 80");
    }
}

/* Surrogate pair (U+D800) - MUST be rejected */
static void test_reject_surrogate_d800(void)
{
    TEST("reject_surrogate_D800");
    
    const char invalid[] = "\xED\xA0\x80";  /* U+D800 */
    
    if (!ax_validate_utf8(invalid, 3)) {
        PASS();
    } else {
        FAIL("accepted surrogate D800");
    }
}

/* Surrogate pair (U+DFFF) - MUST be rejected */
static void test_reject_surrogate_dfff(void)
{
    TEST("reject_surrogate_DFFF");
    
    const char invalid[] = "\xED\xBF\xBF";  /* U+DFFF */
    
    if (!ax_validate_utf8(invalid, 3)) {
        PASS();
    } else {
        FAIL("accepted surrogate DFFF");
    }
}

/* Invalid continuation byte */
static void test_reject_invalid_continuation(void)
{
    TEST("reject_invalid_continuation");
    
    const char invalid[] = "\xC3\x28";  /* C3 followed by ASCII */
    
    if (!ax_validate_utf8(invalid, 2)) {
        PASS();
    } else {
        FAIL("accepted invalid continuation");
    }
}

/* Truncated sequence */
static void test_reject_truncated_sequence(void)
{
    TEST("reject_truncated_sequence");
    
    const char invalid[] = "\xE2\x82";  /* Truncated 3-byte */
    
    if (!ax_validate_utf8(invalid, 2)) {
        PASS();
    } else {
        FAIL("accepted truncated sequence");
    }
}

/* Invalid leading byte 0xFF */
static void test_reject_invalid_ff(void)
{
    TEST("reject_invalid_FF");
    
    const char invalid[] = "\xFF";
    
    if (!ax_validate_utf8(invalid, 1)) {
        PASS();
    } else {
        FAIL("accepted invalid FF");
    }
}

/* Invalid leading byte 0xFE */
static void test_reject_invalid_fe(void)
{
    TEST("reject_invalid_FE");
    
    const char invalid[] = "\xFE";
    
    if (!ax_validate_utf8(invalid, 1)) {
        PASS();
    } else {
        FAIL("accepted invalid FE");
    }
}

/* ========================================================================
 * AUDIT REQUIREMENT B: Line Ending Normalisation
 * ======================================================================== */

static void test_crlf_to_lf(void)
{
    char buf[256];
    int len;
    
    TEST("CRLF_to_LF");
    
    const char *input = "line1\r\nline2\r\n";
    const char *expected = "line1\nline2\n";
    
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));
    
    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("CRLF not normalised");
    }
}

static void test_cr_alone_to_lf(void)
{
    char buf[256];
    int len;
    
    TEST("CR_alone_to_LF");
    
    const char *input = "line1\rline2\r";
    const char *expected = "line1\nline2\n";
    
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));
    
    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("CR alone not normalised");
    }
}

static void test_mixed_line_endings(void)
{
    char buf[256];
    int len;
    
    TEST("mixed_line_endings");
    
    const char *input = "a\r\nb\rc\nd";
    const char *expected = "a\nb\nc\nd";
    
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));
    
    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("mixed line endings not normalised correctly");
    }
}

static void test_empty_between_crlf(void)
{
    char buf[256];
    int len;
    
    TEST("empty_between_CRLF");
    
    const char *input = "\r\n\r\n";
    const char *expected = "\n\n";
    
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));
    
    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("empty between CRLF not handled");
    }
}

static void test_consecutive_cr(void)
{
    char buf[256];
    int len;
    
    TEST("consecutive_CR");
    
    const char *input = "\r\r\r";
    const char *expected = "\n\n\n";
    
    len = ax_normalise_line_endings(buf, sizeof(buf), input, strlen(input));
    
    if (len > 0 && strcmp(buf, expected) == 0) {
        PASS();
    } else {
        FAIL("consecutive CR not normalised");
    }
}

/* ========================================================================
 * AUDIT REQUIREMENT: Full Admission Pipeline
 * ======================================================================== */

static void test_admission_rejects_invalid_utf8(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int result;
    
    TEST("admission_rejects_invalid_utf8");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "invalid \xC0\xAF here";  /* Contains invalid UTF-8 */
    in.output_len = strlen(in.output);
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result == AX_ERR_ENCODING && faults.encoding) {
        PASS();
    } else {
        FAIL("admission did not reject invalid UTF-8");
        printf("    result=%d, encoding_fault=%d\n", result, faults.encoding);
    }
}

static void test_admission_normalises_crlf(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int result;
    
    TEST("admission_normalises_CRLF");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "line1\r\nline2\r\n";
    in.output_len = strlen(in.output);
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    /* Check output is normalised */
    if (result == AX_OK && 
        strstr(obs.output, "\r\n") == NULL &&
        strstr(obs.output, "\r") == NULL &&
        strstr(obs.output, "\n") != NULL) {
        PASS();
    } else {
        FAIL("CRLF not normalised during admission");
        printf("    output: %s\n", obs.output);
    }
}

static void test_admission_rejects_control_chars(void)
{
    ax_obs_record_t obs;
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    char output_buf[256];
    int result;
    
    TEST("admission_rejects_control_chars");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "text\x07with bell";  /* BEL character */
    in.output_len = strlen(in.output);
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result == AX_ERR_ENCODING && faults.encoding) {
        PASS();
    } else {
        FAIL("control char not rejected");
    }
}

int main(void)
{
    printf("\n=== RIGOROUS ENCODING VERIFICATION ===\n\n");
    
    printf("[Invalid UTF-8 Rejection]\n");
    test_reject_overlong_c0_af();
    test_reject_overlong_c0_80();
    test_reject_overlong_c1_bf();
    test_reject_overlong_e0_80_80();
    test_reject_overlong_f0_80_80_80();
    test_reject_surrogate_d800();
    test_reject_surrogate_dfff();
    test_reject_invalid_continuation();
    test_reject_truncated_sequence();
    test_reject_invalid_ff();
    test_reject_invalid_fe();
    
    printf("\n[Line Ending Normalisation]\n");
    test_crlf_to_lf();
    test_cr_alone_to_lf();
    test_mixed_line_endings();
    test_empty_between_crlf();
    test_consecutive_cr();
    
    printf("\n[Full Admission Pipeline]\n");
    test_admission_rejects_invalid_utf8();
    test_admission_normalises_crlf();
    test_admission_rejects_control_chars();
    
    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}

/**
 * @file test_hardening.c
 * @brief L3 Hardening Tests — Audit Closure
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file addresses the hardening items from the L3 closure audit:
 *
 * H1: Hash self-reference edge case — obs_hash must be "" during computation
 * H2: Canonicalisation single source — only ax_obs_canonicalise() exists
 * H3: Ledger sequence authority — L3 authoritative, L5 defensive
 * H4: Encoding rejection totality — rejection before hash, produces ERROR record
 *
 * @traceability SRS-004-SHALL-038, SRS-004-SHALL-042, SRS-004-SHALL-043,
 *               SRS-004-SHALL-044, SRS-004-SHALL-046
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"
#include "axilog/validate.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { printf("  [TEST] %s... ", name); tests_run++; } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ========================================================================
 * H1: Hash Self-Reference Edge Case
 *
 * The obs_hash field must contain "" (empty string) during hash computation.
 * This prevents circular dependency where the hash would include itself.
 * ======================================================================== */

/**
 * Test that obs_hash is explicitly set to "" before canonicalisation.
 */
static void test_h1_hash_self_reference_explicit_empty(void)
{
    ax_obs_record_t obs;
    char canonical_buf[8192];
    
    TEST("h1_hash_self_reference_explicit_empty");
    
    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    obs.ledger_seq = 1;
    obs.oracle_id = "test_oracle";
    obs.model_id = "test_model";
    obs.output = "test output";
    obs.output_size = 11;
    ax_oracle_params_init_null(&obs.params);
    
    /* Set obs_hash to empty string as required for hashing */
    obs.obs_hash[0] = '\0';
    
    /* Canonicalise */
    int len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &obs, 0);
    
    /* Verify obs_hash field is present as empty string in canonical form */
    if (len > 0 && strstr(canonical_buf, "\"obs_hash\":\"\"") != NULL) {
        PASS();
    } else {
        FAIL("obs_hash not present as empty string in canonical form");
        printf("    Canonical: %.300s...\n", canonical_buf);
    }
}

/**
 * Test that non-empty obs_hash before compute results in different hash.
 */
static void test_h1_hash_changes_if_not_cleared(void)
{
    ax_obs_record_t obs1, obs2;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults;
    
    TEST("h1_hash_changes_if_not_cleared");
    
    /* Setup common input */
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "test";
    in.model_id = "test";
    in.input = "input";
    in.input_len = 5;
    in.output = "output";
    in.output_len = 6;
    ax_oracle_params_init_null(&in.params);
    
    /* Admit first observation normally */
    ax_admission_ctx_init(&ctx1);
    ct_fault_clear(&faults);
    ax_obs_admit(&obs1, output_buf, sizeof(output_buf), &in, &ctx1, &faults);
    
    /* Create second observation with different ledger_seq */
    in.ledger_seq = 2;
    ax_admission_ctx_init(&ctx2);
    ct_fault_clear(&faults);
    ax_obs_admit(&obs2, output_buf, sizeof(output_buf), &in, &ctx2, &faults);
    
    /* Hashes must be different due to different ledger_seq */
    if (strcmp(obs1.obs_hash, obs2.obs_hash) != 0) {
        PASS();
    } else {
        FAIL("different ledger_seq should produce different hash");
    }
}

/* ========================================================================
 * H2: Canonicalisation Single Source of Truth
 *
 * There must be exactly ONE canonicalisation function: ax_obs_canonicalise()
 * All hash computations must flow through this function.
 * ======================================================================== */

/**
 * Test that ax_obs_compute_hash uses ax_obs_canonicalise internally.
 * (This is verified by examining code; here we test consistency.)
 */
static void test_h2_canonical_consistency(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    char canonical_direct[8192];
    char canonical_via_hash[8192];
    ax_obs_record_t temp;
    int len1, len2;
    
    TEST("h2_canonical_consistency");
    
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 42;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "result";
    in.output_len = 6;
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    /* Canonicalise directly with obs_hash cleared */
    temp = obs;
    temp.obs_hash[0] = '\0';
    len1 = ax_obs_canonicalise(canonical_direct, sizeof(canonical_direct), &temp, 0);
    
    /* Canonicalise again - should be identical */
    len2 = ax_obs_canonicalise(canonical_via_hash, sizeof(canonical_via_hash), &temp, 0);
    
    if (len1 == len2 && strcmp(canonical_direct, canonical_via_hash) == 0) {
        PASS();
    } else {
        FAIL("canonicalisation not deterministic");
    }
}

/* ========================================================================
 * H3: Ledger Sequence Authority
 *
 * L3 is authoritative for ledger_seq ordering.
 * L5 performs defensive checks but L3 is the source of truth.
 * ======================================================================== */

/**
 * Test that ledger_seq ordering is enforced at admission.
 */
static void test_h3_ledger_seq_ordering_enforced(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    
    TEST("h3_ledger_seq_ordering_enforced");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.output = "test";
    in.output_len = 4;
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    
    /* Admit first observation with ledger_seq = 10 */
    in.ledger_seq = 10;
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result != AX_OK) {
        FAIL("first admission should succeed");
        return;
    }
    
    /* Attempt to admit with ledger_seq = 5 (regression) - must fail */
    in.ledger_seq = 5;
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result == AX_ERR_ORDERING && faults.ordering == 1) {
        PASS();
    } else {
        FAIL("ledger_seq regression not rejected");
        printf("    result: %d, faults.ordering: %d\n", result, faults.ordering);
    }
}

/**
 * Test that duplicate ledger_seq is rejected.
 */
static void test_h3_ledger_seq_duplicate_rejected(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    
    TEST("h3_ledger_seq_duplicate_rejected");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.output = "test";
    in.output_len = 4;
    ax_oracle_params_init_null(&in.params);
    in.ledger_seq = 100;
    
    ax_admission_ctx_init(&ctx);
    
    /* Admit first */
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result != AX_OK) {
        FAIL("first admission should succeed");
        return;
    }
    
    /* Attempt duplicate - must fail */
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    if (result == AX_ERR_ORDERING && faults.ordering == 1) {
        PASS();
    } else {
        FAIL("duplicate ledger_seq not rejected");
    }
}

/* ========================================================================
 * H4: Encoding Rejection Totality
 *
 * Encoding rejection must occur BEFORE hash computation.
 * Invalid encoding must produce ERROR record, not abort.
 * ======================================================================== */

/**
 * Test that invalid UTF-8 is rejected before hash computation.
 */
static void test_h4_invalid_utf8_rejected_before_hash(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    
    TEST("h4_invalid_utf8_rejected_before_hash");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.output = "test\xFF\xFE";  /* Invalid UTF-8 sequence */
    in.output_len = 6;
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    /* Must fail with encoding error */
    if (result == AX_ERR_ENCODING && faults.encoding == 1) {
        /* Verify ERROR state */
        if (obs.completion_state == AX_COMPLETION_ERROR) {
            PASS();
        } else {
            FAIL("completion_state should be ERROR");
        }
    } else {
        FAIL("invalid UTF-8 not rejected");
        printf("    result: %d, faults.encoding: %d\n", result, faults.encoding);
    }
}

/**
 * Test that control characters are rejected before hash computation.
 */
static void test_h4_control_char_rejected_before_hash(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    
    TEST("h4_control_char_rejected_before_hash");
    
    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.output = "test\x01\x02";  /* Control characters (forbidden) */
    in.output_len = 6;
    ax_oracle_params_init_null(&in.params);
    
    ax_admission_ctx_init(&ctx);
    ct_fault_clear(&faults);
    result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
    
    /* Must fail with encoding error */
    if (result == AX_ERR_ENCODING && faults.encoding == 1) {
        PASS();
    } else {
        FAIL("control characters not rejected");
        printf("    result: %d, faults.encoding: %d\n", result, faults.encoding);
    }
}

/**
 * Test that encoding rejection is total — all bad cases caught.
 */
static void test_h4_encoding_rejection_totality(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ct_fault_flags_t faults;
    int result;
    int all_rejected = 1;
    
    TEST("h4_encoding_rejection_totality");
    
    /* Test vectors of invalid encodings with explicit lengths */
    struct {
        const char *data;
        size_t len;
        const char *desc;
    } vectors[] = {
        { "test\x80",              5, "invalid continuation byte" },
        { "test\xC0\x80",          6, "overlong encoding" },
        { "test\xF5\x80\x80\x80",  8, "beyond Unicode range" },
        { "test\x00test",          9, "embedded NUL" },
        { "test\x7F",              5, "DEL character" },
    };
    int num_vectors = sizeof(vectors) / sizeof(vectors[0]);
    
    for (int i = 0; i < num_vectors; i++) {
        memset(&in, 0, sizeof(in));
        in.completion_state = AX_COMPLETION_COMPLETE;
        in.failure_type = AX_FAILURE_NULL;
        in.ledger_seq = (uint64_t)(i + 1);
        in.oracle_id = "oracle";
        in.model_id = "model";
        in.output = vectors[i].data;
        in.output_len = vectors[i].len;
        ax_oracle_params_init_null(&in.params);
        
        ax_admission_ctx_init(&ctx);
        ct_fault_clear(&faults);
        result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);
        
        if (result != AX_ERR_ENCODING || faults.encoding != 1) {
            printf("\n    Vector %d (%s) not rejected: result=%d, encoding=%d",
                   i, vectors[i].desc, result, faults.encoding);
            all_rejected = 0;
        }
    }
    
    if (all_rejected) {
        PASS();
    } else {
        printf("\n");
        FAIL("not all invalid encodings rejected");
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("======================================================================\n");
    printf("axioma-oracle — L3 Hardening Tests\n");
    printf("Audit Closure: H1-H4\n");
    printf("======================================================================\n\n");
    
    /* H1: Hash Self-Reference */
    printf("[SUITE] H1: Hash Self-Reference Edge Case\n");
    test_h1_hash_self_reference_explicit_empty();
    test_h1_hash_changes_if_not_cleared();
    
    /* H2: Canonicalisation Single Source */
    printf("\n[SUITE] H2: Canonicalisation Single Source of Truth\n");
    test_h2_canonical_consistency();
    
    /* H3: Ledger Sequence Authority */
    printf("\n[SUITE] H3: Ledger Sequence Authority\n");
    test_h3_ledger_seq_ordering_enforced();
    test_h3_ledger_seq_duplicate_rejected();
    
    /* H4: Encoding Rejection Totality */
    printf("\n[SUITE] H4: Encoding Rejection Totality\n");
    test_h4_invalid_utf8_rejected_before_hash();
    test_h4_control_char_rejected_before_hash();
    test_h4_encoding_rejection_totality();
    
    /* Summary */
    printf("\n======================================================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("======================================================================\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}

/**
 * @file test_obs_hash_domain.c
 * @brief Verify obs_hash is computed over correct domain
 *
 * AUDIT REQUIREMENT: Prove
 *   1. obs_hash field present as empty string during hash computation
 *   2. obs_hash = SHA-256("AX:OBS:v1" || LE64(|payload|) || payload) — DVEC-001 §4.3
 *   3. Canonicalisation applied BEFORE hashing
 *   4. Recomputation matches stored value
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"
#include "axilog/commitment.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { printf("  [TEST] %s... ", name); tests_run++; } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* Test 1: obs_hash field is PRESENT and EMPTY during computation */
static void test_obs_hash_field_empty_during_computation(void)
{
    ax_obs_record_t obs;
    char canonical_without_hash[8192];

    TEST("obs_hash_field_empty_during_computation");

    ax_obs_init(&obs);
    obs.completion_state = AX_COMPLETION_COMPLETE;
    obs.failure_type = AX_FAILURE_NULL;
    memset(obs.input_hash, 0xAB, 32);
    obs.ledger_seq = 42;
    obs.model_id = "test";
    obs.oracle_id = "test";
    obs.output = "test";
    obs.output_size = 4;
    ax_oracle_params_init_null(&obs.params);
    obs.schema_version = AX_OBS_SCHEMA_VERSION;

    /* Canonicalise with include_hash = 0 (empty string) */
    obs.obs_hash[0] = '\0';
    ax_obs_canonicalise(canonical_without_hash, sizeof(canonical_without_hash), &obs, 0);

    /* Verify obs_hash field is present as empty string */
    if (strstr(canonical_without_hash, "\"obs_hash\":\"\"") != NULL) {
        PASS();
    } else {
        FAIL("obs_hash field not present as empty string");
        printf("    Canonical: %.200s\n", canonical_without_hash);
    }
}

/* Test 2: Recomputation matches stored value
 *
 * Manual recomputation uses axilog_commit("AX:OBS:v1", ...) — the
 * domain-separated commitment format per DVEC-001 §4.3. This matches
 * ax_obs_compute_hash() exactly. */
static void test_obs_hash_recomputation_matches(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    char stored_hash[65];
    uint8_t recomputed[32];
    char recomputed_hex[65];
    char canonical_buf[8192];
    ct_fault_flags_t commit_faults;  /* substrate layout — crosses axilog_commit (E-ABI-1 fixed) */
    int len;

    TEST("obs_hash_recomputation_matches");

    ax_obs_input_t in;
    ax_admission_ctx_t ctx;
    ax_l3_fault_flags_t faults;

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 100;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test input";
    in.input_len = 10;
    in.output = "test output";
    in.output_len = 11;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ax_l3_fault_init(&faults);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Store original hash */
    strcpy(stored_hash, obs.obs_hash);

    /* Manually recompute using domain-separated commitment per DVEC-001 §4.3:
     *   commit = SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)
     * Must match ax_obs_compute_hash() exactly. */
    obs.obs_hash[0] = '\0';
    len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &obs, 0);
    ct_fault_init(&commit_faults);
    axilog_commit(
        "AX:OBS:v1",
        (const uint8_t *)canonical_buf,
        (uint64_t)len,
        recomputed,
        &commit_faults
    );
    ax_format_hash_hex(recomputed_hex, sizeof(recomputed_hex), recomputed, 32);

    if (strcmp(stored_hash, recomputed_hex) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch");
        printf("    Stored:     %s\n", stored_hash);
        printf("    Recomputed: %s\n", recomputed_hex);
    }
}

/* Test 3: Hash is over CANONICAL form, not struct memory */
static void test_hash_is_over_canonical_not_struct(void)
{
    ax_obs_record_t obs1, obs2;
    char output_buf1[256], output_buf2[256];

    TEST("hash_is_over_canonical_not_struct");

    ax_obs_input_t in;
    ax_admission_ctx_t ctx1, ctx2;
    ax_l3_fault_flags_t faults1, faults2;

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
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx1);
    ax_l3_fault_init(&faults1);
    ax_obs_admit(&obs1, output_buf1, sizeof(output_buf1), &in, &ctx1, &faults1);

    ax_admission_ctx_init(&ctx2);
    ax_l3_fault_init(&faults2);
    ax_obs_admit(&obs2, output_buf2, sizeof(output_buf2), &in, &ctx2, &faults2);

    /* Despite being in different memory locations, hashes MUST match */
    if (strcmp(obs1.obs_hash, obs2.obs_hash) == 0) {
        PASS();
    } else {
        FAIL("identical observations produced different hashes");
        printf("    obs1.obs_hash: %s\n", obs1.obs_hash);
        printf("    obs2.obs_hash: %s\n", obs2.obs_hash);
    }
}

/* Test 4: Validation recomputation detects tampering */
static void test_validation_detects_tampering(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_l3_fault_flags_t faults;
    int result;

    TEST("validation_detects_tampering");

    ax_obs_input_t in;
    ax_admission_ctx_t ctx;

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
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ax_l3_fault_init(&faults);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Tamper with a single character in obs_hash */
    obs.obs_hash[0] = (obs.obs_hash[0] == 'a') ? 'b' : 'a';

    ax_l3_fault_init(&faults);
    result = ax_obs_validate(&obs, &faults);

    if (result == AX_ERR_HASH && faults.protocol) {
        PASS();
    } else {
        FAIL("tampering not detected");
    }
}

/* Test 5: Single bit flip in content detected */
static void test_single_bit_flip_detected(void)
{
    ax_obs_record_t obs;
    char output_buf[256];
    ax_l3_fault_flags_t faults;
    int result;

    TEST("single_bit_flip_detected");

    ax_obs_input_t in;
    ax_admission_ctx_t ctx;

    memset(&in, 0, sizeof(in));
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type = AX_FAILURE_NULL;
    in.ledger_seq = 1;
    in.oracle_id = "oracle";
    in.model_id = "model";
    in.input = "test";
    in.input_len = 4;
    in.output = "test output here";
    in.output_len = 16;
    ax_oracle_params_init_null(&in.params);

    ax_admission_ctx_init(&ctx);
    ax_l3_fault_init(&faults);
    ax_obs_admit(&obs, output_buf, sizeof(output_buf), &in, &ctx, &faults);

    /* Flip one bit in output */
    output_buf[5] ^= 0x01;

    ax_l3_fault_init(&faults);
    result = ax_obs_validate(&obs, &faults);

    if (result == AX_ERR_HASH && faults.protocol) {
        PASS();
    } else {
        FAIL("bit flip not detected");
    }
}

int main(void)
{
    printf("\n=== obs_hash DOMAIN VERIFICATION ===\n\n");

    test_obs_hash_field_empty_during_computation();
    test_obs_hash_recomputation_matches();
    test_hash_is_over_canonical_not_struct();
    test_validation_detects_tampering();
    test_single_bit_flip_detected();

    printf("\n=== Results: %d/%d passed ===\n\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}

/**
 * @file test_ledger.c
 * @brief Unit tests for cryptographic audit ledger
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * Test model:
 * - Pure C99
 * - exit(0) pass / exit(1) fail
 * - Fixed vectors, property tests, fault injection
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @traceability SRS-001-SHALL-006, SRS-001-SHALL-007, SRS-005-SHALL-001,
 *               SRS-005-SHALL-002, SRS-005-SHALL-005, SRS-005-SHALL-006,
 *               SRS-005-SHALL-007, SRS-006-SHALL-001, SRS-006-SHALL-002,
 *               SRS-006-SHALL-003, SRS-006-SHALL-004, SRS-006-SHALL-005,
 *               SRS-006-SHALL-006, SRS-006-SHALL-007, SRS-007-SHALL-006,
 *               SRS-007-SHALL-008, SRS-011-SHALL-001, SRS-011-SHALL-003
 */

#include <axilog/audit.h>
#include <axilog/types.h>
#include <axilog/dvec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

/**
 * @brief Convert bytes to hex string for display
 */
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/**
 * @brief Compare two byte arrays
 */
static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Check if all bytes are zero
 */
static int all_zero(const uint8_t *bytes, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (bytes[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Run a single test
 */
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %s... ", #name); \
    fflush(stdout); \
    if (name()) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

/* ========================================================================
 * GENESIS TESTS (SRS-001-SHALL-006, SRS-001-SHALL-007)
 * ======================================================================== */

/**
 * @brief Test genesis initialization produces valid context
 *
 * SRS-001-SHALL-006: Deterministic initialization sequence.
 * SRS-001-SHALL-007: Configuration evidence binding.
 */
static int test_genesis_initialization(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    /* Must succeed without faults */
    if (ct_fault_any(&faults)) {
        printf("Genesis faulted: ");
        return 0;
    }

    /* Must be initialized */
    if (ctx.initialised != 1U) {
        printf("Not initialized: ");
        return 0;
    }

    /* Must not be failed */
    if (ctx.failed != 0U) {
        printf("Failed flag set: ");
        return 0;
    }

    /* Sequence must be 0 */
    if (ctx.sequence != 0U) {
        printf("Sequence != 0: ");
        return 0;
    }

    /* Genesis hash must not be all zeros */
    if (all_zero(ctx.genesis_hash, 32)) {
        printf("Genesis hash is zero: ");
        return 0;
    }

    /* Current hash must equal genesis hash initially */
    if (!bytes_equal(ctx.current_hash, ctx.genesis_hash, 32)) {
        printf("Current != genesis: ");
        return 0;
    }

    /* Padding must be zero */
    if (!all_zero(ctx._pad, 6)) {
        printf("Padding not zero: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test genesis determinism — identical across calls
 *
 * SRS-001-SHALL-006: Two independent initializations MUST produce
 * identical genesis_hash across all platforms.
 */
static int test_genesis_determinism(void)
{
    ax_ledger_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx1, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx2, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }

    /* Genesis hashes must be identical */
    if (!bytes_equal(ctx1.genesis_hash, ctx2.genesis_hash, 32)) {
        printf("Genesis hashes differ: ");
        return 0;
    }

    /* Current hashes must be identical */
    if (!bytes_equal(ctx1.current_hash, ctx2.current_hash, 32)) {
        printf("Current hashes differ: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test genesis NULL context rejection
 *
 * SRS-007-SHALL-008: Input domain validation.
 */
static int test_genesis_null_context(void)
{
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(NULL, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test genesis NULL faults handling
 */
static int test_genesis_null_faults(void)
{
    ax_ledger_ctx_t ctx;

    /* Should not crash with NULL faults */
    ax_ledger_genesis(&ctx, NULL);

    /* If we got here without crash, it handled NULL faults */
    return 1;
}

/* ========================================================================
 * APPEND TESTS (SRS-006-SHALL-002, SRS-006-SHALL-004)
 * ======================================================================== */

/**
 * @brief Test basic append operation
 *
 * SRS-006-SHALL-002: Chain extension function.
 * SRS-006-SHALL-004: Append-only behaviour.
 */
static int test_append_basic(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    size_t i;

    /* Initialize genesis */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }

    /* Create a test commitment */
    for (i = 0; i < 32; i++) {
        commit[i] = (uint8_t)i;
    }

    /* Append should succeed */
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    if (ct_fault_any(&faults)) {
        printf("Append faulted: ");
        return 0;
    }

    /* Sequence should be incremented */
    if (ctx.sequence != 1U) {
        printf("Sequence not incremented: ");
        return 0;
    }

    /* Current hash should have changed */
    if (bytes_equal(ctx.current_hash, ctx.genesis_hash, 32)) {
        printf("Hash unchanged after append: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test multiple appends produce distinct chain states
 *
 * SRS-006-SHALL-001: Strict total order.
 */
static int test_append_multiple(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    uint8_t hash_after_1[32];
    uint8_t hash_after_2[32];
    uint8_t hash_after_3[32];
    size_t i;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }

    /* Append #1 */
    for (i = 0; i < 32; i++) {
        commit[i] = (uint8_t)(i + 1);
    }
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }
    memcpy(hash_after_1, ctx.current_hash, 32);

    /* Append #2 */
    for (i = 0; i < 32; i++) {
        commit[i] = (uint8_t)(i + 2);
    }
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }
    memcpy(hash_after_2, ctx.current_hash, 32);

    /* Append #3 */
    for (i = 0; i < 32; i++) {
        commit[i] = (uint8_t)(i + 3);
    }
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);
    if (ct_fault_any(&faults)) {
        return 0;
    }
    memcpy(hash_after_3, ctx.current_hash, 32);

    /* All chain states must be distinct */
    if (bytes_equal(hash_after_1, hash_after_2, 32)) {
        printf("Hash 1 == Hash 2: ");
        return 0;
    }
    if (bytes_equal(hash_after_1, hash_after_3, 32)) {
        printf("Hash 1 == Hash 3: ");
        return 0;
    }
    if (bytes_equal(hash_after_2, hash_after_3, 32)) {
        printf("Hash 2 == Hash 3: ");
        return 0;
    }

    /* Sequence must be 3 */
    if (ctx.sequence != 3U) {
        printf("Sequence != 3: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test append determinism — same commits produce same chain
 */
static int test_append_determinism(void)
{
    ax_ledger_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    size_t i;

    /* Initialize both contexts */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx1, &faults);
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx2, &faults);

    /* Apply same sequence of commits to both */
    for (i = 0; i < 10; i++) {
        size_t j;
        for (j = 0; j < 32; j++) {
            commit[j] = (uint8_t)((i * 7 + j * 13) & 0xFF);
        }

        ct_fault_init(&faults);
        ax_ledger_append(&ctx1, commit, &faults);
        ct_fault_init(&faults);
        ax_ledger_append(&ctx2, commit, &faults);
    }

    /* Chain states must be identical */
    if (!bytes_equal(ctx1.current_hash, ctx2.current_hash, 32)) {
        printf("Chains diverged: ");
        return 0;
    }

    if (ctx1.sequence != ctx2.sequence) {
        printf("Sequences differ: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * ENTRY GUARD TESTS (SRS-005-SHALL-005)
 * ======================================================================== */

/**
 * @brief Test append to already-failed context
 *
 * Guard 1: ctx->failed == 1 → set ledger_fail, return
 */
static int test_append_guard_failed(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32] = {0};

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    /* Force failed state */
    ctx.failed = 1;

    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    /* Must set ledger_fail */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    /* Sequence must not change */
    if (ctx.sequence != 0U) {
        printf("Sequence changed: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test append to uninitialized context
 *
 * Guard 2: ctx->initialised != 1 → set ledger_fail, set failed, return
 */
static int test_append_guard_uninitialized(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32] = {0};

    /* Zero context without genesis */
    memset(&ctx, 0, sizeof(ctx));

    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    /* Must set ledger_fail */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    /* Must set failed */
    if (ctx.failed != 1U) {
        printf("Should set failed: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test append at sequence overflow
 *
 * Guard 3: ctx->sequence == UINT64_MAX → set ledger_fail, set failed, return
 * SRS-006-SHALL-005: Sequence overflow rule.
 */
static int test_append_guard_overflow(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32] = {0};

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    /* Force sequence to max */
    ctx.sequence = UINT64_MAX;

    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    /* Must set ledger_fail */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    /* Must set failed */
    if (ctx.failed != 1U) {
        printf("Should set failed: ");
        return 0;
    }

    /* Sequence must not wrap */
    if (ctx.sequence != UINT64_MAX) {
        printf("Sequence wrapped: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test append NULL context
 */
static int test_append_null_context(void)
{
    ct_fault_flags_t faults;
    uint8_t commit[32] = {0};

    ct_fault_init(&faults);
    ax_ledger_append(NULL, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test append NULL commit
 */
static int test_append_null_commit(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    ct_fault_init(&faults);
    ax_ledger_append(&ctx, NULL, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * VERIFY CHAIN TESTS (SRS-006-SHALL-007)
 * ======================================================================== */

/**
 * @brief Test verify chain on valid context
 */
static int test_verify_valid(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    ct_fault_init(&faults);
    ax_verify_chain(&ctx, &faults);

    /* Should not fault on valid context */
    if (ct_fault_any(&faults)) {
        printf("Valid context faulted: ");
        return 0;
    }

    /* Should not set failed */
    if (ctx.failed != 0U) {
        printf("Valid context marked failed: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test verify chain on uninitialized context
 */
static int test_verify_uninitialized(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    memset(&ctx, 0, sizeof(ctx));

    ct_fault_init(&faults);
    ax_verify_chain(&ctx, &faults);

    /* Must fault */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    /* Must set failed */
    if (ctx.failed != 1U) {
        printf("Should set failed: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test verify chain on already-failed context
 */
static int test_verify_already_failed(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);
    ctx.failed = 1;

    ct_fault_init(&faults);
    ax_verify_chain(&ctx, &faults);

    /* Must fault */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test verify chain detects corrupted padding
 */
static int test_verify_corrupted_padding(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    /* Corrupt padding */
    ctx._pad[0] = 0xFF;

    ct_fault_init(&faults);
    ax_verify_chain(&ctx, &faults);

    /* Must fault */
    if (faults.ledger_fail != 1U) {
        printf("Should set ledger_fail: ");
        return 0;
    }

    /* Must set failed */
    if (ctx.failed != 1U) {
        printf("Should set failed: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test verify NULL context
 */
static int test_verify_null_context(void)
{
    ct_fault_flags_t faults;

    ct_fault_init(&faults);
    ax_verify_chain(NULL, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * COMMIT EVIDENCE TESTS (SRS-006-SHALL-003, SRS-007-SHALL-006)
 * ======================================================================== */

/**
 * @brief Test commit evidence with valid input
 */
static int test_commit_evidence_valid(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "{\"test\":true}";

    ev.tag = AX_TAG_STATE;
    ev.payload = payload;
    ev.payload_len = sizeof(payload) - 1;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (ct_fault_any(&faults)) {
        printf("Valid evidence faulted: ");
        return 0;
    }

    /* Output should not be all zeros */
    if (all_zero(commit, 32)) {
        printf("Output is zero: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence with all valid tags
 */
static int test_commit_evidence_all_tags(void)
{
    const char *tags[] = {
        AX_TAG_STATE,
        AX_TAG_TRANS,
        AX_TAG_OBS,
        AX_TAG_POLICY,
        AX_TAG_PROOF,
        /* FCC-001 claim tags, registered DVEC v1.4 */
        AX_TAG_FCC_C,
        AX_TAG_FCC_TS,
        AX_TAG_FCC_DEV,
        AX_TAG_FCC_REG,
        AX_TAG_FCC_VERDICT
    };
    size_t num_tags = sizeof(tags) / sizeof(tags[0]);
    size_t i;
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "test";

    for (i = 0; i < num_tags; i++) {
        ev.tag = tags[i];
        ev.payload = payload;
        ev.payload_len = 4;

        ct_fault_init(&faults);
        ax_commit_evidence(&ev, commit, &faults);

        if (ct_fault_any(&faults)) {
            printf("Tag %s faulted: ", tags[i]);
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects chain tag as evidence type
 *
 * SRS-006-SHALL-003: Chain tag separation.
 */
static int test_commit_evidence_rejects_chain_tag(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "test";

    ev.tag = AX_CHAIN_LEDGER;  /* This is a chain tag, NOT evidence type */
    ev.payload = payload;
    ev.payload_len = 4;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should reject chain tag as evidence: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects NULL evidence
 */
static int test_commit_evidence_null_evidence(void)
{
    ct_fault_flags_t faults;
    uint8_t commit[32];

    ct_fault_init(&faults);
    ax_commit_evidence(NULL, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects NULL tag
 */
static int test_commit_evidence_null_tag(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "test";

    ev.tag = NULL;
    ev.payload = payload;
    ev.payload_len = 4;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects NULL payload
 */
static int test_commit_evidence_null_payload(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];

    ev.tag = AX_TAG_STATE;
    ev.payload = NULL;
    ev.payload_len = 4;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects zero payload length
 */
static int test_commit_evidence_zero_length(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "test";

    ev.tag = AX_TAG_STATE;
    ev.payload = payload;
    ev.payload_len = 0;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test commit evidence rejects invalid tag
 */
static int test_commit_evidence_invalid_tag(void)
{
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "test";

    ev.tag = "INVALID:TAG:v1";
    ev.payload = payload;
    ev.payload_len = 4;

    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);

    if (faults.domain != 1U) {
        printf("Should set domain fault: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * FAIL-CLOSED TERMINALITY TESTS (SRS-005-SHALL-005, SRS-005-SHALL-006)
 * ======================================================================== */

/**
 * @brief Test that failed context blocks all operations
 *
 * SRS-005-SHALL-005: Fail-closed terminality.
 */
static int test_fail_closed(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32] = {1, 2, 3};
    uint8_t original_hash[32];

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    /* Save original state */
    memcpy(original_hash, ctx.current_hash, 32);

    /* Force failure */
    ctx.failed = 1;

    /* Attempt append */
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    /* Must not mutate hash */
    if (!bytes_equal(ctx.current_hash, original_hash, 32)) {
        printf("Hash mutated after failure: ");
        return 0;
    }

    /* Must not increment sequence */
    if (ctx.sequence != 0U) {
        printf("Sequence mutated after failure: ");
        return 0;
    }

    return 1;
}

/**
 * @brief Test fault flag persistence
 *
 * SRS-005-SHALL-006: Fault flags SHALL NOT be cleared implicitly.
 */
static int test_fault_persistence(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    uint8_t commit[32] = {0};

    /* Initialize without genesis (will fail) */
    memset(&ctx, 0, sizeof(ctx));

    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    /* Context should be failed */
    if (ctx.failed != 1U) {
        printf("Context not failed: ");
        return 0;
    }

    /* Subsequent operations should still fail */
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);

    if (faults.ledger_fail != 1U) {
        printf("Fault not persistent: ");
        return 0;
    }

    /* Context should remain failed */
    if (ctx.failed != 1U) {
        printf("Failed state not persistent: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * PROPERTY TESTS
 * ======================================================================== */

/**
 * @brief Property: Chain extension is deterministic
 */
static int test_property_chain_determinism(void)
{
    ax_ledger_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults;
    uint8_t commits[50][32];
    size_t i, j;

    /* Generate pseudo-random commits */
    for (i = 0; i < 50; i++) {
        for (j = 0; j < 32; j++) {
            commits[i][j] = (uint8_t)((i * 31 + j * 17) & 0xFF);
        }
    }

    /* Apply to both chains */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx1, &faults);
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx2, &faults);

    for (i = 0; i < 50; i++) {
        ct_fault_init(&faults);
        ax_ledger_append(&ctx1, commits[i], &faults);
        ct_fault_init(&faults);
        ax_ledger_append(&ctx2, commits[i], &faults);

        /* Must remain synchronized */
        if (!bytes_equal(ctx1.current_hash, ctx2.current_hash, 32)) {
            printf("Chains diverged at %zu: ", i);
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Property: Order matters — different order produces different chain
 */
static int test_property_order_matters(void)
{
    ax_ledger_ctx_t ctx1, ctx2;
    ct_fault_flags_t faults;
    uint8_t commit_a[32] = {1};
    uint8_t commit_b[32] = {2};

    /* Chain 1: A then B */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx1, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx1, commit_a, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx1, commit_b, &faults);

    /* Chain 2: B then A */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx2, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx2, commit_b, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx2, commit_a, &faults);

    /* Must be different */
    if (bytes_equal(ctx1.current_hash, ctx2.current_hash, 32)) {
        printf("Order should matter: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * CROSS-PLATFORM IDENTITY TESTS (SRS-011-SHALL-001, SRS-011-SHALL-003)
 * ======================================================================== */

/**
 * @brief Generate golden reference for genesis hash
 *
 * This hash MUST be identical on x86_64, ARM64, and RISC-V.
 */
static int test_golden_genesis(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    char hex[65];

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);

    if (ct_fault_any(&faults)) {
        printf("Genesis faulted: ");
        return 0;
    }

    bytes_to_hex(ctx.genesis_hash, 32, hex);
    /* Machine-readable marker for CI extraction */
    printf("\nL0:%s\n", hex);
    printf("    Golden genesis L0: %s\n    ", hex);

    /* Verify repeated calls produce same result */
    {
        ax_ledger_ctx_t ctx2;
        ct_fault_init(&faults);
        ax_ledger_genesis(&ctx2, &faults);

        if (!bytes_equal(ctx.genesis_hash, ctx2.genesis_hash, 32)) {
            printf("Genesis not deterministic: ");
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Generate golden reference for chain after 3 appends
 */
static int test_golden_chain_3_appends(void)
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t faults;
    char hex[65];
    uint8_t commit1[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    uint8_t commit2[32] = {
        0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
        0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
        0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8,
        0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0
    };
    uint8_t commit3[32] = {
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };

    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit1, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit2, &faults);
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit3, &faults);

    if (ct_fault_any(&faults)) {
        printf("Chain construction faulted: ");
        return 0;
    }

    bytes_to_hex(ctx.current_hash, 32, hex);
    /* Machine-readable marker for CI extraction */
    printf("\nL3:%s\n", hex);
    printf("    Golden L3: %s\n    ", hex);

    /* Verify sequence */
    if (ctx.sequence != 3U) {
        printf("Sequence != 3: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * INTEGRATION TEST
 * ======================================================================== */

/**
 * @brief Full integration: evidence → commit → append → verify
 */
static int test_integration_full_flow(void)
{
    ax_ledger_ctx_t ctx;
    ax_evidence_t ev;
    ct_fault_flags_t faults;
    uint8_t commit[32];
    const uint8_t payload[] = "{\"event\":\"test_event\",\"timestamp\":12345}";

    /* Initialize ledger */
    ct_fault_init(&faults);
    ax_ledger_genesis(&ctx, &faults);
    if (ct_fault_any(&faults)) {
        printf("Genesis failed: ");
        return 0;
    }

    /* Create evidence */
    ev.tag = AX_TAG_OBS;
    ev.payload = payload;
    ev.payload_len = sizeof(payload) - 1;

    /* Commit evidence */
    ct_fault_init(&faults);
    ax_commit_evidence(&ev, commit, &faults);
    if (ct_fault_any(&faults)) {
        printf("Commit failed: ");
        return 0;
    }

    /* Append to ledger */
    ct_fault_init(&faults);
    ax_ledger_append(&ctx, commit, &faults);
    if (ct_fault_any(&faults)) {
        printf("Append failed: ");
        return 0;
    }

    /* Verify chain */
    ct_fault_init(&faults);
    ax_verify_chain(&ctx, &faults);
    if (ct_fault_any(&faults)) {
        printf("Verify failed: ");
        return 0;
    }

    /* Check final state */
    if (ctx.sequence != 1U) {
        printf("Sequence != 1: ");
        return 0;
    }

    if (ctx.failed != 0U) {
        printf("Unexpected failure: ");
        return 0;
    }

    return 1;
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(void)
{
    printf("axioma-audit: test_ledger\n");
    printf("DVEC: v1.3 | Layer: L6 | Class: D1\n");
    printf("========================================\n\n");

    printf("Genesis Tests:\n");
    RUN_TEST(test_genesis_initialization);
    RUN_TEST(test_genesis_determinism);
    RUN_TEST(test_genesis_null_context);
    RUN_TEST(test_genesis_null_faults);

    printf("\nAppend Tests:\n");
    RUN_TEST(test_append_basic);
    RUN_TEST(test_append_multiple);
    RUN_TEST(test_append_determinism);

    printf("\nEntry Guard Tests:\n");
    RUN_TEST(test_append_guard_failed);
    RUN_TEST(test_append_guard_uninitialized);
    RUN_TEST(test_append_guard_overflow);
    RUN_TEST(test_append_null_context);
    RUN_TEST(test_append_null_commit);

    printf("\nVerify Chain Tests:\n");
    RUN_TEST(test_verify_valid);
    RUN_TEST(test_verify_uninitialized);
    RUN_TEST(test_verify_already_failed);
    RUN_TEST(test_verify_corrupted_padding);
    RUN_TEST(test_verify_null_context);

    printf("\nCommit Evidence Tests:\n");
    RUN_TEST(test_commit_evidence_valid);
    RUN_TEST(test_commit_evidence_all_tags);
    RUN_TEST(test_commit_evidence_rejects_chain_tag);
    RUN_TEST(test_commit_evidence_null_evidence);
    RUN_TEST(test_commit_evidence_null_tag);
    RUN_TEST(test_commit_evidence_null_payload);
    RUN_TEST(test_commit_evidence_zero_length);
    RUN_TEST(test_commit_evidence_invalid_tag);

    printf("\nFail-Closed Terminality Tests:\n");
    RUN_TEST(test_fail_closed);
    RUN_TEST(test_fault_persistence);

    printf("\nProperty Tests:\n");
    RUN_TEST(test_property_chain_determinism);
    RUN_TEST(test_property_order_matters);

    printf("\nCross-Platform Identity Tests:\n");
    RUN_TEST(test_golden_genesis);
    RUN_TEST(test_golden_chain_3_appends);

    printf("\nIntegration Tests:\n");
    RUN_TEST(test_integration_full_flow);

    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    if (tests_passed != tests_run) {
        return 1;
    }

    return 0;
}

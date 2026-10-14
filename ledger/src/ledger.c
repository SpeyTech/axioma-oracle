/**
 * @file ledger.c
 * @brief Axilog Layer 6 Cryptographic Audit Ledger Implementation
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * Implements the append-only cryptographic hash chain per SRS-001 v0.3.
 * Chain extension: Ln = SHA-256("AX:LEDGER:v1" || Ln-1 || commit(en))
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @traceability SRS-001-SHALL-006, SRS-001-SHALL-007, SRS-005-SHALL-001,
 *               SRS-005-SHALL-002, SRS-005-SHALL-003, SRS-005-SHALL-005,
 *               SRS-005-SHALL-006, SRS-006-SHALL-001, SRS-006-SHALL-002,
 *               SRS-006-SHALL-003, SRS-006-SHALL-004, SRS-006-SHALL-005,
 *               SRS-006-SHALL-006, SRS-006-SHALL-007, SRS-007-SHALL-006,
 *               SRS-007-SHALL-008
 */

#include <axilog/audit.h>
#include <axilog/commitment.h>
#include <axilog/sha256.h>
#include <axilog/dvec.h>
#include <string.h>
#include <stdint.h>

/**
 * @brief Validate evidence tag against domain registry
 *
 * SRS-006-SHALL-003: The ledger SHALL use AX:LEDGER:v1 only as a chain tag
 *                    and SHALL NOT use it as an evidence type identifier.
 * SRS-007-SHALL-006: Evidence type SHALL be one of the registered types.
 *
 * @param tag Tag string to validate
 * @return 1 if valid evidence tag, 0 otherwise
 */
/* Registered evidence type tags (DVEC-001 §4.4). Table-driven over the
 * AX_TAG_* defines in <axilog/dvec.h> so a registry amendment in the
 * canonical header flows here without editing this function. v1.4 added
 * the AX:FCC block. */
static const char *const REGISTERED_EVIDENCE_TAGS[] = {
    AX_TAG_STATE,
    AX_TAG_TRANS,
    AX_TAG_OBS,
    AX_TAG_POLICY,
    AX_TAG_PROOF,
    AX_TAG_FCC_C,
    AX_TAG_FCC_TS,
    AX_TAG_FCC_DEV,
    AX_TAG_FCC_REG,
    AX_TAG_FCC_VERDICT,
};

static int is_valid_evidence_tag(const char *tag)
{
    size_t i;

    if (tag == NULL) {
        return 0;
    }

    for (i = 0; i < sizeof(REGISTERED_EVIDENCE_TAGS) /
                    sizeof(REGISTERED_EVIDENCE_TAGS[0]); i++) {
        if (strcmp(tag, REGISTERED_EVIDENCE_TAGS[i]) == 0) {
            return 1;
        }
    }

    /* Chain tags (AX:LEDGER:v1, DVM:*) are not evidence types. */
    return 0;
}

/**
 * @brief Initialize ledger with deterministic genesis
 *
 * SRS-001-SHALL-006: Deterministic initialization sequence.
 * SRS-001-SHALL-007: Configuration evidence binding (genesis is first record).
 * SRS-006-SHALL-002: Chain extension function.
 *
 * GENESIS SEQUENCE (per specification):
 * 1. memset(ctx, 0, sizeof(ax_ledger_ctx_t))
 * 2. Compute e0 = commit(AX:STATE:v1, genesis_payload)
 * 3. Compute L0 = SHA-256("AX:LEDGER:v1" || e0)
 * 4. Store genesis state
 */
void ax_ledger_genesis(
    ax_ledger_ctx_t  *ctx,
    ct_fault_flags_t *faults
)
{
    uint8_t e0[32];
    size_t chain_tag_len;

    /* Input validation (SRS-007-SHALL-008) */
    if (ctx == NULL || faults == NULL) {
        if (faults != NULL) {
            faults->domain = 1;
        }
        return;
    }

    /* Step 1: Zero entire context (SRS-001-SHALL-006) */
    memset(ctx, 0, sizeof(ax_ledger_ctx_t));

    /* Step 2: Compute e0 = commit(AX:STATE:v1, genesis_payload) */
    axilog_commit(
        AX_TAG_STATE,
        (const uint8_t *)AX_GENESIS_PAYLOAD,
        AX_GENESIS_PAYLOAD_LEN,
        e0,
        faults
    );

    if (faults->domain != 0U) {
        /* Commitment failed — context remains zeroed */
        return;
    }

    /*
     * Step 3: Compute L0 = SHA-256("AX:LEDGER:v1" || e0)
     *
     * Note: This is the GENESIS chain extension. Unlike subsequent
     * extensions which use (tag || prev || commit), genesis uses
     * only (tag || e0) because there is no previous hash.
     *
     * SRS-006-SHALL-002: L0 = SHA-256("AX:LEDGER:v1" || commit(e0))
     *
     * Stack buffer: strlen("AX:LEDGER:v1") + 32 = 12 + 32 = 44 bytes
     */
    chain_tag_len = strlen(AX_LEDGER_CHAIN_TAG);
    {
        uint8_t chain_buf[44]; /* 12 + 32 — compile-time bounded */
        memcpy(chain_buf, AX_LEDGER_CHAIN_TAG, chain_tag_len);
        memcpy(chain_buf + chain_tag_len, e0, 32U);
        axilog_sha256(ctx->genesis_hash, chain_buf, chain_tag_len + 32U);
    }

    /* Step 4: Store genesis state */
    memcpy(ctx->current_hash, ctx->genesis_hash, 32);
    ctx->sequence = 0U;
    ctx->initialised = 1U;
    ctx->failed = 0U;
    /* _pad already zeroed by memset */
}

/**
 * @brief Append commitment to ledger chain
 *
 * SRS-005-SHALL-005: Fail-closed terminality.
 * SRS-006-SHALL-002: Chain extension function.
 * SRS-006-SHALL-004: Append-only behaviour.
 * SRS-006-SHALL-005: Sequence overflow rule.
 *
 * Chain extension: Ln = SHA-256("AX:LEDGER:v1" || Ln-1 || commit)
 */
void ax_ledger_append(
    ax_ledger_ctx_t  *ctx,
    const uint8_t     commit[32],
    ct_fault_flags_t *faults
)
{
    size_t chain_tag_len;

    /* Input validation */
    if (ctx == NULL || commit == NULL || faults == NULL) {
        if (faults != NULL) {
            faults->domain = 1;
        }
        return;
    }

    /*
     * ENTRY GUARDS (STRICT ORDER per specification)
     *
     * These MUST be checked in exactly this sequence.
     * SRS-005-SHALL-005: Fail-closed terminality.
     */

    /* Guard 1: Already in failed state */
    if (ctx->failed != 0U) {
        faults->ledger_fail = 1;
        return;
    }

    /* Guard 2: Not initialized */
    if (ctx->initialised != 1U) {
        faults->ledger_fail = 1;
        ctx->failed = 1;
        return;
    }

    /* Guard 3: Sequence overflow (SRS-006-SHALL-005) */
    if (ctx->sequence == UINT64_MAX) {
        faults->ledger_fail = 1;
        ctx->failed = 1;
        return;
    }

    /*
     * Compute chain extension:
     * Ln = SHA-256("AX:LEDGER:v1" || Ln-1 || commit)
     *
     * SRS-006-SHALL-002: Chain extension SHALL be computed as specified.
     *
     * Stack buffer: 12 + 32 + 32 = 76 bytes — compile-time bounded
     */
    chain_tag_len = strlen(AX_LEDGER_CHAIN_TAG);
    {
        uint8_t chain_buf[76]; /* 12 + 32 + 32 */
        memcpy(chain_buf, AX_LEDGER_CHAIN_TAG, chain_tag_len);
        memcpy(chain_buf + chain_tag_len, ctx->current_hash, 32U);
        memcpy(chain_buf + chain_tag_len + 32U, commit, 32U);
        axilog_sha256(ctx->current_hash, chain_buf, chain_tag_len + 64U);
    }

    /* Increment sequence */
    ctx->sequence += 1U;
}

/**
 * @brief Verify internal ledger consistency
 *
 * SRS-005-SHALL-005: Fail-closed terminality.
 * SRS-006-SHALL-007: Verification SHALL detect inconsistency.
 *
 * Phase 1 = internal consistency only (per specification).
 * MUTATING — sets failed flag on any inconsistency.
 */
void ax_verify_chain(
    ax_ledger_ctx_t  *ctx,
    ct_fault_flags_t *faults
)
{
    unsigned int i;

    /* Input validation */
    if (ctx == NULL || faults == NULL) {
        if (faults != NULL) {
            faults->domain = 1;
        }
        return;
    }

    /* Check 1: Already in failed state */
    if (ctx->failed != 0U) {
        faults->ledger_fail = 1;
        return;
    }

    /* Check 2: Must be initialized */
    if (ctx->initialised != 1U) {
        faults->ledger_fail = 1;
        ctx->failed = 1;
        return;
    }

    /* Check 3: Padding bytes must be zero */
    for (i = 0U; i < 6U; i++) {
        if (ctx->_pad[i] != 0U) {
            faults->ledger_fail = 1;
            ctx->failed = 1;
            return;
        }
    }

    /*
     * Context self-check complete. This is not chain verification; see
     * ax_verify_chain_records() / ax_verify_chain_commits().
     */
}

/**
 * @brief Check a ledger context's own flags (preferred name)
 *
 * SRS-005-SHALL-005: Fail-closed terminality.
 */
void ax_ledger_check_context(
    ax_ledger_ctx_t  *ctx,
    ct_fault_flags_t *faults
)
{
    ax_verify_chain(ctx, faults);
}

/*
 * Shared tail of the two chain verifiers: compare the recomputed head.
 */
static ax_chain_verdict_t chain_verdict(
    const ax_ledger_ctx_t *c,
    const uint8_t          expected_head[32],
    uint8_t                out_head[32],
    ct_fault_flags_t      *faults
)
{
    if (out_head != NULL) {
        memcpy(out_head, c->current_hash, 32U);
    }
    if (expected_head == NULL) {
        return AX_CHAIN_CONSISTENT_UNANCHORED;
    }
    if (memcmp(c->current_hash, expected_head, 32U) != 0) {
        faults->ledger_fail = 1;
        return AX_CHAIN_HEAD_MISMATCH;
    }
    return AX_CHAIN_MATCHES_SUPPLIED_HEAD;
}

/**
 * @brief Recompute a chain from its evidence records and compare
 *
 * SRS-006-SHALL-002: Chain extension function (recomputed, not trusted).
 * SRS-006-SHALL-003: Registered evidence tags only.
 * SRS-006-SHALL-007: Verification SHALL detect inconsistency.
 *
 * Uses a private ledger context: the caller's ledger state is never read.
 */
ax_chain_verdict_t ax_verify_chain_records(
    const ax_evidence_t *records,
    uint64_t             n,
    const uint8_t      (*claimed_commits)[32],
    const uint8_t        expected_head[32],
    uint8_t              out_head[32],
    ct_fault_flags_t    *faults
)
{
    ax_ledger_ctx_t  c;
    ct_fault_flags_t f;
    uint8_t          commit[32];
    uint64_t         i;

    if (faults == NULL) {
        return AX_CHAIN_REJECTED;
    }
    if ((records == NULL && n > 0U) || n == UINT64_MAX) {
        faults->domain = 1;
        return AX_CHAIN_REJECTED;
    }

    ct_fault_init(&f);
    ax_ledger_genesis(&c, &f);
    if (ct_fault_any(&f)) {
        faults->domain = 1;
        return AX_CHAIN_REJECTED;
    }

    for (i = 0U; i < n; i++) {
        ax_commit_evidence(&records[i], commit, &f);
        if (ct_fault_any(&f)) {
            faults->domain = 1;
            return AX_CHAIN_REJECTED;
        }
        if (claimed_commits != NULL &&
            memcmp(commit, claimed_commits[i], 32U) != 0) {
            faults->ledger_fail = 1;
            return AX_CHAIN_INCONSISTENT;
        }
        ax_ledger_append(&c, commit, &f);
        if (ct_fault_any(&f)) {
            faults->ledger_fail = 1;
            return AX_CHAIN_REJECTED;
        }
    }

    return chain_verdict(&c, expected_head, out_head, faults);
}

/**
 * @brief Recompute a chain from commitments alone and compare
 *
 * SRS-006-SHALL-002: Chain extension function (recomputed, not trusted).
 * SRS-006-SHALL-007: Verification SHALL detect inconsistency.
 */
ax_chain_verdict_t ax_verify_chain_commits(
    const uint8_t      (*commits)[32],
    uint64_t             n,
    const uint8_t        expected_head[32],
    uint8_t              out_head[32],
    ct_fault_flags_t    *faults
)
{
    ax_ledger_ctx_t  c;
    ct_fault_flags_t f;
    uint64_t         i;

    if (faults == NULL) {
        return AX_CHAIN_REJECTED;
    }
    if (expected_head == NULL || (commits == NULL && n > 0U) ||
        n == UINT64_MAX) {
        faults->domain = 1;
        return AX_CHAIN_REJECTED;
    }

    ct_fault_init(&f);
    ax_ledger_genesis(&c, &f);
    for (i = 0U; i < n && !ct_fault_any(&f); i++) {
        ax_ledger_append(&c, commits[i], &f);
    }
    if (ct_fault_any(&f)) {
        faults->ledger_fail = 1;
        return AX_CHAIN_REJECTED;
    }

    return chain_verdict(&c, expected_head, out_head, faults);
}

/**
 * @brief Commit evidence record to hash
 *
 * SRS-006-SHALL-003: Chain tag separation.
 * SRS-007-SHALL-006: Evidence type validation.
 * SRS-007-SHALL-008: Input domain validation.
 */
void ax_commit_evidence(
    const ax_evidence_t  *ev,
    uint8_t               out_commit[32],
    ct_fault_flags_t     *faults
)
{
    /* Input validation (SRS-007-SHALL-008) */
    if (faults == NULL) {
        return;
    }

    if (ev == NULL) {
        faults->domain = 1;
        if (out_commit != NULL) {
            memset(out_commit, 0, 32);
        }
        return;
    }

    if (out_commit == NULL) {
        faults->domain = 1;
        return;
    }

    /* Evidence field validation (ALL REQUIRED per specification) */
    if (ev->tag == NULL) {
        faults->domain = 1;
        memset(out_commit, 0, 32);
        return;
    }

    if (ev->payload == NULL) {
        faults->domain = 1;
        memset(out_commit, 0, 32);
        return;
    }

    if (ev->payload_len == 0U) {
        faults->domain = 1;
        memset(out_commit, 0, 32);
        return;
    }

    /* Tag validation (SRS-006-SHALL-003, SRS-007-SHALL-006) */
    if (!is_valid_evidence_tag(ev->tag)) {
        faults->domain = 1;
        memset(out_commit, 0, 32);
        return;
    }

    /* Compute domain-separated commitment */
    axilog_commit(
        ev->tag,
        ev->payload,
        ev->payload_len,
        out_commit,
        faults
    );
}

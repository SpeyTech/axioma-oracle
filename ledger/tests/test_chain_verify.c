/**
 * @file test_chain_verify.c
 * @brief PCR-003: chain recomputation verifiers and their verdicts
 *
 * DVEC: v1.3
 * DETERMINISM: D1 — Strict Deterministic
 * MEMORY: Zero Dynamic Allocation
 *
 * On 5c7b05f the only check was ax_verify_chain(), which recomputes no hash:
 * reordered, truncated, inserted and substituted chains, and fabricated
 * contexts, raised no fault (finding C-21, independently reproduced). This
 * test exercises the additive verifiers that recompute from genesis, for
 * each tamper class, and asserts the verdict vocabulary:
 *
 *   REJECTED / INCONSISTENT / HEAD_MISMATCH / CONSISTENT_UNANCHORED /
 *   MATCHES_SUPPLIED_HEAD; MATCHES_AUTHENTICATED_HEAD is never returned.
 *
 * It also asserts the BUILD-MANIFEST golden values L0 and L3, which the
 * existing golden test prints but never compares.
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * @traceability SRS-006-SHALL-002, SRS-006-SHALL-003, SRS-006-SHALL-007
 */

#include <axilog/audit.h>
#include <axilog/types.h>
#include <axilog/dvec.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(name, cond) do {                         \
        tests_run++;                                   \
        if (cond) { tests_passed++; printf("  PASS %s\n", name); } \
        else { printf("  FAIL %s\n", name); }          \
    } while (0)

/* BUILD-MANIFEST.md golden references. */
static const char GOLDEN_L0[] =
    "7bb0d791697306ce2f1cc5df0bcdf66d810d6af9425aa380b352a62453a5ec7b";
static const char GOLDEN_L3[] =
    "0a6b796ca38fe030c7108e15551a05ee1628392e9a88af94ccf840b8d4605d3e";

#define N_REC 4U

static const char *const PAYLOADS[N_REC] = {
    "{\"obs\":\"request: refund 250\"}",
    "{\"policy\":\"POL-REFUND-LIMIT\",\"result\":\"PERMITTED\"}",
    "{\"action\":\"refund\",\"outcome\":\"SUCCEEDED\"}",
    "{\"state\":\"idle\"}"
};
static const char *const TAGS[N_REC] = {
    AX_TAG_OBS, AX_TAG_POLICY, AX_TAG_TRANS, AX_TAG_STATE
};

static void hex32(const uint8_t *b, char out[65])
{
    static const char h[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[2 * i] = h[b[i] >> 4];
        out[2 * i + 1] = h[b[i] & 0x0F];
    }
    out[64] = '\0';
}

static void make_records(ax_evidence_t rec[N_REC])
{
    unsigned i;
    for (i = 0; i < N_REC; i++) {
        rec[i].tag = TAGS[i];
        rec[i].payload = (const uint8_t *)PAYLOADS[i];
        rec[i].payload_len = strlen(PAYLOADS[i]);
    }
}

/* The writer's own head, built through the normal append path. */
static void writer_head(const ax_evidence_t *rec, unsigned n,
                        uint8_t commits[][32], uint8_t head[32])
{
    ax_ledger_ctx_t ctx;
    ct_fault_flags_t f;
    unsigned i;

    ct_fault_init(&f);
    ax_ledger_genesis(&ctx, &f);
    for (i = 0; i < n; i++) {
        ax_commit_evidence(&rec[i], commits[i], &f);
        ax_ledger_append(&ctx, commits[i], &f);
    }
    memcpy(head, ctx.current_hash, 32);
}

static ax_chain_verdict_t vr(const ax_evidence_t *rec, uint64_t n,
                             const uint8_t (*claimed)[32],
                             const uint8_t *expected)
{
    ct_fault_flags_t f;
    ct_fault_init(&f);
    return ax_verify_chain_records(rec, n, claimed, expected, NULL, &f);
}

int main(void)
{
    ax_evidence_t rec[N_REC], t[N_REC + 1];
    uint8_t commits[N_REC][32], tc[N_REC + 1][32], head[32], out[32];
    char hex[65];
    uint8_t mutated[64];
    ct_fault_flags_t f;
    ax_ledger_ctx_t fake;

    printf("\n=== PCR-003 chain verification ===\n");
    make_records(rec);
    writer_head(rec, N_REC, commits, head);

    /* Golden values */
    ct_fault_init(&f);
    ax_verify_chain_commits(NULL, 0, head, out, &f);
    hex32(out, hex);
    CHECK("genesis head equals BUILD-MANIFEST L0", strcmp(hex, GOLDEN_L0) == 0);
    {
        uint8_t g[3][32];
        unsigned i;
        for (i = 0; i < 32; i++) {
            g[0][i] = (uint8_t)i;
            g[1][i] = (uint8_t)(0xFF - i);
        }
        {
            static const uint8_t c3[32] = {
                0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
                0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
            };
            memcpy(g[2], c3, 32);
        }
        ct_fault_init(&f);
        ax_verify_chain_commits((const uint8_t (*)[32])g, 3, head, out, &f);
        hex32(out, hex);
        CHECK("three fixed commits give BUILD-MANIFEST L3", strcmp(hex, GOLDEN_L3) == 0);
    }

    /* Genuine chain */
    CHECK("genuine chain, writer head -> MATCHES_SUPPLIED_HEAD",
          vr(rec, N_REC, (const uint8_t (*)[32])commits, head) == AX_CHAIN_MATCHES_SUPPLIED_HEAD);
    CHECK("genuine chain, no head -> CONSISTENT_UNANCHORED",
          vr(rec, N_REC, NULL, NULL) == AX_CHAIN_CONSISTENT_UNANCHORED);
    ct_fault_init(&f);
    CHECK("genuine commits, writer head -> MATCHES_SUPPLIED_HEAD",
          ax_verify_chain_commits((const uint8_t (*)[32])commits, N_REC, head, NULL, &f)
              == AX_CHAIN_MATCHES_SUPPLIED_HEAD);

    /* Mutated payload byte */
    memcpy(t, rec, sizeof rec);
    memcpy(mutated, PAYLOADS[1], strlen(PAYLOADS[1]));
    mutated[5] ^= 0x01;
    t[1].payload = mutated;
    CHECK("mutated payload vs writer head -> HEAD_MISMATCH",
          vr(t, N_REC, NULL, head) == AX_CHAIN_HEAD_MISMATCH);
    CHECK("mutated payload vs its claimed commit -> INCONSISTENT",
          vr(t, N_REC, (const uint8_t (*)[32])commits, head) == AX_CHAIN_INCONSISTENT);

    /* Reordered pair */
    memcpy(t, rec, sizeof rec);
    t[1] = rec[2];
    t[2] = rec[1];
    CHECK("reordered records -> HEAD_MISMATCH", vr(t, N_REC, NULL, head) == AX_CHAIN_HEAD_MISMATCH);

    /* Deleted middle record */
    t[0] = rec[0]; t[1] = rec[2]; t[2] = rec[3];
    CHECK("deleted record -> HEAD_MISMATCH", vr(t, N_REC - 1, NULL, head) == AX_CHAIN_HEAD_MISMATCH);

    /* Inserted record */
    t[0] = rec[0]; t[1] = rec[1]; t[2] = rec[1]; t[3] = rec[2]; t[4] = rec[3];
    CHECK("inserted record -> HEAD_MISMATCH", vr(t, N_REC + 1, NULL, head) == AX_CHAIN_HEAD_MISMATCH);

    /* Truncated tail */
    CHECK("truncated tail -> HEAD_MISMATCH", vr(rec, N_REC - 1, NULL, head) == AX_CHAIN_HEAD_MISMATCH);

    /* Substituted record with consistent commitments throughout: only a
     * trusted head can detect it. */
    memcpy(t, rec, sizeof rec);
    t[1].payload = (const uint8_t *)"{\"policy\":\"POL-REFUND-LIMIT\",\"result\":\"BREACH\"}";
    t[1].payload_len = strlen((const char *)t[1].payload);
    writer_head(t, N_REC, tc, out);
    CHECK("consistent rewrite, own commits, no head -> CONSISTENT_UNANCHORED",
          vr(t, N_REC, (const uint8_t (*)[32])tc, NULL) == AX_CHAIN_CONSISTENT_UNANCHORED);
    CHECK("consistent rewrite, head from the rewritten bundle -> MATCHES_SUPPLIED_HEAD",
          vr(t, N_REC, (const uint8_t (*)[32])tc, out) == AX_CHAIN_MATCHES_SUPPLIED_HEAD);
    CHECK("consistent rewrite vs original writer head -> HEAD_MISMATCH",
          vr(t, N_REC, (const uint8_t (*)[32])tc, head) == AX_CHAIN_HEAD_MISMATCH);

    /* Malformed input */
    memcpy(t, rec, sizeof rec);
    t[2].tag = "AX:NOT:REGISTERED";
    CHECK("unregistered tag -> REJECTED", vr(t, N_REC, NULL, head) == AX_CHAIN_REJECTED);
    memcpy(t, rec, sizeof rec);
    t[0].payload_len = 0;
    CHECK("empty payload -> REJECTED", vr(t, N_REC, NULL, head) == AX_CHAIN_REJECTED);
    CHECK("NULL records with n > 0 -> REJECTED", vr(NULL, 1, NULL, head) == AX_CHAIN_REJECTED);
    CHECK("n == UINT64_MAX -> REJECTED", vr(rec, UINT64_MAX, NULL, head) == AX_CHAIN_REJECTED);
    ct_fault_init(&f);
    CHECK("commits variant without expected head -> REJECTED",
          ax_verify_chain_commits((const uint8_t (*)[32])commits, N_REC, NULL, NULL, &f)
              == AX_CHAIN_REJECTED && f.domain);

    /* The old context check still accepts a fabricated context; the new
     * name has identical behaviour. Recorded so the limitation stays visible. */
    memset(&fake, 0, sizeof fake);
    fake.initialised = 1;
    ct_fault_init(&f);
    ax_ledger_check_context(&fake, &f);
    CHECK("ax_ledger_check_context: fabricated context raises no fault (documented limit)",
          !ct_fault_any(&f));

    printf("\nResults: %d/%d checks passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

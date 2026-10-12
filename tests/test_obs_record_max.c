/**
 * @file test_obs_record_max.c
 * @brief SRS-004-SHALL-048: the complete AX:OBS:v1 record never exceeds AX_MAX_OBS_BYTES
 *
 * On 57ec02b (and on the first PCR-001 commit) admission bounded only the
 * raw output, so an output whose escaped form was large was admitted as
 * COMPLETE with a record of up to AX_CANONICAL_BUFFER_SIZE. Owner decision
 * 2026-10-12: enforce the 64 KiB limit on the complete stored record
 * (canonical form including obs_hash) and reject an oversized record with
 * AX_ERR_SIZE, never truncating or relabelling it.
 *
 * Fails on 57ec02b; passes with the enforcement.
 *
 * SRS-004-SHALL-048, SRS-004-SHALL-040
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"
#include "axilog/limits.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { printf("  [TEST] %s... ", name); tests_run++; } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static char out[AX_MAX_OUTPUT_BYTES + 6000];
static char norm_buf[AX_MAX_OUTPUT_BYTES + 1];
static char canon[AX_CANONICAL_BUFFER_SIZE];

static int admit(ax_obs_record_t *rec, size_t len, ax_l3_fault_flags_t *faults)
{
    ax_admission_ctx_t ctx;
    ax_obs_input_t in;

    ax_admission_ctx_init(&ctx);
    memset(&in, 0, sizeof in);
    memset(faults, 0, sizeof *faults);
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.input = "prompt";
    in.input_len = 6;
    in.ledger_seq = 1;
    in.model_id = "m";
    in.oracle_id = "o";
    in.output = out;
    in.output_len = len;
    ax_oracle_params_init_null(&in.params);
    ax_obs_init(rec);
    return ax_obs_admit(rec, norm_buf, sizeof norm_buf, &in, &ctx, faults);
}

static int record_len(const ax_obs_record_t *rec)
{
    return ax_obs_canonicalise(canon, sizeof canon, rec, 1);
}

/* 1. An escape-heavy output whose record would exceed the limit is rejected. */
static void test_oversized_record_rejected(void)
{
    ax_obs_record_t rec;
    ax_l3_fault_flags_t f;
    int rc;

    TEST("12,000 LF output (record ~72 KB) rejected with AX_ERR_SIZE");
    memset(out, '\n', 12000);
    out[12000] = '\0';
    rc = admit(&rec, 12000, &f);
    if (rc != AX_ERR_SIZE) { FAIL("not rejected with AX_ERR_SIZE"); return; }
    if (!f.size) { FAIL("size fault not set"); return; }
    if (rec.completion_state != AX_COMPLETION_ERROR) { FAIL("state not ERROR"); return; }
    if (rec.obs_hash[0] != '\0') { FAIL("rejected record carries an obs_hash"); return; }
    PASS();
}

/* 2. Boundary: every admitted record is within the limit, and the limit is reachable. */
static void test_boundary(void)
{
    ax_obs_record_t rec;
    ax_l3_fault_flags_t f;
    size_t n, lo = 0, hi = 13000;
    int len;

    TEST("largest admissible LF output gives a record of at most AX_MAX_OBS_BYTES; +1 LF is rejected");
    /* Binary search on LF count for the largest admissible output. */
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        memset(out, '\n', mid); out[mid] = '\0';
        if (admit(&rec, mid, &f) == AX_OK) lo = mid; else hi = mid - 1;
    }
    n = lo;
    memset(out, '\n', n); out[n] = '\0';
    if (admit(&rec, n, &f) != AX_OK) { FAIL("boundary not admitted"); return; }
    len = record_len(&rec);
    if (len <= 0 || len > AX_MAX_OBS_BYTES || len < AX_MAX_OBS_BYTES - 6) {
        printf("(n=%zu len=%d) ", n, len); FAIL("boundary record length wrong"); return;
    }
    memset(out, '\n', n + 1); out[n + 1] = '\0';
    if (admit(&rec, n + 1, &f) != AX_ERR_SIZE) { FAIL("n+1 not rejected"); return; }
    printf("(max %zu LF, record %d bytes) ", n, len);
    PASS();
}

/* 3. SHALL-040 truncation of an over-long plain output is unchanged and in bounds. */
static void test_truncated_plain_output_in_bounds(void)
{
    ax_obs_record_t rec;
    ax_l3_fault_flags_t f;
    const size_t n = AX_MAX_OUTPUT_BYTES + 5000;
    int len;

    TEST("plain output over AX_MAX_OUTPUT_BYTES: TRUNCATED record within the limit");
    memset(out, 'A', n);
    out[n] = '\0';
    if (admit(&rec, n, &f) != AX_OK) { FAIL("not admitted"); return; }
    if (rec.completion_state != AX_COMPLETION_TRUNCATED || rec.output_size != n) {
        FAIL("TRUNCATED/output_size semantics changed"); return;
    }
    len = record_len(&rec);
    if (len <= 0 || len > AX_MAX_OBS_BYTES) { FAIL("record over limit"); return; }
    PASS();
}

/* 4. Sweep: no admitted record anywhere exceeds the limit. */
static void test_sweep(void)
{
    static const size_t counts[] = { 0, 100, 5000, 10000, 10750, 10800, 11000,
                                     12000, 21504, 30000, AX_MAX_OUTPUT_BYTES - 1 };
    ax_obs_record_t rec;
    ax_l3_fault_flags_t f;
    size_t i;

    TEST("sweep: admitted records never exceed AX_MAX_OBS_BYTES");
    for (i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        memset(out, '\n', counts[i]);
        out[counts[i]] = 'x';
        out[counts[i] + 1] = '\0';
        if (admit(&rec, counts[i] + 1, &f) == AX_OK && record_len(&rec) > AX_MAX_OBS_BYTES) {
            printf("(count %zu) ", counts[i]); FAIL("admitted record over limit"); return;
        }
    }
    PASS();
}

int main(void)
{
    printf("\n=== SRS-004-SHALL-048 complete-record limit ===\n");
    test_oversized_record_rejected();
    test_boundary();
    test_truncated_plain_output_in_bounds();
    test_sweep();
    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

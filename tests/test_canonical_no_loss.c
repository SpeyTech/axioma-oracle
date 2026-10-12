/**
 * @file test_canonical_no_loss.c
 * @brief PCR-001: the canonical AX:OBS:v1 record never silently loses content
 *
 * On 57ec02b, ax_obs_canonicalise escaped each string field into an
 * intermediate buffer and wrote nothing when escaping overflowed it, while
 * still returning success. A newline-dense output therefore became
 * "output":"" and two different outputs of equal length received the same
 * obs_hash; identifiers longer than the buffer were dropped the same way.
 *
 * These tests fail on 57ec02b and pass with the fix. They require that an
 * admitted record contains every byte of every field, and that anything
 * that cannot be serialised in full is rejected.
 *
 * SRS-004-SHALL-006, SRS-004-SHALL-040, SRS-004-SHALL-045, SRS-004-SHALL-046
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

static char out_a[AX_MAX_OUTPUT_BYTES + 1];
static char out_b[AX_MAX_OUTPUT_BYTES + 1];
static char norm_buf[AX_MAX_OUTPUT_BYTES + 1];
static char canon[AX_CANONICAL_BUFFER_SIZE];
static char id_buf[AX_MAX_MODEL_ID_BYTES + 2];

static int admit(ax_obs_record_t *rec, const char *out, size_t len,
                 const char *model_id, const char *oracle_id)
{
    ax_admission_ctx_t ctx;
    ax_obs_input_t in;
    ax_l3_fault_flags_t faults;

    ax_admission_ctx_init(&ctx);
    memset(&in, 0, sizeof in);
    memset(&faults, 0, sizeof faults);
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.input = "prompt";
    in.input_len = 6;
    in.ledger_seq = 1;
    in.model_id = model_id;
    in.oracle_id = oracle_id;
    in.output = out;
    in.output_len = len;
    ax_oracle_params_init_null(&in.params);
    ax_obs_init(rec);
    return ax_obs_admit(rec, norm_buf, sizeof norm_buf, &in, &ctx, &faults);
}

/* JSON-escaped length per SHALL-045. */
static size_t escaped_len(const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        n += (c == '"' || c == '\\') ? 2u : (c < 0x20 ? 6u : 1u);
    }
    return n;
}

/* An admitted record must contain every field in full: its canonical length
 * equals the empty-field record length plus the escaped field lengths. */
static int record_is_complete(const ax_obs_record_t *rec)
{
    ax_obs_record_t empty = *rec;
    char small[1024];
    int full, base;

    full = ax_obs_canonicalise(canon, sizeof canon, rec, 1);
    empty.output = "";
    empty.model_id = "";
    empty.oracle_id = "";
    base = ax_obs_canonicalise(small, sizeof small, &empty, 1);
    if (full < 0 || base < 0) {
        return 0;
    }
    return (size_t)full == (size_t)base + escaped_len(rec->output) +
                           escaped_len(rec->model_id) + escaped_len(rec->oracle_id);
}

/* 1. Equal-length outputs that differ only in the last byte, past the old
 *    drop threshold (21,504 LF). */
static void test_distinct_outputs_distinct_hashes(void)
{
    ax_obs_record_t a, b;
    char hash_a[AX_HASH_HEX_SIZE];
    const size_t n = 21504;
    int ra, rb;

    TEST("distinct newline-dense outputs never share obs_hash");
    memset(out_a, '\n', n); out_a[n] = 'a'; out_a[n + 1] = '\0';
    memset(out_b, '\n', n); out_b[n] = 'b'; out_b[n + 1] = '\0';
    ra = admit(&a, out_a, n + 1, "m", "o");
    if (ra == AX_OK && !record_is_complete(&a)) { FAIL("record A lost content"); return; }
    memcpy(hash_a, a.obs_hash, sizeof hash_a);
    rb = admit(&b, out_b, n + 1, "m", "o");
    if (rb == AX_OK && !record_is_complete(&b)) { FAIL("record B lost content"); return; }
    if (ra == AX_OK && rb == AX_OK && strcmp(hash_a, b.obs_hash) == 0) {
        FAIL("distinct outputs share obs_hash");
        return;
    }
    PASS();
}

/* 2. Boundary sweep: every admitted record is complete; nothing in between. */
static void test_newline_sweep_complete_or_rejected(void)
{
    static const size_t counts[] = { 0, 1, 1000, 10000, 21400, 21503, 21504,
                                     21505, 21566, 21567, 21600, 30000,
                                     AX_MAX_OUTPUT_BYTES - 1 };
    ax_obs_record_t rec;
    size_t i;

    TEST("newline sweep: admitted records are complete, others rejected");
    for (i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        memset(out_a, '\n', counts[i]);
        out_a[counts[i]] = 'x';
        out_a[counts[i] + 1] = '\0';
        if (admit(&rec, out_a, counts[i] + 1, "m", "o") == AX_OK &&
            !record_is_complete(&rec)) {
            printf("(count %zu) ", counts[i]);
            FAIL("admitted record is incomplete");
            return;
        }
    }
    PASS();
}

/* 3. Worst case: an all-control-character output at the size limit. */
static void test_worst_case_not_lossy(void)
{
    ax_obs_record_t rec;

    TEST("all-control-character output at AX_MAX_OUTPUT_BYTES is not lossy");
    memset(out_a, '\n', AX_MAX_OUTPUT_BYTES);
    out_a[AX_MAX_OUTPUT_BYTES] = '\0';
    if (admit(&rec, out_a, AX_MAX_OUTPUT_BYTES, "m", "o") == AX_OK &&
        !record_is_complete(&rec)) {
        FAIL("admitted incomplete record");
        return;
    }
    PASS();
}

/* 4. Identifier bounds are enforced, never dropped. */
static void test_identifier_bounds(void)
{
    ax_obs_record_t rec;

    TEST("identifiers above the limit are rejected; at the limit admitted in full");
    memset(id_buf, 'm', AX_MAX_MODEL_ID_BYTES);
    id_buf[AX_MAX_MODEL_ID_BYTES] = '\0';
    if (admit(&rec, "hello", 5, id_buf, "o") != AX_OK || !record_is_complete(&rec)) {
        FAIL("model_id at limit not admitted in full"); return;
    }
    if (admit(&rec, "hello", 5, "m", id_buf) != AX_OK || !record_is_complete(&rec)) {
        FAIL("oracle_id at limit not admitted in full"); return;
    }
    id_buf[AX_MAX_MODEL_ID_BYTES] = 'm';
    id_buf[AX_MAX_MODEL_ID_BYTES + 1] = '\0';
    if (admit(&rec, "hello", 5, id_buf, "o") != AX_ERR_SIZE) {
        FAIL("over-long model_id not rejected"); return;
    }
    if (admit(&rec, "hello", 5, "m", id_buf) != AX_ERR_SIZE) {
        FAIL("over-long oracle_id not rejected"); return;
    }
    PASS();
}

/* 5. Fault injection: a destination too small for the record is an error,
 *    never a shortened record. */
static void test_small_destination_is_error(void)
{
    ax_obs_record_t rec;
    char tiny[300];

    TEST("canonicalise into a short buffer returns an error");
    if (admit(&rec, "hello world", 11, "m", "o") != AX_OK) { FAIL("admit"); return; }
    if (ax_obs_canonicalise(tiny, sizeof tiny, &rec, 1) >= 0) {
        FAIL("short destination accepted");
        return;
    }
    PASS();
}

int main(void)
{
    printf("\n=== PCR-001 canonical record completeness ===\n");
    test_distinct_outputs_distinct_hashes();
    test_newline_sweep_complete_or_rejected();
    test_worst_case_not_lossy();
    test_identifier_bounds();
    test_small_destination_is_error();
    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

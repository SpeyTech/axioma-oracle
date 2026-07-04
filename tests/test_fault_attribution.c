/**
 * @file test_fault_attribution.c
 * @brief E-ABI-1 regression: substrate fault attribution lands in the
 *        right field when the struct crosses the axilog_commit boundary
 *        from an oracle translation unit.
 *
 * Copyright (c) 2026 Spey Systems LTD
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The defect (CONFORMANCE.md section 12, E-ABI-1): the oracle's former
 * axilog/types.h redefined ct_fault_flags_t (16 bytes, domain at offset
 * 3) under the substrate's guard and path. libaxilog, compiled against
 * the substrate's 8-byte layout, wrote domain at ITS offset 5, which the
 * oracle read as encoding. Nothing in the suite exercised the fault path,
 * so misattribution went undetected until the gateway delivery review.
 *
 * This test exercises exactly that path, compiled with the oracle's
 * include set, and proves:
 *   1. The type this TU sees IS the substrate's 8-byte layout.
 *   2. A forced commit fault (payload NULL, payload_len > 0, the
 *      substrate's documented domain condition) sets faults.domain and
 *      nothing else.
 *   3. The L3 wrapper's base is at offset 0 and receives the same
 *      attribution when its base crosses the boundary.
 */

#include "axilog/l3_types.h"
#include "axilog/commitment.h"
#include <stdio.h>
#include <stddef.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name)  printf("  %s\n", name)
#define PASS()      do { printf("    PASS\n"); g_pass++; } while (0)
#define FAIL(msg)   do { printf("    FAIL: %s\n", msg); g_fail++; } while (0)

/* Test 1: this TU's view of ct_fault_flags_t is the substrate layout.
 * Compile-time asserts in l3_types.h already pin sizeof == 8; this
 * confirms field offsets at runtime for the report. */
static void test_substrate_layout_adopted(void)
{
    TEST("substrate_layout_adopted");
    if (sizeof(ct_fault_flags_t) == 8 &&
        offsetof(ct_fault_flags_t, domain)      == 5 &&
        offsetof(ct_fault_flags_t, ledger_fail) == 6) {
        PASS();
    } else {
        FAIL("ct_fault_flags_t is not the substrate 8-byte layout");
        printf("    sizeof=%zu domain@%zu\n",
               sizeof(ct_fault_flags_t),
               offsetof(ct_fault_flags_t, domain));
    }
}

/* Test 2: forced commit fault attributes to domain, and only domain.
 * Under E-ABI-1 the write landed in the oracle-view encoding field and
 * domain stayed clear; this is the direct regression check. */
static void test_commit_fault_lands_in_domain(void)
{
    ct_fault_flags_t f;
    uint8_t out[32];

    TEST("commit_fault_lands_in_domain");

    ct_fault_init(&f);
    /* payload NULL with payload_len > 0: substrate sets faults->domain,
     * zeroes out_commit, returns. */
    axilog_commit("AX:OBS:v1", NULL, 1u, out, &f);

    if (f.domain == 1 &&
        f.overflow == 0 && f.underflow == 0 && f.div_zero == 0 &&
        f.saturation == 0 && f.narrowing == 0 && f.ledger_fail == 0 &&
        ct_fault_any(&f)) {
        PASS();
    } else {
        FAIL("fault misattributed");
        printf("    domain=%u overflow=%u underflow=%u div_zero=%u "
               "saturation=%u narrowing=%u ledger_fail=%u\n",
               f.domain, f.overflow, f.underflow, f.div_zero,
               f.saturation, f.narrowing, f.ledger_fail);
    }
}

/* Test 3: out_commit is zeroed on the fault path, per the commitment.h
 * postcondition. Attribution and the zeroing contract stand together. */
static void test_commit_fault_zeroes_output(void)
{
    ct_fault_flags_t f;
    uint8_t out[32];
    int i, nonzero = 0;

    TEST("commit_fault_zeroes_output");

    for (i = 0; i < 32; i++) out[i] = 0xAA;
    ct_fault_init(&f);
    axilog_commit("AX:OBS:v1", NULL, 1u, out, &f);
    for (i = 0; i < 32; i++) nonzero |= out[i];

    if (nonzero == 0) {
        PASS();
    } else {
        FAIL("out_commit not zeroed on fault");
    }
}

/* Test 4: the L3 wrapper's base crosses the boundary and receives the
 * attribution; the L3-specific flags stay untouched. This is the
 * load-bearing offset-0 property from l3_types.h. */
static void test_l3_base_crossing(void)
{
    ax_l3_fault_flags_t lf;
    uint8_t out[32];

    TEST("l3_base_crossing");

    ax_l3_fault_init(&lf);
    axilog_commit("AX:OBS:v1", NULL, 1u, out, &lf.base);

    if (lf.base.domain == 1 &&
        lf.precision == 0 && lf.encoding == 0 && lf.schema == 0 &&
        lf.ordering == 0 && lf.size == 0 && lf.protocol == 0 &&
        ax_l3_fault_any(&lf)) {
        PASS();
    } else {
        FAIL("base crossing misattributed or leaked into L3 flags");
        printf("    base.domain=%u encoding=%u\n",
               lf.base.domain, lf.encoding);
    }
}

/* Test 5: the green path writes nothing. Confirms the erratum's honest
 * qualification that conformance statements 1-11 were unaffected. */
static void test_green_path_writes_nothing(void)
{
    ct_fault_flags_t f;
    uint8_t out[32];
    static const uint8_t payload[] = "attribution";

    TEST("green_path_writes_nothing");

    ct_fault_init(&f);
    axilog_commit("AX:OBS:v1", payload, sizeof(payload) - 1u, out, &f);

    if (!ct_fault_any(&f)) {
        PASS();
    } else {
        FAIL("green path set a fault flag");
    }
}

int main(void)
{
    printf("test_fault_attribution (E-ABI-1 regression)\n");

    test_substrate_layout_adopted();
    test_commit_fault_lands_in_domain();
    test_commit_fault_zeroes_output();
    test_l3_base_crossing();
    test_green_path_writes_nothing();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

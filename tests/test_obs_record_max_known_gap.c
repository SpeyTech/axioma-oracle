/**
 * @file test_obs_record_max_known_gap.c
 * @brief KNOWN GAP (tracked, not fixed): admitted records can exceed AX_MAX_OBS_BYTES
 *
 * SRS-004-SHALL-048 sets the maximum size of an AX:OBS:v1 record at
 * AX_MAX_OBS_BYTES (64 KiB). Admission bounds the raw output at
 * AX_MAX_OUTPUT_BYTES but not the serialised record, so an output with
 * enough characters that escape to six bytes is admitted as COMPLETE with
 * a canonical record of up to AX_CANONICAL_BUFFER_SIZE. This is true on
 * 57ec02b and after PCR-001.
 *
 * Enforcing the 64 KiB record bound changes which observations are
 * admitted (some previously admitted, complete records would become
 * TRUNCATED or rejected), so it is a protocol decision awaiting owner
 * approval, not part of PCR-001.
 *
 * This test asserts the specified bound and is registered with WILL_FAIL:
 * CTest reports it as passing while the gap exists, and reports a failure
 * the moment the gap is closed, so the registration must then be updated.
 * It is never skipped and never marked continue-on-error.
 *
 * SRS-004-SHALL-048
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"
#include "axilog/limits.h"

static char out[AX_MAX_OUTPUT_BYTES + 1];
static char norm_buf[AX_MAX_OUTPUT_BYTES + 1];
static char canon[AX_CANONICAL_BUFFER_SIZE];

int main(void)
{
    ax_admission_ctx_t ctx;
    ax_obs_input_t in;
    ax_obs_record_t rec;
    ax_l3_fault_flags_t faults;
    const size_t n = 12000;   /* 12,000 LF escape to 72,000 bytes */
    int rc, len;

    memset(out, '\n', n);
    out[n] = '\0';
    ax_admission_ctx_init(&ctx);
    memset(&in, 0, sizeof in);
    memset(&faults, 0, sizeof faults);
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.input = "prompt";
    in.input_len = 6;
    in.ledger_seq = 1;
    in.model_id = "m";
    in.oracle_id = "o";
    in.output = out;
    in.output_len = n;
    ax_oracle_params_init_null(&in.params);
    ax_obs_init(&rec);

    rc = ax_obs_admit(&rec, norm_buf, sizeof norm_buf, &in, &ctx, &faults);
    if (rc != AX_OK) {
        printf("record not admitted (rc=%d): SHALL-048 bound holds\n", rc);
        return 0;
    }
    len = ax_obs_canonicalise(canon, sizeof canon, &rec, 1);
    printf("admitted COMPLETE record of %d bytes; SHALL-048 maximum is %d\n",
           len, AX_MAX_OBS_BYTES);
    if (len > AX_MAX_OBS_BYTES && rec.completion_state == AX_COMPLETION_COMPLETE) {
        printf("KNOWN GAP: record exceeds AX_MAX_OBS_BYTES without TRUNCATED\n");
        return 1;
    }
    return 0;
}

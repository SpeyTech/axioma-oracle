/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_fixture.c - the cross-ISA acceptance in fixture form.
 *
 * A captured response body (fixture) runs the full evidence path:
 * extract, admit, validate, canonicalise. The canonical bytes and the
 * obs_hash must be identical on aarch64 and x86_64; the test prints
 * both so the two runs can be compared, and pins them against frozen
 * values once the first real capture replaces the hand-built fixture
 * (final acceptance item). TU group A.
 */
#include "gw_json.h"

#include <axilog/oracle.h>
#include <axilog/obs.h>
#include <axilog/hash.h>
#include <axilog/canonical.h>
#include <axilog/limits.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-44s PASS\n", name); \
    else { printf("  %-44s FAIL\n", name); failures++; } \
} while (0)

/* The first real captured body: axioma, 2 July 2026, Haiku-class
 * shakedown call, obs seq 5 on the production chain. Contains a raw
 * UTF-8 em dash and markdown, which is exactly why captured bytes
 * beat hand-built ones. */
static const char FIXTURE[] =
    "{\"model\":\"claude-haiku-4-5-20251001\",\"id\":\"msg_01NaB1C2u9"
    "q2hmjtZ4hfVM8n\",\"type\":\"message\",\"role\":\"assistant\",\"c"
    "ontent\":[{\"type\":\"text\",\"text\":\"# Deterministic Evidence"
    " Chain Layers\\n\\nA deterministic evidence chain consists of: *"
    "*collection, preservation, analysis, and presentation** \xe2\x80"
    "\x94 each layer building upon the previous to maintain integrity"
    " and establish conclusive proof.\"}],\"stop_reason\":\"end_turn\""
    ",\"stop_sequence\":null,\"stop_details\":null,\"usage\":{\"input"
    "_tokens\":19,\"cache_creation_input_tokens\":0,\"cache_read_inpu"
    "t_tokens\":0,\"cache_creation\":{\"ephemeral_5m_input_tokens\":0"
    ",\"ephemeral_1h_input_tokens\":0},\"output_tokens\":46,\"service"
    "_tier\":\"standard\",\"inference_geo\":\"not_available\"}}";

static const char PROMPT[] =
    "state the layers of a deterministic evidence chain in one sentence";

int main(void)
{
    char text[4096];
    size_t tlen = 0;
    char stop[32] = {0};
    static char output_buf[AX_MAX_OUTPUT_BYTES];
    static char canon[AX_CANONICAL_BUFFER_SIZE];
    ax_obs_input_t in;
    ax_obs_record_t rec;
    ax_admission_ctx_t adm;
    ct_fault_flags_t faults;
    int clen;

    printf("gateway: test_gw_fixture\n");

    CHECK(gwj_content_text(FIXTURE, sizeof FIXTURE - 1,
                           text, sizeof text, &tlen) == 0, "fixture extracts");
    CHECK(gwj_top_string(FIXTURE, sizeof FIXTURE - 1, "stop_reason",
                         stop, sizeof stop) == 0 &&
          strcmp(stop, "end_turn") == 0, "fixture stop_reason");

    memset(&in, 0, sizeof in);
    in.completion_state = AX_COMPLETION_COMPLETE;
    in.failure_type     = AX_FAILURE_NULL;
    in.input            = PROMPT;
    in.input_len        = sizeof PROMPT - 1;
    in.ledger_seq       = 1;
    in.model_id         = "claude-haiku-4-5-20251001";
    in.oracle_id        = "anthropic-messages-api";
    in.output           = text;
    in.output_len       = tlen;
    ax_oracle_params_init_null(&in.params);
    in.params.max_tokens = 64;

    ax_admission_ctx_init(&adm);
    ax_obs_init(&rec);
    memset(&faults, 0, sizeof faults);
    CHECK(ax_obs_admit(&rec, output_buf, sizeof output_buf,
                       &in, &adm, &faults) == AX_OK, "fixture admits");
    memset(&faults, 0, sizeof faults);
    CHECK(ax_obs_validate(&rec, &faults) == AX_OK, "fixture validates");

    clen = ax_obs_canonicalise(canon, sizeof canon, &rec, 1);
    CHECK(clen > 0, "fixture canonicalises");

    /* Cross-ISA pin: x86_64 container value, 2 July 2026. An ARM64 run
     * that disagrees is a finding, not a flake. */
    CHECK(clen == 675, "canonical length pinned");
    CHECK(strcmp(rec.obs_hash,
        "974c53ff9d238999174a286498442d53e113d7523ae6b8cdde08e9a52397e856") == 0,
        "obs_hash pinned cross-ISA");
    printf("  obs_hash: %s\n", rec.obs_hash);

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_main.c - the L3 gateway service.
 *
 * TU group A: compiles against axioma-oracle (and via it the substrate
 * headers). The durable chain is reached only through gw_ledger.h,
 * which passes bare bytes. See the ABI note in the delivery report.
 *
 * Startup: config -> log -> ledger open/replay -> WAL recovery ->
 * AX:STATE:v1 config commitment -> listen. Serial accept loop, one
 * request in flight, per the brief.
 *
 * Request path implements the brief's steps 1-10 with the spend guard
 * ahead of the oracle call. Budget refusal is pre-spend and log-only,
 * consistent with the encoding reject class: only attempts that reach
 * the oracle produce chain observations.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_config.h"
#include "gw_log.h"
#include "gw_wal.h"
#include "gw_ledger.h"
#include "gw_client.h"
#include "gw_json.h"
#include "gw_notify.h"

#include <axilog/oracle.h>
#include <axilog/obs.h>
#include <axilog/validate.h>
#include <axilog/hash.h>
#include <axilog/canonical.h>
#include <axilog/limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Evidence tag string. gw_ledger validates it against the registry on
 * the audit side; this TU must not include audit headers. */
#define GW_TAG_OBS   "AX:OBS:v1"
#define GW_TAG_STATE "AX:STATE:v1"

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ---- small helpers ----------------------------------------------------- */

static void hex32(char out[65], const uint8_t in[32])
{
    static const char hd[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[i*2]   = hd[(in[i] >> 4) & 0xF];
        out[i*2+1] = hd[in[i] & 0xF];
    }
    out[64] = '\0';
}

/* q16.16 to decimal string, integer arithmetic only, 6 dp, no trailing
 * zero trimming (deterministic formatting). Non-negative values only. */
static int q16_to_dec(char *dst, size_t cap, int32_t q)
{
    uint32_t v, ip, fp;
    if (q < 0) return -1;
    v = (uint32_t)q;
    ip = v >> 16;
    fp = (uint32_t)(((uint64_t)(v & 0xFFFFu) * 1000000u) >> 16);
    return (snprintf(dst, cap, "%u.%06u", ip, fp) < (int)cap) ? 0 : -1;
}

/* ---- line protocol ------------------------------------------------------
 * Request:  "key: value\n" lines, blank line, then prompt_len raw bytes.
 *   Keys: prompt_len (required), max_tokens, temperature_q16, top_p_q16.
 *   seed is rejected: the Messages API carries no seed parameter and
 *   recording a param not sent would be false evidence.
 * Response: "key: value\n" lines, blank line, then output_len raw bytes.
 */

static int read_line(int fd, char *buf, size_t cap)
{
    size_t o = 0;
    while (o + 1 < cap) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n') { buf[o] = '\0'; return 0; }
        buf[o++] = c;
    }
    return -1;
}

static int write_all_fd(int fd, const void *p, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t w = write(fd, (const char *)p + o, n - o);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        o += (size_t)w;
    }
    return 0;
}

static void proto_error(int fd, const char *reason)
{
    char line[256];
    int n = snprintf(line, sizeof line, "status: error\nreason: %s\n\n", reason);
    if (n > 0) (void)write_all_fd(fd, line, (size_t)n);
}

/* ---- observation assembly ---------------------------------------------- */

typedef struct {
    gw_config_t        *cfg;
    ax_admission_ctx_t  adm;
    long                calls_made;   /* spend guard */
    char               *prompt;       /* malloc'd, max_prompt_bytes */
    char               *norm;         /* normalised prompt */
    char               *escaped;      /* escaped prompt for request JSON */
    char               *reqjson;      /* request body */
} gw_state_t;

static char g_output_buf[AX_MAX_OUTPUT_BYTES];
static char g_canon_buf[AX_CANONICAL_BUFFER_SIZE];
static char g_text_buf[AX_MAX_OUTPUT_BYTES];

/* Build, admit, validate, canonicalise, append. Returns 0 and fills
 * obs_hash_hex/head_hex/seq on success; -1 on internal fault (alarmed:
 * the wrapper disagreeing with itself). */
static int commit_observation(gw_state_t *st,
                              ax_completion_state_t state,
                              ax_failure_type_t ftype,
                              const char *norm_prompt, size_t norm_len,
                              const char *output, size_t output_len,
                              const ax_oracle_params_t *params,
                              char obs_hash_hex[AX_HASH_HEX_SIZE],
                              char head_hex[65], uint64_t *seq_out)
{
    ax_obs_input_t in;
    ax_obs_record_t rec;
    ax_l3_fault_flags_t faults;
    int rc, clen;
    uint8_t head[32];

    memset(&in, 0, sizeof in);
    in.completion_state = state;
    in.failure_type     = ftype;
    in.input            = norm_prompt;
    in.input_len        = norm_len;
    in.ledger_seq       = gwl_seq() + 1u;
    in.model_id         = st->cfg->model_id;
    in.oracle_id        = st->cfg->oracle_id;
    in.output           = output;
    in.output_len       = output_len;
    in.params           = *params;

    ax_obs_init(&rec);
    memset(&faults, 0, sizeof faults);
    rc = ax_obs_admit(&rec, g_output_buf, sizeof g_output_buf,
                      &in, &st->adm, &faults);
    if (rc != AX_OK) return -1;

    memset(&faults, 0, sizeof faults);
    rc = ax_obs_validate(&rec, &faults);
    if (rc != AX_OK) return -1;

    clen = ax_obs_canonicalise(g_canon_buf, sizeof g_canon_buf, &rec, 1);
    if (clen <= 0) return -1;

    if (gwl_append(GW_TAG_OBS, (const uint8_t *)g_canon_buf,
                   (uint64_t)clen, head, seq_out) != 0) return -1;

    memcpy(obs_hash_hex, rec.obs_hash, AX_HASH_HEX_SIZE);
    hex32(head_hex, head);
    return 0;
}

/* ---- one request --------------------------------------------------------- */

static void handle_request(gw_state_t *st, int cfd, const char *api_key)
{
    char line[256];
    long prompt_len = -1;
    long max_tokens = st->cfg->default_max_tokens;
    long temp_q16 = -1, top_p_q16 = -1;   /* -1 = null here; q16 >= 0 */
    int have_params_line = 0;

    /* step 1: ingest */
    for (;;) {
        if (read_line(cfd, line, sizeof line) != 0) { proto_error(cfd, "bad_request"); return; }
        if (line[0] == '\0') break;
        if      (sscanf(line, "prompt_len: %ld", &prompt_len) == 1) continue;
        else if (sscanf(line, "max_tokens: %ld", &max_tokens) == 1) continue;
        else if (sscanf(line, "temperature_q16: %ld", &temp_q16) == 1) { have_params_line = 1; continue; }
        else if (sscanf(line, "top_p_q16: %ld", &top_p_q16) == 1) { have_params_line = 1; continue; }
        else if (strncmp(line, "seed:", 5) == 0) {
            gw_log("warn", "protocol_reject", "seed parameter unsupported by oracle", -1, -1, 0);
            proto_error(cfd, "seed_unsupported");
            return;
        }
        else { proto_error(cfd, "unknown_key"); return; }
    }
    (void)have_params_line;
    if (prompt_len <= 0 || prompt_len > st->cfg->max_prompt_bytes) {
        proto_error(cfd, "bad_prompt_len");
        return;
    }
    if (max_tokens <= 0 || max_tokens > 200000) { proto_error(cfd, "bad_max_tokens"); return; }
    if (temp_q16 > 131072L || top_p_q16 > 65536L) { proto_error(cfd, "bad_param_range"); return; }
    {
        size_t got = 0;
        while (got < (size_t)prompt_len) {
            ssize_t r = read(cfd, st->prompt + got, (size_t)prompt_len - got);
            if (r <= 0) { proto_error(cfd, "short_prompt"); return; }
            got += (size_t)r;
        }
    }

    /* step 2: validate and normalise; pre-spend reject is final */
    {
        ax_l3_fault_flags_t f;
        int n;
        memset(&f, 0, sizeof f);
        n = ax_validate_and_normalise(st->norm, (size_t)st->cfg->max_prompt_bytes + 1,
                                      st->prompt, (size_t)prompt_len, &f);
        if (n < 0) {
            gw_log("warn", "encoding_reject", "prompt failed UTF-8/NFC/control validation",
                   -1, -1, 0);
            proto_error(cfd, "encoding_reject");
            return;
        }
        prompt_len = n;
    }

    /* step 3: params */
    {
        ax_oracle_params_t params;
        char input_hash_hex[65];
        uint8_t input_hash[32];
        gw_intent_t intent;
        gwc_result_t res;
        char reqhdr[512];
        int n;

        ax_oracle_params_init_null(&params);
        params.max_tokens = (int32_t)max_tokens;
        if (temp_q16 >= 0)  params.temperature = (q16_16_t)temp_q16;
        if (top_p_q16 >= 0) params.top_p       = (q16_16_t)top_p_q16;

        /* spend guard: pre-intent, pre-spend, log-only */
        if (st->calls_made >= st->cfg->call_budget) {
            gw_log("warn", "budget_refused", "per-run call budget exhausted",
                   -1, -1, 0);
            proto_error(cfd, "budget_exhausted");
            return;
        }

        /* step 4: durable intent before any network I/O */
        ax_compute_input_hash(input_hash, st->norm, (size_t)prompt_len);
        hex32(input_hash_hex, input_hash);
        memset(&intent, 0, sizeof intent);
        memcpy(intent.input_hash_hex, input_hash_hex, 65);
        (void)snprintf(intent.model_id, sizeof intent.model_id, "%s",
                       st->cfg->model_id);
        intent.max_tokens = params.max_tokens;
        intent.seed = -1;
        intent.temperature_q16 = params.temperature;
        intent.top_p_q16 = params.top_p;
        if (gw_wal_write(st->cfg->wal_path, &intent) != 0) {
            gw_log("error", "fault", "WAL intent write failed", -1, -1, 0);
            proto_error(cfd, "internal");
            return;
        }
        gw_log("info", "intent", input_hash_hex, -1, -1, gwl_seq() + 1u);

        /* build request body */
        {
            int en = gwj_escape(st->escaped, (size_t)st->cfg->max_prompt_bytes * 6 + 1,
                                st->norm, (size_t)prompt_len);
            if (en < 0) {
                (void)gw_wal_clear(st->cfg->wal_path);
                proto_error(cfd, "internal");
                return;
            }
            n = snprintf(st->reqjson, (size_t)st->cfg->max_prompt_bytes * 6 + 1024,
                "{\"model\":\"%s\",\"max_tokens\":%ld",
                st->cfg->model_id, (long)params.max_tokens);
            if (!ax_params_temperature_is_null(&params)) {
                char dec[24];
                (void)q16_to_dec(dec, sizeof dec, params.temperature);
                n += snprintf(st->reqjson + n, 64, ",\"temperature\":%s", dec);
            }
            if (!ax_params_top_p_is_null(&params)) {
                char dec[24];
                (void)q16_to_dec(dec, sizeof dec, params.top_p);
                n += snprintf(st->reqjson + n, 64, ",\"top_p\":%s", dec);
            }
            n += snprintf(st->reqjson + n, (size_t)en + 128,
                ",\"messages\":[{\"role\":\"user\",\"content\":\"%.*s\"}]}",
                en, st->escaped);
        }

        /* step 5: the oracle call; every path from here yields one obs */
        st->calls_made++;
        if (gwc_call(st->cfg, api_key, st->reqjson, (size_t)n, &res) != 0) {
            gw_log("error", "fault", "client allocation failure", -1, -1, 0);
            proto_error(cfd, "internal");
            return;   /* WAL intent left open deliberately: restart closes it */
        }
        (void)snprintf(reqhdr, sizeof reqhdr, "%s", res.errbuf);

        /* steps 6-10 by outcome */
        {
            ax_completion_state_t state = AX_COMPLETION_ERROR;
            ax_failure_type_t ftype = AX_FAILURE_TRANSPORT_ERROR;
            const char *output = "";
            size_t output_len = 0;
            char obs_hash_hex[AX_HASH_HEX_SIZE];
            char head_hex[65];
            uint64_t seq = 0;
            int ok_for_caller = 0;

            switch (res.outcome) {
            case GWC_OK: {
                char stop_reason[64] = {0};
                if (st->cfg->fixture_capture_path[0] != '\0') {
                    int ffd = open(st->cfg->fixture_capture_path,
                                   O_WRONLY | O_CREAT | O_EXCL, 0640);
                    if (ffd >= 0) {
                        if (write_all_fd(ffd, res.body, res.body_len) == 0 &&
                            fsync(ffd) == 0)
                            gw_log("info", "fixture_captured",
                                   st->cfg->fixture_capture_path, -1, -1, 0);
                        close(ffd);
                    }
                }
                size_t tlen = 0;
                if (gwj_content_text(res.body, res.body_len,
                                     g_text_buf, sizeof g_text_buf, &tlen) != 0 ||
                    gwj_top_string(res.body, res.body_len, "stop_reason",
                                   stop_reason, sizeof stop_reason) != 0) {
                    state = AX_COMPLETION_ERROR;
                    ftype = AX_FAILURE_INVALID_OUTPUT;
                    gw_log("error", "oracle_fail", "200 body failed structural parse",
                           res.attempts, res.http_status, 0);
                } else {
                    state = (strcmp(stop_reason, "max_tokens") == 0)
                            ? AX_COMPLETION_TRUNCATED : AX_COMPLETION_COMPLETE;
                    ftype = AX_FAILURE_NULL;
                    output = g_text_buf;
                    output_len = tlen;
                    ok_for_caller = 1;
                    gw_log("info", "oracle_ok", stop_reason,
                           res.attempts, res.http_status, 0);
                }
                break;
            }
            case GWC_TIMEOUT:
                state = AX_COMPLETION_ERROR; ftype = AX_FAILURE_TIMEOUT;
                gw_log("error", "oracle_fail", res.errbuf,
                       res.attempts, res.http_status, 0);
                break;
            case GWC_OVERCAP:
                state = AX_COMPLETION_ERROR; ftype = AX_FAILURE_INVALID_OUTPUT;
                gw_log("error", "oracle_fail", res.errbuf,
                       res.attempts, res.http_status, 0);
                break;
            case GWC_HTTP_FAIL: {
                char em[400];
                state = AX_COMPLETION_ERROR; ftype = AX_FAILURE_TRANSPORT_ERROR;
                if (res.body != NULL &&
                    gwj_error_message(res.body, res.body_len, em, sizeof em) == 0) {
                    gw_log("error", "oracle_fail", em,
                           res.attempts, res.http_status, 0);
                } else {
                    gw_log("error", "oracle_fail", res.errbuf,
                           res.attempts, res.http_status, 0);
                }
                break;
            }
            case GWC_TRANSPORT:
            default:
                state = AX_COMPLETION_ERROR; ftype = AX_FAILURE_TRANSPORT_ERROR;
                gw_log("error", "oracle_fail", res.errbuf,
                       res.attempts, res.http_status, 0);
                break;
            }

            /* Non-NFC or otherwise unadmittable COMPLETE output degrades
             * to INVALID_OUTPUT with empty output, per the schema-forced
             * ruling; the paid completion is never silently discarded,
             * its rejection is the recorded fact. */
            if (commit_observation(st, state, ftype,
                                   st->norm, (size_t)prompt_len,
                                   output, output_len, &params,
                                   obs_hash_hex, head_hex, &seq) != 0) {
                if (state == AX_COMPLETION_COMPLETE ||
                    state == AX_COMPLETION_TRUNCATED) {
                    gw_log("warn", "oracle_fail",
                           "output failed admission; recording INVALID_OUTPUT",
                           res.attempts, res.http_status, 0);
                    state = AX_COMPLETION_ERROR;
                    ftype = AX_FAILURE_INVALID_OUTPUT;
                    output = ""; output_len = 0; ok_for_caller = 0;
                    if (commit_observation(st, state, ftype,
                                           st->norm, (size_t)prompt_len,
                                           output, output_len, &params,
                                           obs_hash_hex, head_hex, &seq) != 0) {
                        gw_log("error", "fault",
                               "record validation reject: wrapper disagrees with itself",
                               -1, -1, 0);
                        gwc_result_free(&res);
                        proto_error(cfd, "internal");
                        return;   /* WAL open; restart replays */
                    }
                } else {
                    gw_log("error", "fault",
                           "record validation reject: wrapper disagrees with itself",
                           -1, -1, 0);
                    gwc_result_free(&res);
                    proto_error(cfd, "internal");
                    return;       /* WAL open; restart replays */
                }
            }
            gwc_result_free(&res);

            /* observation durable: close the intent */
            if (gw_wal_clear(st->cfg->wal_path) != 0)
                gw_log("error", "fault", "WAL clear failed", -1, -1, seq);
            gw_log("info", "obs_committed", obs_hash_hex, -1, -1, seq);

            /* respond */
            {
                char hdr[512];
                int hn = snprintf(hdr, sizeof hdr,
                    "status: %s\ncompletion_state: %s\nfailure_type: %s\n"
                    "obs_hash: %s\nchain_head: %s\nseq: %llu\noutput_len: %zu\n\n",
                    ok_for_caller ? "ok" : "error",
                    state == AX_COMPLETION_COMPLETE ? "COMPLETE" :
                    state == AX_COMPLETION_TRUNCATED ? "TRUNCATED" : "ERROR",
                    ftype == AX_FAILURE_NULL ? "NULL" :
                    ftype == AX_FAILURE_TIMEOUT ? "TIMEOUT" :
                    ftype == AX_FAILURE_INVALID_OUTPUT ? "INVALID_OUTPUT" :
                                                         "TRANSPORT_ERROR",
                    obs_hash_hex, head_hex, (unsigned long long)seq, output_len);
                if (hn > 0 && write_all_fd(cfd, hdr, (size_t)hn) == 0 &&
                    output_len > 0)
                    (void)write_all_fd(cfd, output, output_len);
            }
        }
    }
}

/* ---- WAL recovery -> failure observation ------------------------------- */

static int recover_wal(gw_state_t *st)
{
    gw_intent_t in;
    int r = gw_wal_recover(st->cfg->wal_path, &in);
    if (r == 0) return 0;
    if (r < 0) {
        gw_log("error", "fault", "corrupt WAL intent; refusing to start", -1, -1, 0);
        return -1;
    }
    /* If the most recent chain payload already carries this intent's
     * input_hash, the observation landed before the crash and the
     * intent is stale: close it without a second record. */
    if (gwl_last_payload_contains(in.input_hash_hex)) {
        gw_log("info", "wal_recovered",
               "intent already evidenced; clearing stale WAL", -1, -1, gwl_seq());
        return gw_wal_clear(st->cfg->wal_path);
    }
    {
        /* The original prompt is gone by design (only its hash was
         * durable), so the recovery obs hashes the recorded intent line
         * itself as its input: the evidence honestly states what is
         * known, which is that a call with this input_hash was made and
         * no response was evidenced. */
        ax_oracle_params_t params;
        char synth[256];
        char obs_hash_hex[AX_HASH_HEX_SIZE];
        char head_hex[65];
        uint64_t seq = 0;
        int n;

        ax_oracle_params_init_null(&params);
        params.max_tokens = in.max_tokens;
        if (in.temperature_q16 != INT32_MIN) params.temperature = in.temperature_q16;
        if (in.top_p_q16 != INT32_MIN)       params.top_p = in.top_p_q16;

        n = snprintf(synth, sizeof synth,
                     "unclosed-intent input_hash=%s", in.input_hash_hex);
        if (n <= 0) return -1;

        if (commit_observation(st, AX_COMPLETION_ERROR,
                               AX_FAILURE_TRANSPORT_ERROR,
                               synth, (size_t)n, "", 0, &params,
                               obs_hash_hex, head_hex, &seq) != 0) {
            gw_log("error", "fault", "WAL recovery observation failed", -1, -1, 0);
            return -1;
        }
        gw_log("info", "wal_recovered", in.input_hash_hex, -1, -1, seq);
    }
    return gw_wal_clear(st->cfg->wal_path);
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static gw_config_t cfg;
    gw_state_t st;
    const char *api_key;
    int lfd;
    struct sockaddr_un addr;

    if (argc != 2) {
        fprintf(stderr, "usage: axioma-gateway <config-file>\n");
        return 1;
    }
    if (gw_config_load(&cfg, argv[1]) != 0) return 1;

    api_key = getenv("AX_ORACLE_ANTHROPIC_KEY");
    if (api_key == NULL || api_key[0] == '\0') {
        fprintf(stderr, "AX_ORACLE_ANTHROPIC_KEY not set\n");
        return 1;
    }

    if (gw_log_open(cfg.log_path, api_key) != 0) {
        fprintf(stderr, "cannot open log %s\n", cfg.log_path);
        return 1;
    }

    memset(&st, 0, sizeof st);
    st.cfg = &cfg;
    ax_admission_ctx_init(&st.adm);
    st.prompt  = malloc((size_t)cfg.max_prompt_bytes + 1);
    st.norm    = malloc((size_t)cfg.max_prompt_bytes + 1);
    st.escaped = malloc((size_t)cfg.max_prompt_bytes * 6 + 1);
    st.reqjson = malloc((size_t)cfg.max_prompt_bytes * 6 + 1024);
    if (!st.prompt || !st.norm || !st.escaped || !st.reqjson) {
        fprintf(stderr, "allocation failure\n");
        return 1;
    }

    {
        /* Torn tail is tolerable only when a WAL intent can account for
         * it; otherwise refuse and let the operator look. */
        int wal_present, truncated = 0;
        gw_intent_t probe;
        wal_present = (gw_wal_recover(cfg.wal_path, &probe) == 1);
        if (gwl_open(cfg.ledger_path, wal_present, &truncated) != 0) {
            gw_log("error", "fault", "ledger open/replay failed", -1, -1, 0);
            return 1;
        }
        if (truncated)
            gw_log("warn", "fault", "torn ledger tail truncated under open WAL intent",
                   -1, -1, gwl_seq());
    }
    if (recover_wal(&st) != 0) return 1;

    /* Config binding as chain evidence: AX:STATE:v1 per start. */
    {
        uint8_t cfg_hash[32];
        char cfg_hex[65];
        char payload[192];
        int n;
        uint8_t head[32];
        uint64_t seq;
        axilog_sha256(cfg_hash, cfg.raw, cfg.raw_len);
        hex32(cfg_hex, cfg_hash);
        n = snprintf(payload, sizeof payload,
            "{\"component\":\"axioma-gateway\",\"config_sha256\":\"%s\","
            "\"evidence_type\":\"AX:STATE:v1\"}", cfg_hex);
        if (n <= 0 ||
            gwl_append(GW_TAG_STATE, (const uint8_t *)payload,
                       (uint64_t)n, head, &seq) != 0) {
            gw_log("error", "fault", "config commitment failed", -1, -1, 0);
            return 1;
        }
        gw_log("info", "config_commit", cfg_hex, -1, -1, seq);
    }

    if (gwc_global_init() != 0) {
        gw_log("error", "fault", "curl init failed", -1, -1, 0);
        return 1;
    }

    {
        /* sigaction without SA_RESTART: accept() must return EINTR so
         * the loop rechecks g_stop, or SIGTERM never lands and systemd
         * SIGKILLs after its stop timeout. */
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        signal(SIGPIPE, SIG_IGN);
    }

    (void)unlink(cfg.socket_path);
    lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) { gw_log("error", "fault", "socket", -1, -1, 0); return 1; }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (strlen(cfg.socket_path) >= sizeof addr.sun_path) {
        gw_log("error", "fault", "socket_path exceeds AF_UNIX limit", -1, -1, 0);
        return 1;
    }
    memcpy(addr.sun_path, cfg.socket_path, strlen(cfg.socket_path) + 1);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        chmod(cfg.socket_path, 0660) != 0 ||
        listen(lfd, 1) != 0) {
        gw_log("error", "fault", "bind/listen", -1, -1, 0);
        return 1;
    }

    /* E2: readiness is bind+listen, not fork. Under systemd
     * (Type=notify) this datagram is the start signal; on a manual
     * run NOTIFY_SOCKET is unset and the call is a no-op. A failed
     * send is logged and the service continues: it is up, only the
     * supervisor's view of it is wrong, and it will say so. */
    if (gw_notify_ready() != 0)
        gw_log("warn", "fault", "sd_notify READY=1 send failed", -1, -1, 0);

    gw_log("info", "startup", cfg.model_id, -1, -1, gwl_seq());

    while (!g_stop) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            gw_log("error", "fault", "accept", -1, -1, 0);
            break;
        }
        handle_request(&st, cfd, api_key);
        close(cfd);
    }

    gw_log("info", "shutdown", NULL, -1, -1, gwl_seq());
    close(lfd);
    (void)unlink(cfg.socket_path);
    gwc_global_cleanup();
    gwl_close();
    gw_log_close();
    free(st.prompt); free(st.norm); free(st.escaped); free(st.reqjson);
    return 0;
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_ledger.c - see gw_ledger.h. TU group B: audit + substrate headers
 * only. Do not include any axioma-oracle header in this file.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_ledger.h"


#include <axilog/audit.h>        /* ax_ledger_*, ax_commit_evidence */
#include <axilog/types.h>        /* substrate ct_fault_flags_t */
#include <axilog/commitment.h>   /* AX_TAG_* */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define GWL_TAG_MAX      32u
#define GWL_PAYLOAD_MAX  (256u * 1024u)

static ax_ledger_ctx_t g_ctx;
static int   g_fd = -1;
static int   g_export_fd = -1;   /* serving-determinism witness export */
static char  g_last_payload[GWL_PAYLOAD_MAX];
static size_t g_last_payload_len = 0;

static int read_exact(int fd, void *buf, size_t n, size_t *got)
{
    size_t o = 0;
    while (o < n) {
        ssize_t r = read(fd, (char *)buf + o, n - o);
        if (r < 0) return -1;
        if (r == 0) break;
        o += (size_t)r;
    }
    *got = o;
    return 0;
}

static int write_all(int fd, const void *buf, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t w = write(fd, (const char *)buf + o, n - o);
        if (w < 0) return -1;
        o += (size_t)w;
    }
    return 0;
}

static uint32_t rd_u32le(const uint8_t b[4])
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint64_t rd_u64le(const uint8_t b[8])
{
    uint64_t v = 0; int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}
static void wr_u32le(uint8_t b[4], uint32_t v)
{
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
static void wr_u64le(uint8_t b[8], uint64_t v)
{
    int i; for (i = 0; i < 8; i++) { b[i]=(uint8_t)v; v >>= 8; }
}

int gwl_open(const char *path, int torn_tail_ok, int *truncated)
{
    ct_fault_flags_t cf;
    off_t good_end = 0;
    static uint8_t payload[GWL_PAYLOAD_MAX];   /* replay scratch */

    if (truncated != NULL) *truncated = 0;

    ct_fault_init(&cf);
    ax_ledger_genesis(&g_ctx, &cf);
    if (ct_fault_any(&cf)) return -1;

    g_fd = open(path, O_RDWR | O_CREAT, 0640);
    if (g_fd < 0) return -1;

    for (;;) {
        uint8_t h4[4], h8[8], stored[32], computed[32];
        char tag[GWL_TAG_MAX + 1];
        uint32_t tag_len;
        uint64_t plen;
        size_t got;
        ax_evidence_t ev;

        if (read_exact(g_fd, h4, 4, &got) != 0) goto fail;
        if (got == 0) break;                       /* clean EOF */
        if (got < 4) goto torn;
        tag_len = rd_u32le(h4);
        if (tag_len == 0 || tag_len > GWL_TAG_MAX) goto fail;
        if (read_exact(g_fd, tag, tag_len, &got) != 0 || got < tag_len) goto torn;
        tag[tag_len] = '\0';
        if (read_exact(g_fd, h8, 8, &got) != 0 || got < 8) goto torn;
        plen = rd_u64le(h8);
        if (plen == 0 || plen > GWL_PAYLOAD_MAX) goto fail;
        if (read_exact(g_fd, payload, (size_t)plen, &got) != 0 || got < plen) goto torn;
        if (read_exact(g_fd, stored, 32, &got) != 0 || got < 32) goto torn;

        ev.tag = tag; ev.payload = payload; ev.payload_len = plen;
        ct_fault_init(&cf);
        ax_commit_evidence(&ev, computed, &cf);
        if (ct_fault_any(&cf)) goto fail;
        if (memcmp(computed, stored, 32) != 0) goto fail;   /* tamper: refuse */

        ct_fault_init(&cf);
        ax_ledger_append(&g_ctx, computed, &cf);
        if (ct_fault_any(&cf)) goto fail;

        memcpy(g_last_payload, payload, (size_t)plen);
        g_last_payload_len = (size_t)plen;
        good_end = lseek(g_fd, 0, SEEK_CUR);
        if (good_end < 0) goto fail;
        continue;

    torn:
        if (!torn_tail_ok) goto fail;
        if (ftruncate(g_fd, good_end) != 0) goto fail;
        if (truncated != NULL) *truncated = 1;
        break;
    }

    if (lseek(g_fd, 0, SEEK_END) < 0) goto fail;
    return 0;

fail:
    close(g_fd);
    g_fd = -1;
    return -1;
}

int gwl_append(const char *tag, const uint8_t *payload, uint64_t payload_len,
               uint8_t head_out[32], uint64_t *seq_out)
{
    ct_fault_flags_t cf;
    ax_evidence_t ev;
    uint8_t commit[32], h4[4], h8[8];
    size_t tag_len;

    if (g_fd < 0 || tag == NULL || payload == NULL) return -1;
    tag_len = strlen(tag);
    if (tag_len == 0 || tag_len > GWL_TAG_MAX) return -1;
    if (payload_len == 0 || payload_len > GWL_PAYLOAD_MAX) return -1;

    ev.tag = tag; ev.payload = payload; ev.payload_len = payload_len;
    ct_fault_init(&cf);
    ax_commit_evidence(&ev, commit, &cf);
    if (ct_fault_any(&cf)) return -1;

    wr_u32le(h4, (uint32_t)tag_len);
    wr_u64le(h8, payload_len);
    if (write_all(g_fd, h4, 4) != 0)                      return -1;
    if (write_all(g_fd, tag, tag_len) != 0)               return -1;
    if (write_all(g_fd, h8, 8) != 0)                      return -1;
    if (write_all(g_fd, payload, (size_t)payload_len) != 0) return -1;
    if (write_all(g_fd, commit, 32) != 0)                 return -1;
    if (fsync(g_fd) != 0)                                 return -1;

    /* Mirror the identical frame to the witness export. The primary is
     * already durable; export failure must not refuse serving (the
     * projection is not the truth), so on any short write the export
     * drops to inactive and the caller logs the transition. A stale
     * export is NOT-DISCHARGEABLE at witness time, never a silent pass. */
    if (g_export_fd >= 0) {
        if (write_all(g_export_fd, h4, 4) != 0 ||
            write_all(g_export_fd, tag, tag_len) != 0 ||
            write_all(g_export_fd, h8, 8) != 0 ||
            write_all(g_export_fd, payload, (size_t)payload_len) != 0 ||
            write_all(g_export_fd, commit, 32) != 0) {
            close(g_export_fd);
            g_export_fd = -1;
        }
    }

    ct_fault_init(&cf);
    ax_ledger_append(&g_ctx, commit, &cf);
    if (ct_fault_any(&cf)) return -1;   /* ctx fail-closed; service must stop */

    memcpy(g_last_payload, payload,
           (size_t)(payload_len < GWL_PAYLOAD_MAX ? payload_len : GWL_PAYLOAD_MAX));
    g_last_payload_len = (size_t)payload_len;

    if (head_out != NULL) memcpy(head_out, g_ctx.current_hash, 32);
    if (seq_out  != NULL) *seq_out = g_ctx.sequence;
    return 0;
}

uint64_t gwl_seq(void) { return g_ctx.sequence; }

int gwl_export_enable(const char *path)
{
    off_t size, off;
    static uint8_t copy_buf[64 * 1024];

    if (g_fd < 0 || path == NULL || path[0] == '\0') return -1;
    if (g_export_fd >= 0) { close(g_export_fd); g_export_fd = -1; }

    /* O_TRUNC: the primary, as replayed and verified by gwl_open, is
     * the truth; whatever a prior run left at the export path (torn
     * tail from an unsynced frame, stale prefix, garbage) is healed by
     * rewriting from byte zero. 0644: the export exists to be read by
     * the witness user; the primary keeps its 0640. */
    g_export_fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_export_fd < 0) return -1;

    size = lseek(g_fd, 0, SEEK_CUR);   /* gwl_open left g_fd at the end */
    if (size < 0) goto fail;
    for (off = 0; off < size; ) {
        ssize_t r = pread(g_fd, copy_buf, sizeof copy_buf, off);
        if (r <= 0) goto fail;
        if (write_all(g_export_fd, copy_buf, (size_t)r) != 0) goto fail;
        off += r;
    }
    if (fsync(g_export_fd) != 0) goto fail;   /* the baseline copy is synced;
                                               * per-frame mirrors are not */
    return 0;

fail:
    close(g_export_fd);
    g_export_fd = -1;
    return -1;
}

int gwl_export_active(void) { return g_export_fd >= 0; }

void gwl_head(uint8_t out[32]) { memcpy(out, g_ctx.current_hash, 32); }

int gwl_last_payload_contains(const char *needle)
{
    size_t nl;
    if (needle == NULL || g_last_payload_len == 0) return 0;
    nl = strlen(needle);
    if (nl == 0 || nl > g_last_payload_len) return 0;
    {
        size_t i;
        for (i = 0; i + nl <= g_last_payload_len; i++) {
            if (memcmp(g_last_payload + i, needle, nl) == 0) return 1;
        }
    }
    return 0;
}

void gwl_close(void)
{
    if (g_export_fd >= 0) { close(g_export_fd); g_export_fd = -1; }
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}

/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_json.c - see gw_json.h.
 *
 * The parser walks real JSON structure (strings with escapes, nesting
 * depth) rather than pattern-matching, so a "type":"text" inside a
 * string literal cannot fool it. It still only understands the shapes
 * it was built for; surprises return -1 and the caller records
 * INVALID_OUTPUT.
 */
#include "gw_json.h"
#include <string.h>

int gwj_escape(char *dst, size_t cap, const char *src, size_t len)
{
    size_t i, o = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        const char *rep = NULL;
        char ubuf[7];
        size_t rn;
        if      (c == '"')  rep = "\\\"";
        else if (c == '\\') rep = "\\\\";
        else if (c == '\n') rep = "\\n";
        else if (c == '\r') rep = "\\r";
        else if (c == '\t') rep = "\\t";
        else if (c < 0x20) {
            static const char hexd[] = "0123456789abcdef";
            ubuf[0]='\\'; ubuf[1]='u'; ubuf[2]='0'; ubuf[3]='0';
            ubuf[4]=hexd[(c >> 4) & 0xF]; ubuf[5]=hexd[c & 0xF]; ubuf[6]='\0';
            rep = ubuf;
        }
        if (rep != NULL) {
            rn = strlen(rep);
            if (o + rn > cap) return -1;
            memcpy(dst + o, rep, rn);
            o += rn;
        } else {
            if (o + 1 > cap) return -1;
            dst[o++] = (char)c;
        }
    }
    return (int)o;
}

/* ---- scanning primitives ---------------------------------------------- */

typedef struct { const char *p; const char *end; } scan_t;

static int s_eof(const scan_t *s) { return s->p >= s->end; }

static void s_ws(scan_t *s)
{
    while (!s_eof(s) && (*s->p==' '||*s->p=='\t'||*s->p=='\n'||*s->p=='\r'))
        s->p++;
}

/* Advance past a JSON string (opening quote already consumed by caller
 * or at *s->p). Returns 0 with s->p past the closing quote, -1 on error. */
static int s_skip_string(scan_t *s)
{
    if (s_eof(s) || *s->p != '"') return -1;
    s->p++;
    while (!s_eof(s)) {
        char c = *s->p++;
        if (c == '"')  return 0;
        if (c == '\\') {
            if (s_eof(s)) return -1;
            s->p++;                      /* escaped char; \uXXXX handled as u + 4 */
            if (s->p[-1] == 'u') {
                if (s->end - s->p < 4) return -1;
                s->p += 4;
            }
        }
    }
    return -1;
}

/* Decode a JSON string starting at *s->p (must be '"') into dst.
 * Handles all escapes including surrogate pairs, encodes UTF-8.
 * Returns 0, advances past closing quote, or -1. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int s_read_u4(scan_t *s, unsigned int *out)
{
    unsigned int v = 0; int i;
    if (s->end - s->p < 4) return -1;
    for (i = 0; i < 4; i++) {
        int h = hexval(s->p[i]);
        if (h < 0) return -1;
        v = (v << 4) | (unsigned int)h;
    }
    s->p += 4;
    *out = v;
    return 0;
}

static int put_utf8(char *dst, size_t cap, size_t *o, unsigned long cp)
{
    unsigned char b[4]; size_t n;
    if      (cp < 0x80)     { b[0]=(unsigned char)cp; n=1; }
    else if (cp < 0x800)    { b[0]=(unsigned char)(0xC0|(cp>>6));
                              b[1]=(unsigned char)(0x80|(cp&0x3F)); n=2; }
    else if (cp < 0x10000)  { b[0]=(unsigned char)(0xE0|(cp>>12));
                              b[1]=(unsigned char)(0x80|((cp>>6)&0x3F));
                              b[2]=(unsigned char)(0x80|(cp&0x3F)); n=3; }
    else if (cp <= 0x10FFFF){ b[0]=(unsigned char)(0xF0|(cp>>18));
                              b[1]=(unsigned char)(0x80|((cp>>12)&0x3F));
                              b[2]=(unsigned char)(0x80|((cp>>6)&0x3F));
                              b[3]=(unsigned char)(0x80|(cp&0x3F)); n=4; }
    else return -1;
    if (*o + n > cap) return -1;
    memcpy(dst + *o, b, n);
    *o += n;
    return 0;
}

static int s_decode_string(scan_t *s, char *dst, size_t cap, size_t *out_len)
{
    size_t o = *out_len;
    if (s_eof(s) || *s->p != '"') return -1;
    s->p++;
    while (!s_eof(s)) {
        char c = *s->p++;
        if (c == '"') { *out_len = o; return 0; }
        if (c == '\\') {
            char e;
            if (s_eof(s)) return -1;
            e = *s->p++;
            switch (e) {
            case '"': case '\\': case '/':
                if (o + 1 > cap) return -1;
                dst[o++] = e; break;
            case 'b': if (o+1>cap) return -1; dst[o++]='\b'; break;
            case 'f': if (o+1>cap) return -1; dst[o++]='\f'; break;
            case 'n': if (o+1>cap) return -1; dst[o++]='\n'; break;
            case 'r': if (o+1>cap) return -1; dst[o++]='\r'; break;
            case 't': if (o+1>cap) return -1; dst[o++]='\t'; break;
            case 'u': {
                unsigned int u1;
                unsigned long cp;
                if (s_read_u4(s, &u1) != 0) return -1;
                if (u1 >= 0xD800 && u1 <= 0xDBFF) {
                    unsigned int u2;
                    if (s->end - s->p < 6 || s->p[0] != '\\' || s->p[1] != 'u')
                        return -1;
                    s->p += 2;
                    if (s_read_u4(s, &u2) != 0) return -1;
                    if (u2 < 0xDC00 || u2 > 0xDFFF) return -1;
                    cp = 0x10000UL +
                         (((unsigned long)(u1 - 0xD800) << 10) |
                          (unsigned long)(u2 - 0xDC00));
                } else if (u1 >= 0xDC00 && u1 <= 0xDFFF) {
                    return -1;               /* lone low surrogate */
                } else {
                    cp = u1;
                }
                if (put_utf8(dst, cap, &o, cp) != 0) return -1;
                break;
            }
            default: return -1;
            }
        } else {
            if (o + 1 > cap) return -1;
            dst[o++] = c;
        }
    }
    return -1;
}

/* Skip any JSON value at *s->p. Depth-bounded. */
static int s_skip_value(scan_t *s, int depth)
{
    if (depth > 64) return -1;
    s_ws(s);
    if (s_eof(s)) return -1;
    switch (*s->p) {
    case '"': return s_skip_string(s);
    case '{': case '[': {
        char open = *s->p++, close = (open=='{') ? '}' : ']';
        s_ws(s);
        if (!s_eof(s) && *s->p == close) { s->p++; return 0; }
        for (;;) {
            if (open == '{') {
                s_ws(s);
                if (s_skip_string(s) != 0) return -1;   /* key */
                s_ws(s);
                if (s_eof(s) || *s->p != ':') return -1;
                s->p++;
            }
            if (s_skip_value(s, depth + 1) != 0) return -1;
            s_ws(s);
            if (s_eof(s)) return -1;
            if (*s->p == ',') { s->p++; continue; }
            if (*s->p == close) { s->p++; return 0; }
            return -1;
        }
    }
    default: {
        /* number, true, false, null: consume token chars */
        const char *tok = "0123456789+-.eEtruefalsn";
        if (strchr(tok, *s->p) == NULL) return -1;
        while (!s_eof(s) && strchr(tok, *s->p) != NULL) s->p++;
        return 0;
    }
    }
}

/* Iterate a JSON object at *s->p. For each key, calls back via a small
 * dispatch inline below (no function pointers needed at this scale). */

int gwj_top_string(const char *body, size_t body_len, const char *key,
                   char *dst, size_t cap)
{
    scan_t s = { body, body + body_len };
    s_ws(&s);
    if (s_eof(&s) || *s.p != '{') return -1;
    s.p++;
    for (;;) {
        char kbuf[64]; size_t klen = 0;
        s_ws(&s);
        if (!s_eof(&s) && *s.p == '}') return -1;      /* not found */
        if (s_decode_string(&s, kbuf, sizeof kbuf - 1, &klen) != 0) return -1;
        kbuf[klen] = '\0';
        s_ws(&s);
        if (s_eof(&s) || *s.p != ':') return -1;
        s.p++;
        s_ws(&s);
        if (strcmp(kbuf, key) == 0) {
            size_t olen = 0;
            if (s_eof(&s) || *s.p != '"') return -1;   /* present, not a string */
            if (s_decode_string(&s, dst, cap - 1, &olen) != 0) return -1;
            dst[olen] = '\0';
            return 0;
        }
        if (s_skip_value(&s, 0) != 0) return -1;
        s_ws(&s);
        if (s_eof(&s)) return -1;
        if (*s.p == ',') { s.p++; continue; }
        if (*s.p == '}') return -1;                    /* not found */
        return -1;
    }
}

/* Walk content[] and concatenate text of "type":"text" blocks. */
int gwj_content_text(const char *body, size_t body_len,
                     char *dst, size_t cap, size_t *out_len)
{
    scan_t s = { body, body + body_len };
    size_t o = 0;
    int found_content = 0;

    s_ws(&s);
    if (s_eof(&s) || *s.p != '{') return -1;
    s.p++;
    for (;;) {
        char kbuf[64]; size_t klen = 0;
        s_ws(&s);
        if (!s_eof(&s) && *s.p == '}') break;
        if (s_decode_string(&s, kbuf, sizeof kbuf - 1, &klen) != 0) return -1;
        kbuf[klen] = '\0';
        s_ws(&s);
        if (s_eof(&s) || *s.p != ':') return -1;
        s.p++;
        s_ws(&s);
        if (strcmp(kbuf, "content") == 0) {
            found_content = 1;
            if (s_eof(&s) || *s.p != '[') return -1;
            s.p++;
            s_ws(&s);
            if (!s_eof(&s) && *s.p == ']') { s.p++; }
            else for (;;) {
                /* each element: object with type/text among other keys */
                int is_text = 0;
                const char *text_at = NULL;   /* deferred decode */
                scan_t save;
                s_ws(&s);
                if (s_eof(&s) || *s.p != '{') return -1;
                s.p++;
                for (;;) {
                    char ek[32]; size_t ekl = 0;
                    s_ws(&s);
                    if (!s_eof(&s) && *s.p == '}') { s.p++; break; }
                    if (s_decode_string(&s, ek, sizeof ek - 1, &ekl) != 0) return -1;
                    ek[ekl] = '\0';
                    s_ws(&s);
                    if (s_eof(&s) || *s.p != ':') return -1;
                    s.p++;
                    s_ws(&s);
                    if (strcmp(ek, "type") == 0) {
                        char tv[16]; size_t tvl = 0;
                        if (s_eof(&s) || *s.p != '"') return -1;
                        if (s_decode_string(&s, tv, sizeof tv - 1, &tvl) != 0) return -1;
                        tv[tvl] = '\0';
                        if (strcmp(tv, "text") == 0) is_text = 1;
                    } else if (strcmp(ek, "text") == 0) {
                        if (s_eof(&s) || *s.p != '"') return -1;
                        text_at = s.p;
                        if (s_skip_string(&s) != 0) return -1;
                    } else {
                        if (s_skip_value(&s, 0) != 0) return -1;
                    }
                    s_ws(&s);
                    if (s_eof(&s)) return -1;
                    if (*s.p == ',') { s.p++; continue; }
                    if (*s.p == '}') { s.p++; break; }
                    return -1;
                }
                if (is_text && text_at != NULL) {
                    save.p = text_at; save.end = s.end;
                    if (s_decode_string(&save, dst, cap, &o) != 0) return -1;
                }
                s_ws(&s);
                if (s_eof(&s)) return -1;
                if (*s.p == ',') { s.p++; continue; }
                if (*s.p == ']') { s.p++; break; }
                return -1;
            }
        } else {
            if (s_skip_value(&s, 0) != 0) return -1;
        }
        s_ws(&s);
        if (s_eof(&s)) return -1;
        if (*s.p == ',') { s.p++; continue; }
        if (*s.p == '}') break;
        return -1;
    }
    if (!found_content) return -1;
    *out_len = o;
    return 0;
}

int gwj_error_message(const char *body, size_t body_len,
                      char *dst, size_t cap)
{
    /* {"type":"error","error":{"type":"...","message":"..."}} */
    scan_t s = { body, body + body_len };
    s_ws(&s);
    if (s_eof(&s) || *s.p != '{') return -1;
    s.p++;
    for (;;) {
        char kbuf[32]; size_t klen = 0;
        s_ws(&s);
        if (!s_eof(&s) && *s.p == '}') return -1;
        if (s_decode_string(&s, kbuf, sizeof kbuf - 1, &klen) != 0) return -1;
        kbuf[klen] = '\0';
        s_ws(&s);
        if (s_eof(&s) || *s.p != ':') return -1;
        s.p++;
        s_ws(&s);
        if (strcmp(kbuf, "error") == 0) {
            /* find "message" within this object */
            if (s_eof(&s) || *s.p != '{') return -1;
            {
                const char *obj_start = s.p;
                size_t remaining = (size_t)(s.end - obj_start);
                return gwj_top_string(obj_start, remaining, "message", dst, cap);
            }
        }
        if (s_skip_value(&s, 0) != 0) return -1;
        s_ws(&s);
        if (s_eof(&s)) return -1;
        if (*s.p == ',') { s.p++; continue; }
        if (*s.p == '}') return -1;
        return -1;
    }
}

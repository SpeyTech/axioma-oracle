/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * test_gw_json.c - the extractor must survive escapes, surrogate pairs,
 * and adversarial placement of key-like strings inside string values.
 */
#include "gw_json.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  %-44s PASS\n", name); \
    else { printf("  %-44s FAIL\n", name); failures++; } \
} while (0)

int main(void)
{
    char out[4096];
    size_t n = 0;

    printf("gateway: test_gw_json\n");

    /* plain extraction */
    {
        const char *b = "{\"id\":\"m1\",\"content\":[{\"type\":\"text\","
                        "\"text\":\"hello\"}],\"stop_reason\":\"end_turn\"}";
        CHECK(gwj_content_text(b, strlen(b), out, sizeof out, &n) == 0 &&
              n == 5 && memcmp(out, "hello", 5) == 0, "plain text block");
        CHECK(gwj_top_string(b, strlen(b), "stop_reason", out, sizeof out) == 0 &&
              strcmp(out, "end_turn") == 0, "stop_reason");
    }

    /* escapes and unicode incl. surrogate pair */
    {
        const char *b = "{\"content\":[{\"type\":\"text\","
            "\"text\":\"a\\\"b\\\\c\\n\\u00e9\\ud83c\\udfb4\"}],"
            "\"stop_reason\":\"max_tokens\"}";
        const char expect[] = "a\"b\\c\n\xc3\xa9\xf0\x9f\x8e\xb4";
        CHECK(gwj_content_text(b, strlen(b), out, sizeof out, &n) == 0 &&
              n == sizeof expect - 1 && memcmp(out, expect, n) == 0,
              "escapes, e-acute, surrogate pair");
    }

    /* key-like text inside a string value must not confuse the walker */
    {
        const char *b = "{\"model\":\"\\\"content\\\": trap\","
            "\"content\":[{\"type\":\"text\",\"text\":\"real\"}],"
            "\"stop_reason\":\"end_turn\"}";
        CHECK(gwj_content_text(b, strlen(b), out, sizeof out, &n) == 0 &&
              n == 4 && memcmp(out, "real", 4) == 0, "trap key inside string");
    }

    /* multiple text blocks concatenate; non-text blocks skipped */
    {
        const char *b = "{\"content\":[{\"type\":\"text\",\"text\":\"a\"},"
            "{\"type\":\"tool_use\",\"id\":\"x\",\"input\":{\"k\":[1,2]}},"
            "{\"type\":\"text\",\"text\":\"b\"}],\"stop_reason\":\"end_turn\"}";
        CHECK(gwj_content_text(b, strlen(b), out, sizeof out, &n) == 0 &&
              n == 2 && memcmp(out, "ab", 2) == 0, "concat, skip tool_use");
    }

    /* error body */
    {
        const char *b = "{\"type\":\"error\",\"error\":{\"type\":"
            "\"authentication_error\",\"message\":\"invalid x-api-key\"}}";
        CHECK(gwj_error_message(b, strlen(b), out, sizeof out) == 0 &&
              strcmp(out, "invalid x-api-key") == 0, "error.message");
    }

    /* structural surprises return -1 */
    {
        const char *b1 = "{\"content\":\"not an array\"}";
        const char *b2 = "{\"no_content\":true}";
        const char *b3 = "{\"content\":[{\"type\":\"text\",\"text\":\"x\"}";
        CHECK(gwj_content_text(b1, strlen(b1), out, sizeof out, &n) == -1,
              "content not array rejected");
        CHECK(gwj_content_text(b2, strlen(b2), out, sizeof out, &n) == -1,
              "missing content rejected");
        CHECK(gwj_content_text(b3, strlen(b3), out, sizeof out, &n) == -1,
              "truncated body rejected");
    }

    /* lone low surrogate rejected */
    {
        const char *b = "{\"content\":[{\"type\":\"text\","
                        "\"text\":\"\\udc00\"}],\"stop_reason\":\"x\"}";
        CHECK(gwj_content_text(b, strlen(b), out, sizeof out, &n) == -1,
              "lone low surrogate rejected");
    }

    /* escape round trip */
    {
        const char src[] = "he said \"hi\"\n\ttab\\slash\x01";
        char esc[256];
        int en = gwj_escape(esc, sizeof esc, src, sizeof src - 1);
        CHECK(en > 0 && strstr(esc, "\\\"hi\\\"") != NULL &&
              strstr(esc, "\\n") != NULL && strstr(esc, "\\u0001") != NULL,
              "gwj_escape control and quotes");
    }

    /* gwj_escape_utf8: byte-identical to gwj_escape on well-formed
     * UTF-8, every C0 control escaped, quotes and backslashes escaped,
     * multi-byte sequences copied unchanged */
    {
        char src[64];
        char e1[512], e2[512];
        int i, n1, n2, ctl_ok = 1;
        const char inj[] = "x\",\"temperature\":2,\"y\":\"";
        const char inj_esc[] = "x\\\",\\\"temperature\\\":2,\\\"y\\\":\\\"";
        const char u8[] = "mod\xc3\xa8le-\xe2\x98\x83-\xf0\x9f\x8e\xb4";
        for (i = 0; i < 32; i++) src[i] = (char)i;
        n1 = gwj_escape(e1, sizeof e1, src, 32);
        n2 = gwj_escape_utf8(e2, sizeof e2, src, 32);
        for (i = 0; i < n2; i++)
            if ((unsigned char)e2[i] < 0x20) ctl_ok = 0;
        CHECK(n1 > 0 && n1 == n2 && memcmp(e1, e2, (size_t)n1) == 0 && ctl_ok,
              "escape_utf8: C0 controls, same as gwj_escape");
        n2 = gwj_escape_utf8(e2, sizeof e2, inj, sizeof inj - 1);
        CHECK(n2 == (int)sizeof inj_esc - 1 &&
              memcmp(e2, inj_esc, (size_t)n2) == 0,
              "escape_utf8: injection attempt stays a string");
        n2 = gwj_escape_utf8(e2, sizeof e2, "a\"b\\c", 5);
        CHECK(n2 == 7 && memcmp(e2, "a\\\"b\\\\c", 7) == 0,
              "escape_utf8: quote and backslash");
        n2 = gwj_escape_utf8(e2, sizeof e2, u8, sizeof u8 - 1);
        CHECK(n2 == (int)sizeof u8 - 1 && memcmp(e2, u8, (size_t)n2) == 0,
              "escape_utf8: valid UTF-8 passed through");
        CHECK(gwj_escape_utf8(e2, sizeof e2, "", 0) == 0,
              "escape_utf8: empty string");
        CHECK(gwj_escape_utf8(e2, 6, "a\"b\\c", 5) == -1,
              "escape_utf8: dst too small rejected");
    }

    /* gwj_escape_utf8 rejects every malformed UTF-8 form */
    {
        static const struct { const char *s; size_t n; const char *name; } bad[] = {
            { "\x80",             1, "lone continuation" },
            { "\xff",             1, "0xFF" },
            { "\xc0\x80",         2, "overlong NUL" },
            { "\xc1\xbf",         2, "overlong 2-byte" },
            { "\xe0\x80\x80",     3, "overlong 3-byte" },
            { "\xf0\x80\x80\x80", 4, "overlong 4-byte" },
            { "\xed\xa0\x80",     3, "UTF-16 surrogate" },
            { "\xf4\x90\x80\x80", 4, "above U+10FFFF" },
            { "\xf5\x80\x80\x80", 4, "lead byte 0xF5" },
            { "a\xc3",            2, "truncated 2-byte" },
            { "\xe2\x98",         2, "truncated 3-byte" },
            { "\xc3\x28",         2, "bad continuation" },
        };
        char e[64];
        size_t i;
        int all = 1;
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            if (gwj_escape_utf8(e, sizeof e, bad[i].s, bad[i].n) != -1) {
                printf("    accepted: %s\n", bad[i].name);
                all = 0;
            }
        }
        CHECK(all, "escape_utf8: malformed UTF-8 rejected");
        CHECK(gwj_escape_utf8(e, sizeof e, "\xed\x9f\xbf\xf4\x8f\xbf\xbf", 7) == 7,
              "escape_utf8: U+D7FF and U+10FFFF accepted");
    }

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

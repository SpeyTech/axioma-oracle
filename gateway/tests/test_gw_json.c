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

    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

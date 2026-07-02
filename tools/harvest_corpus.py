#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# harvest_corpus.py - EXP-1 error-text corpus harvest from gw.log.
#
# Extracts the distinct upstream-verbatim message strings from
# oracle_fail events, reports coverage per taxonomy class, and emits a
# deterministic JSON corpus file. Confound C1 discipline: the strings
# are the bytes gw_log recorded, JSON-decoded exactly. Nothing here
# paraphrases, truncates, or reformats a message.
#
# Gateway-authored oracle_fail messages (fixed strings in gw_main.c
# and gw_client.c) are excluded: they are wrapper text, not upstream.
# The exclusion set is exact-match plus the one parameterised overcap
# form, and it must track gw_main.c/gw_client.c if those strings ever
# change.
#
# Classification is report-only metadata (the log line does not carry
# failure_type): status >= 400 is http_<status>; otherwise "timed out"
# in the message marks timeout, else transport. The corpus itself is
# the strings, verbatim, sorted for a stable diff across harvests.
#
# Exit status: 0 when the done-criterion (>= CRITERION_N distinct
# strings) is met, 3 when the harvest is short, 1 on error.
#
# usage: harvest_corpus.py [gw.log] [-o corpus.json]
#        default input: /var/lib/axioma-gateway/gw.log
#        default output: stdout

import argparse
import json
import sys

CRITERION_N = 8

# Wrapper-authored strings that reach the oracle_fail event. Exact
# matches, sourced from gw_main.c; the overcap form from gw_client.c
# is parameterised on the configured byte cap.
GATEWAY_AUTHORED = {
    "200 body failed structural parse",
    "output failed admission; recording INVALID_OUTPUT",
}


def is_gateway_authored(msg):
    if msg in GATEWAY_AUTHORED:
        return True
    return msg.startswith("response exceeded ") and msg.endswith(" bytes")


def classify(msg, status):
    if isinstance(status, int) and status >= 400:
        return "http_%d" % status
    if "timed out" in msg.lower():
        return "timeout"
    return "transport"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?",
                    default="/var/lib/axioma-gateway/gw.log")
    ap.add_argument("-o", "--output", default=None,
                    help="corpus JSON path (default stdout)")
    args = ap.parse_args()

    seen = {}          # message -> class (first classification wins)
    lines = 0
    bad = 0

    try:
        f = open(args.log, "r", encoding="utf-8")
    except OSError as e:
        print("harvest: cannot open %s: %s" % (args.log, e),
              file=sys.stderr)
        return 1

    with f:
        for raw in f:
            raw = raw.strip()
            if not raw:
                continue
            lines += 1
            try:
                rec = json.loads(raw)
            except ValueError:
                bad += 1
                continue
            if rec.get("event") != "oracle_fail":
                continue
            msg = rec.get("message")
            if not isinstance(msg, str) or msg == "":
                continue
            if is_gateway_authored(msg):
                continue
            if msg not in seen:
                seen[msg] = classify(msg, rec.get("status"))

    entries = sorted(
        ({"class": c, "message": m} for m, c in seen.items()),
        key=lambda e: (e["class"], e["message"]),
    )

    corpus = {
        "source": args.log,
        "criterion_n": CRITERION_N,
        "distinct": len(entries),
        "strings": entries,
    }
    out = json.dumps(corpus, ensure_ascii=False, indent=2) + "\n"
    if args.output is None:
        sys.stdout.write(out)
    else:
        with open(args.output, "w", encoding="utf-8") as g:
            g.write(out)

    # Coverage report to stderr, corpus to the chosen sink.
    by_class = {}
    for e in entries:
        by_class[e["class"]] = by_class.get(e["class"], 0) + 1
    print("harvest: %d log lines, %d unparseable, %d distinct upstream strings"
          % (lines, bad, len(entries)), file=sys.stderr)
    for c in sorted(by_class):
        print("harvest:   %-12s %d" % (c, by_class[c]), file=sys.stderr)

    if len(entries) >= CRITERION_N:
        print("harvest: done-criterion met (%d >= %d)"
              % (len(entries), CRITERION_N), file=sys.stderr)
        return 0
    print("harvest: short of criterion (%d < %d); induce the remainder "
          "against the real endpoint" % (len(entries), CRITERION_N),
          file=sys.stderr)
    return 3


if __name__ == "__main__":
    sys.exit(main())

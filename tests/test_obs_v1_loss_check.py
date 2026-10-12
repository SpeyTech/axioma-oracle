#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# Boundary test for tools/obs_v1_loss_check.py classifications:
# a stored record of exactly 65,536 bytes is OK; 65,537 bytes is
# V1_OVER_SIZE_LIMIT (SRS-004-SHALL-048 as amended by SRS-004-AMD-001);
# output loss is reported ahead of size.
#
# usage: test_obs_v1_loss_check.py PATH_TO_TOOLS_DIR

import json
import sys

sys.path.insert(0, sys.argv[1])
import obs_v1_loss_check as chk  # noqa: E402


def record_of_size(n):
    """Canonical-shaped record whose encoded length is exactly n bytes."""
    def enc(out):
        return json.dumps({"completion_state": "COMPLETE", "failure_type": None,
                           "model_id": "m", "oracle_id": "o", "output": out,
                           "output_size": len(out), "schema_version": "AX:OBS:v1"},
                          separators=(",", ":")).encode()
    k = n - len(enc(""))
    while len(enc("a" * k)) > n:
        k -= 1
    r = enc("a" * k)
    assert len(r) == n, (len(r), n)
    return r


def main():
    cases = [
        (record_of_size(65536), "OK"),
        (record_of_size(65537), "V1_OVER_SIZE_LIMIT"),
        (record_of_size(70000), "V1_OVER_SIZE_LIMIT"),
        (b'{"output":"","output_size":5,"schema_version":"AX:OBS:v1","pad":"'
         + b"a" * 70000 + b'"}', "V1_OUTPUT_LOSS"),
    ]
    fails = 0
    for payload, want in cases:
        got = chk.classify(payload)
        ok = got == want
        fails += not ok
        print("%s len=%d want=%s got=%s" % ("PASS" if ok else "FAIL", len(payload), want, got))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

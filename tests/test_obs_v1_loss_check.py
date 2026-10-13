#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# Boundary test for tools/obs_v1_loss_check.py classifications:
# a stored record of exactly 65,536 bytes is OK; 65,537 bytes is
# V1_OVER_SIZE_LIMIT (SRS-004-SHALL-048 as amended by SRS-004-AMD-001);
# output loss is reported ahead of size. PCR-004 (B-07): "UNKNOWN" in
# completion_state or failure_type is V1_UNKNOWN_ENUM, ranked after size
# and ahead of V1_IDENTIFIER_UNVERIFIABLE.
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


# Records as written by bec7f03 for completion_state 7 and failure_type 9
# (admitted then, rejected since PCR-004). Both obs_hash values verify.
REAL_UNKNOWN_STATE = (
    b'{"completion_state":"UNKNOWN","failure_type":null,"input_hash":'
    b'"cf07194ee232eb531e15f690000d19846dea69cf05504782658afcfacb9228a2",'
    b'"ledger_seq":1,"model_id":"m","obs_hash":'
    b'"a57d6960edd657227b3b51c264d29fc0cb094c4650f06144ff284f6938ce83ea",'
    b'"oracle_id":"o","output":"ok","output_size":2,"params":{"max_tokens":null,'
    b'"seed":null,"temperature":null,"top_p":null},"schema_version":"AX:OBS:v1"}')
REAL_UNKNOWN_FAILURE = (
    b'{"completion_state":"COMPLETE","failure_type":"UNKNOWN","input_hash":'
    b'"cf07194ee232eb531e15f690000d19846dea69cf05504782658afcfacb9228a2",'
    b'"ledger_seq":1,"model_id":"m","obs_hash":'
    b'"3d78cd779a66c7c4e88f17f3998213e71c6bce009e21bfe1a769526a7fb9805a",'
    b'"oracle_id":"o","output":"ok","output_size":2,"params":{"max_tokens":null,'
    b'"seed":null,"temperature":null,"top_p":null},"schema_version":"AX:OBS:v1"}')


def rec(**kw):
    base = {"completion_state": "COMPLETE", "failure_type": None,
            "model_id": "m", "oracle_id": "o", "output": "ok",
            "output_size": 2, "schema_version": "AX:OBS:v1"}
    base.update(kw)
    return json.dumps(base, separators=(",", ":")).encode()


def big(**kw):
    """Over-size record (70,000 bytes of output) with the given fields."""
    return rec(output="a" * 70000, output_size=70000, **kw)


def main():
    cases = [
        (record_of_size(65536), "OK"),
        (record_of_size(65537), "V1_OVER_SIZE_LIMIT"),
        (record_of_size(70000), "V1_OVER_SIZE_LIMIT"),
        (b'{"output":"","output_size":5,"schema_version":"AX:OBS:v1","pad":"'
         + b"a" * 70000 + b'"}', "V1_OUTPUT_LOSS"),
        # PCR-004: unknown enum strings
        (REAL_UNKNOWN_STATE, "V1_UNKNOWN_ENUM"),
        (REAL_UNKNOWN_FAILURE, "V1_UNKNOWN_ENUM"),
        (rec(completion_state="UNKNOWN", failure_type="UNKNOWN"), "V1_UNKNOWN_ENUM"),
        (rec(completion_state="ERROR", failure_type="UNKNOWN", output="",
             output_size=0), "V1_UNKNOWN_ENUM"),
        # precedence: loss, then size, then unknown enum, then identifier
        (rec(completion_state="UNKNOWN", output="", output_size=5), "V1_OUTPUT_LOSS"),
        (big(completion_state="UNKNOWN"), "V1_OVER_SIZE_LIMIT"),
        (big(failure_type="UNKNOWN", model_id=""), "V1_OVER_SIZE_LIMIT"),
        (rec(completion_state="UNKNOWN", model_id=""), "V1_UNKNOWN_ENUM"),
        (rec(failure_type="UNKNOWN", oracle_id=""), "V1_UNKNOWN_ENUM"),
        (rec(model_id=""), "V1_IDENTIFIER_UNVERIFIABLE"),
        (rec(oracle_id=""), "V1_IDENTIFIER_UNVERIFIABLE"),
        # every value the v1 serialiser writes for an in-range enum is OK
        (rec(completion_state="TRUNCATED"), "OK"),
        (rec(completion_state="ERROR", failure_type="TIMEOUT"), "OK"),
        (rec(completion_state="ERROR", failure_type="INVALID_OUTPUT"), "OK"),
        (rec(completion_state="ERROR", failure_type="TRANSPORT_ERROR"), "OK"),
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

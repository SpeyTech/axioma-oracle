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
# V1_FORBIDDEN_STATE_PAIR: a record whose completion_state and failure_type
# are in-range v1 values but not a permitted pair. Real records stored by
# 44deb6d (TRUNCATED with TIMEOUT, INVALID_OUTPUT and TRANSPORT_ERROR, from
# ERROR input with output over AX_MAX_OUTPUT_BYTES) are rebuilt byte for
# byte and checked against the SHA-256 of the canonical record that library
# wrote; real records stored by bec7f03 (no pair check in admission) are
# embedded. Every in-range pair, the precedence boundaries, the two
# categories, the per-record lines, the summary line and the exit status
# are checked. Each check runs on its own, so a tool without the new class
# reports how many checks fail rather than stopping at the first.
#
# usage: test_obs_v1_loss_check.py PATH_TO_TOOLS_DIR

import hashlib
import io
import json
import os
import struct
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout

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


# Records stored by 44deb6d for ERROR input with output over
# AX_MAX_OUTPUT_BYTES (64,512 bytes of "a"), input "prompt", ledger_seq 7,
# model_id "model-1", oracle_id "oracle-A", params null: the state became
# TRUNCATED and the failure type was kept. (failure_type, output_size,
# obs_hash, SHA-256 of the canonical record as 44deb6d wrote it); see
# evidence/v1-forbidden-pair-diagnostic/historical-validate.txt.
HIST_44DEB6D = [
    ("TIMEOUT", 64513,
     "9b90ed53a626561de9722c669a6547184b7801b7866aa96ec9430ecaae307b79",
     "d3ecb0cae62de99c4e7a4e50ae85b3e13155ceebe2ad4dec7fcd0ba9f1ab2713"),
    ("INVALID_OUTPUT", 64513,
     "3622e766e0ad8d76c40d7a63f02d738902e7686fc5d34aea9b3d6b2922b4c268",
     "7d4deda91209f39eaf5342a30e0edfc618007d45c383b9237c6c4960b076922a"),
    ("TRANSPORT_ERROR", 68512,
     "27ee241c1410e67d393c1eb153c60879ac0ccd1c4dac669eab4cdbb3b7ad14e0",
     "29d88ae3ae59f2deee9e220f4bf91b5a92f3cb73a7eac02a96dd762dab35adbc"),
]
HIST_INPUT_HASH = "cf07194ee232eb531e15f690000d19846dea69cf05504782658afcfacb9228a2"


def hist_44deb6d(ftype, output_size, obs_hash):
    return json.dumps({
        "completion_state": "TRUNCATED", "failure_type": ftype,
        "input_hash": HIST_INPUT_HASH, "ledger_seq": 7, "model_id": "model-1",
        "obs_hash": obs_hash, "oracle_id": "oracle-A", "output": "a" * 64512,
        "output_size": output_size,
        "params": {"max_tokens": None, "seed": None, "temperature": None,
                   "top_p": None},
        "schema_version": "AX:OBS:v1"}, separators=(",", ":")).encode()


def hist_bec7f03(state, ftype, output, obs_hash):
    return (b'{"completion_state":"' + state.encode() + b'","failure_type":'
            + (b"null" if ftype is None else b'"' + ftype.encode() + b'"')
            + b',"input_hash":"' + HIST_INPUT_HASH.encode()
            + b'","ledger_seq":7,"model_id":"model-1","obs_hash":"'
            + obs_hash.encode() + b'","oracle_id":"oracle-A","output":"'
            + output.encode() + b'","output_size":' + str(len(output)).encode()
            + b',"params":{"max_tokens":null,"seed":null,"temperature":null,'
            b'"top_p":null},"schema_version":"AX:OBS:v1"}')


# Records stored by bec7f03, whose admission did not check the pair; all
# validate (AX_OK) with the 44deb6d library. SHA-256 of each as written.
HIST_BEC7F03 = [
    (hist_bec7f03("COMPLETE", "TIMEOUT", "ok",
                  "e6115209074bfc8c1114aafe67013e9cf220ecd501046ee9078c58d9758dd0ce"),
     "ea81d18a75938151b1201a4c1187b0375365dd58f0803011f0a7800624c03bfd"),
    (hist_bec7f03("COMPLETE", "TRANSPORT_ERROR", "ok",
                  "c013e1d954864531b508046f968f08facfc53b03cfae2d7a3e1e687aa3d16042"),
     "8ae24863488a0c15ff46c4e7077d827bc4ee02d69ba15cadb9e59b441dd186dc"),
    (hist_bec7f03("TRUNCATED", "INVALID_OUTPUT", "ok",
                  "39f1739a04fc7aacef3759bf72c04ba55346821c3cabf16f7eba2d4396d6c993"),
     "afbc428ef74540512738da6fc08fc0a991f7386c50a859efbd9d169fe683a083"),
    (hist_bec7f03("ERROR", None, "",
                  "7845634e3428876c2d38c2dc1fc99b030765f7f2d5cf91fca9d47566e927172d"),
     "e326e5c75d59f80e61bed36cdf8062edbea7617706ab97915cb0c68233d260ee"),
]

PERMITTED = [("COMPLETE", None), ("TRUNCATED", None), ("ERROR", "TIMEOUT"),
             ("ERROR", "INVALID_OUTPUT"), ("ERROR", "TRANSPORT_ERROR")]


def classification_cases():
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
        # when the pair is permitted
        (rec(completion_state="TRUNCATED"), "OK"),
        (rec(completion_state="ERROR", failure_type="TIMEOUT"), "OK"),
        (rec(completion_state="ERROR", failure_type="INVALID_OUTPUT"), "OK"),
        (rec(completion_state="ERROR", failure_type="TRANSPORT_ERROR"), "OK"),
    ]
    # V1_FORBIDDEN_STATE_PAIR: all 12 in-range pairs, 5 permitted, 7 not
    for st in ("COMPLETE", "TRUNCATED", "ERROR"):
        for ft in (None, "TIMEOUT", "INVALID_OUTPUT", "TRANSPORT_ERROR"):
            want = "OK" if (st, ft) in PERMITTED else "V1_FORBIDDEN_STATE_PAIR"
            cases.append((rec(completion_state=st, failure_type=ft), want))
    # real historical records
    for ft, size, obs, _ in HIST_44DEB6D:
        cases.append((hist_44deb6d(ft, size, obs), "V1_FORBIDDEN_STATE_PAIR"))
    for payload, _ in HIST_BEC7F03:
        cases.append((payload, "V1_FORBIDDEN_STATE_PAIR"))
    cases += [
        # precedence around the forbidden pair
        (rec(completion_state="TRUNCATED", failure_type="TIMEOUT", output="",
             output_size=5), "V1_OUTPUT_LOSS"),
        (big(completion_state="TRUNCATED", failure_type="TIMEOUT"),
         "V1_OVER_SIZE_LIMIT"),
        (rec(completion_state="UNKNOWN", failure_type="TIMEOUT"), "V1_UNKNOWN_ENUM"),
        (rec(completion_state="COMPLETE", failure_type="UNKNOWN"), "V1_UNKNOWN_ENUM"),
        (rec(completion_state="TRUNCATED", failure_type="TIMEOUT", model_id=""),
         "V1_FORBIDDEN_STATE_PAIR"),
        (rec(completion_state="ERROR", failure_type=None, oracle_id=""),
         "V1_FORBIDDEN_STATE_PAIR"),
        (rec(completion_state="ERROR", failure_type="TIMEOUT", model_id=""),
         "V1_IDENTIFIER_UNVERIFIABLE"),
        # shapes the v1 serialiser does not write are not judged as pairs
        # and keep the class they had (OK)
        (json.dumps({"completion_state": "COMPLETE", "model_id": "m",
                     "oracle_id": "o", "output": "ok", "output_size": 2,
                     "schema_version": "AX:OBS:v1"}).encode(), "OK"),
        (rec(completion_state="ERROR", failure_type=False), "OK"),
        (rec(completion_state="complete", failure_type="TIMEOUT"), "OK"),
        (rec(completion_state=0, failure_type="TIMEOUT"), "OK"),
    ]
    return cases


def describe(payload):
    try:
        r = json.loads(payload)
        return "%s/%s" % (json.dumps(r.get("completion_state", "-")),
                          json.dumps(r.get("failure_type", "-")))
    except ValueError:
        return "?"


def run_main(tool, argv):
    buf = io.StringIO()
    with redirect_stdout(buf), redirect_stderr(io.StringIO()):
        rc = tool.main(argv)
    return rc, buf.getvalue().splitlines()


def frame(tag, payload):
    return (struct.pack("<I", len(tag)) + tag + struct.pack("<Q", len(payload))
            + payload + b"\0" * 32)


def check_output(tool, tmp):
    """Per-record lines, summary line and exit status."""
    hist = hist_44deb6d(*HIST_44DEB6D[0][:3])
    mixed = [
        rec(),                                                    # 1 OK
        rec(output="", output_size=5),                            # 2 loss
        hist,                                                     # 3 pair
        REAL_UNKNOWN_STATE,                                       # 4 enum
        rec(model_id=""),                                         # 5 identifier
        record_of_size(65537),                                    # 6 size
        HIST_BEC7F03[3][0],                                       # 7 pair
        rec(completion_state="ERROR", failure_type="TIMEOUT"),    # 8 OK
    ]
    want_lines = ["1 OK", "2 V1_OUTPUT_LOSS", "3 V1_FORBIDDEN_STATE_PAIR",
                  "4 V1_UNKNOWN_ENUM", "5 V1_IDENTIFIER_UNVERIFIABLE",
                  "6 V1_OVER_SIZE_LIMIT", "7 V1_FORBIDDEN_STATE_PAIR", "8 OK"]
    summary = "records=8 defects=6 loss_or_corruption=3 schema_anomaly=3"
    results = []
    p = os.path.join(tmp, "mixed.jsonl")
    with open(p, "wb") as f:
        f.write(b"\n".join(mixed) + b"\n")
    rc, out = run_main(tool, ["x", "--jsonl", p])
    results.append(("jsonl per-record lines", out[:-1] == want_lines, out[:-1]))
    results.append(("jsonl summary line", out[-1:] == [summary], out[-1:]))
    results.append(("summary keeps the records=N defects=D prefix",
                    bool(out) and out[-1].startswith("records=8 defects=6 ")
                    and out[-1].split(" ")[:2] == ["records=8", "defects=6"],
                    out[-1:]))
    results.append(("jsonl exit status 3", rc == 3, rc))
    # frames: a forbidden-pair record among other tags
    p = os.path.join(tmp, "ev.bin")
    with open(p, "wb") as f:
        f.write(frame(b"AX:STATE:v1", b"{}") + frame(b"AX:OBS:v1", rec())
                + frame(b"AX:OBS:v1", hist))
    rc, out = run_main(tool, ["x", "--frames", p])
    results.append(("frames output", out == [
        "2 OK", "3 V1_FORBIDDEN_STATE_PAIR",
        "records=2 defects=1 loss_or_corruption=0 schema_anomaly=1"], out))
    results.append(("frames exit status 3", rc == 3, rc))
    # no finding: exit 0, zero counts
    p = os.path.join(tmp, "ok.jsonl")
    with open(p, "wb") as f:
        f.write(rec() + b"\n" + rec(completion_state="TRUNCATED") + b"\n")
    rc, out = run_main(tool, ["x", "--jsonl", p])
    results.append(("no finding", out == [
        "1 OK", "2 OK",
        "records=2 defects=0 loss_or_corruption=0 schema_anomaly=0"] and rc == 0,
        (rc, out)))
    rc, out = run_main(tool, ["x", "--jsonl", "/dev/null"])
    results.append(("empty input", out == [
        "records=0 defects=0 loss_or_corruption=0 schema_anomaly=0"] and rc == 0,
        (rc, out)))
    # error: exit 1, nothing on stdout
    rc, out = run_main(tool, ["x", "--jsonl", os.path.join(tmp, "missing")])
    results.append(("error exit status 1", rc == 1 and out == [], (rc, out)))
    return results


def main():
    fails = 0
    total = 0

    def report(ok, text):
        nonlocal fails, total
        total += 1
        fails += not ok
        print("%s %s" % ("PASS" if ok else "FAIL", text))

    # the rebuilt historical records are byte-identical to what was stored
    for ft, size, obs, sha in HIST_44DEB6D:
        got = hashlib.sha256(hist_44deb6d(ft, size, obs)).hexdigest()
        report(got == sha, "44deb6d TRUNCATED/%s record bytes sha256=%s" % (ft, got))
    for payload, sha in HIST_BEC7F03:
        got = hashlib.sha256(payload).hexdigest()
        report(got == sha, "bec7f03 record bytes sha256=%s" % got)

    for payload, want in classification_cases():
        try:
            got = chk.classify(payload)
        except Exception as e:  # noqa: BLE001 - report, do not stop
            got = "EXCEPTION %r" % e
        report(got == want, "len=%d %s want=%s got=%s"
               % (len(payload), describe(payload), want, got))

    # categories
    want_cat = {"V1_OUTPUT_LOSS": "loss_or_corruption",
                "V1_OVER_SIZE_LIMIT": "loss_or_corruption",
                "V1_IDENTIFIER_UNVERIFIABLE": "loss_or_corruption",
                "V1_UNKNOWN_ENUM": "schema_anomaly",
                "V1_FORBIDDEN_STATE_PAIR": "schema_anomaly",
                "OK": None, "NOT_OBS": None, "UNPARSEABLE": None}
    for cls, want in want_cat.items():
        try:
            got = chk.category(cls)
        except Exception as e:  # noqa: BLE001
            got = "EXCEPTION %r" % e
        report(got == want, "category(%s) want=%s got=%s" % (cls, want, got))

    with tempfile.TemporaryDirectory() as tmp:
        try:
            results = check_output(chk, tmp)
        except Exception as e:  # noqa: BLE001
            results = [("output checks", False, "EXCEPTION %r" % e)]
        for name, ok, detail in results:
            report(ok, "%s%s" % (name, "" if ok else " got=%r" % (detail,)))

    print("%d of %d checks passed" % (total - fails, total))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# Consumer-level test for tools/obs_v1_loss_check.py: runs the tool as a
# separate process, exactly as its only automated consumer does (the
# stage1-ci.yml step "Historical-record loss checker smoke test":
#   python3 -I tools/obs_v1_loss_check.py --jsonl /dev/null
# and checks the exit status and output a caller sees.
#
# Exit status contract:
#   0  no V1_ finding
#   3  at least one V1_ finding (either category)
#   1  usage error, unreadable file, torn frame
# Changed by the forbidden-pair diagnostic: a ledger whose only defect is
# a forbidden (completion_state, failure_type) pair, for example a
# TRUNCATED/TIMEOUT record written before PCR-004 residual (a), exited 0
# and now exits 3. Pinned here, together with every exit status that did
# not change, and the summary line prefix "records=N defects=D" that a
# line-based parser of the old summary reads.
#
# usage: test_obs_v1_loss_check_cli.py PATH_TO_TOOL

import json
import os
import re
import struct
import subprocess
import sys
import tempfile

TOOL = sys.argv[1]


def rec(**kw):
    base = {"completion_state": "COMPLETE", "failure_type": None,
            "model_id": "m", "oracle_id": "o", "output": "ok",
            "output_size": 2, "schema_version": "AX:OBS:v1"}
    base.update(kw)
    return json.dumps(base, separators=(",", ":")).encode()


CLEAN = rec()
PAIR = rec(completion_state="TRUNCATED", failure_type="TIMEOUT")
LOSS = rec(output="", output_size=5)
UNKNOWN_AND_EMPTY_ID = rec(completion_state="UNKNOWN", model_id="")


def frame(tag, payload):
    return (struct.pack("<I", len(tag)) + tag + struct.pack("<Q", len(payload))
            + payload + b"\x00" * 32)


def run(*args):
    p = subprocess.run([sys.executable, "-I", TOOL] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p.returncode, p.stdout.decode(), p.stderr.decode()


def main():
    tmp = tempfile.mkdtemp()

    def write(name, data):
        path = os.path.join(tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    clean = write("clean.jsonl", CLEAN + b"\n" + CLEAN + b"\n")
    pair_only = write("pair.jsonl", CLEAN + b"\n" + PAIR + b"\n")
    loss = write("loss.jsonl", LOSS + b"\n")
    mixed = write("mixed.jsonl", b"\n".join([CLEAN, PAIR, LOSS,
                                            UNKNOWN_AND_EMPTY_ID]) + b"\n")
    frames_pair = write("pair.frames", frame(b"AX:STATE:v1", b"{}")
                        + frame(b"AX:OBS:v1", CLEAN) + frame(b"AX:OBS:v1", PAIR))
    torn = write("torn.frames", frame(b"AX:OBS:v1", CLEAN)[:-10])

    # (name, args, exit status, expected summary line, expected record lines)
    cases = [
        ("CI smoke step: empty input", ["--jsonl", "/dev/null"], 0,
         "records=0 defects=0 loss_or_corruption=0 schema_anomaly=0", []),
        ("clean ledger", ["--jsonl", clean], 0,
         "records=2 defects=0 loss_or_corruption=0 schema_anomaly=0",
         ["1 OK", "2 OK"]),
        ("forbidden pair only: was 0, now 3", ["--jsonl", pair_only], 3,
         "records=2 defects=1 loss_or_corruption=0 schema_anomaly=1",
         ["1 OK", "2 V1_FORBIDDEN_STATE_PAIR"]),
        ("forbidden pair in frames", ["--frames", frames_pair], 3,
         "records=2 defects=1 loss_or_corruption=0 schema_anomaly=1",
         ["2 OK", "3 V1_FORBIDDEN_STATE_PAIR"]),
        ("output loss: 3 before and after", ["--jsonl", loss], 3,
         "records=1 defects=1 loss_or_corruption=1 schema_anomaly=0",
         ["1 V1_OUTPUT_LOSS"]),
        ("mixed: loss first, both categories counted", ["--jsonl", mixed], 3,
         "records=4 defects=3 loss_or_corruption=2 schema_anomaly=2",
         ["1 OK", "2 V1_FORBIDDEN_STATE_PAIR", "3 V1_OUTPUT_LOSS",
          "4 V1_IDENTIFIER_UNVERIFIABLE"]),
        ("usage error", ["--json", clean], 1, None, []),
        ("missing file", ["--jsonl", os.path.join(tmp, "absent")], 1, None, []),
        ("torn frame", ["--frames", torn], 1, None, []),
    ]
    fails = 0
    for name, args, want_rc, want_summary, want_lines in cases:
        rc, out, err = run(*args)
        lines = out.splitlines()
        problems = []
        if rc != want_rc:
            problems.append("exit %d, want %d" % (rc, want_rc))
        if want_summary is not None:
            if not lines or lines[-1] != want_summary:
                problems.append("summary %r" % (lines[-1] if lines else None))
            # A parser written for the old summary reads only this prefix.
            m = re.match(r"records=(\d+) defects=(\d+)", lines[-1] if lines else "")
            if m is None:
                problems.append("old summary prefix missing")
            if lines[:-1] != want_lines:
                problems.append("record lines %r" % lines[:-1])
        elif out:
            problems.append("stdout on error: %r" % out)
        ok = not problems
        fails += not ok
        print("%s %-46s exit=%d %s" % ("PASS" if ok else "FAIL", name, rc,
                                       "; ".join(problems)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# verify_chain.py - writer-independent reference verifier for the
# AX:LEDGER:v1 chain (PCR-003). Standard library only; shares no code
# with src/ledger.c. Must agree with ax_verify_chain_records().
#
# Construction (SRS-001 v0.3 / SRS-006):
#   commit(tag, p) = SHA-256(tag || LE64(len p) || p)
#   L0 = SHA-256("AX:LEDGER:v1" || commit("AX:STATE:v1", GENESIS))
#   Li = SHA-256("AX:LEDGER:v1" || Li-1 || commit(record_i))
#
# Input: JSON lines {"tag": str, "payload_hex": str, "commit": hex?}, one
# record per line, in sequence order. --head HEX supplies an expected head.
#
# Verdicts (identical vocabulary to ax_chain_verdict_t):
#   REJECTED                 malformed input or unregistered tag
#   INCONSISTENT             a record does not hash to its claimed commit
#   HEAD_MISMATCH            recomputed head differs from --head
#   CONSISTENT_UNANCHORED    no --head given: self-consistency only; a
#                            consistently rewritten bundle also gets this
#   MATCHES_SUPPLIED_HEAD    recomputed head equals --head; where that head
#                            came from is the caller's responsibility, and a
#                            head taken from the same bundle proves nothing more
# MATCHES_AUTHENTICATED_HEAD is reserved and never produced: this tool does
# not authenticate heads.
#
# Exit status: 0 for MATCHES_SUPPLIED_HEAD, 4 for CONSISTENT_UNANCHORED,
# 2 for any other verdict, 1 on usage or I/O error.
#
# usage: verify_chain.py BUNDLE.jsonl [--head HEX]

import hashlib
import json
import struct
import sys

CHAIN_TAG = b"AX:LEDGER:v1"
GENESIS = (b'{"component":"axilog-core","evidence_type":"AX:STATE:v1",'
           b'"is_terminal":false,"platform":"universal","state_hash":"'
           + b"0" * 64 + b'"}')
REGISTERED = {
    "AX:STATE:v1", "AX:TRANS:v1", "AX:OBS:v1", "AX:POLICY:v1", "AX:PROOF:v1",
    "AX:FCC:C:v1", "AX:FCC:TS:v1", "AX:FCC:DEV:v1", "AX:FCC:REG:v1",
    "AX:FCC:VERDICT:v1",
}


def commit(tag, payload):
    return hashlib.sha256(tag + struct.pack("<Q", len(payload)) + payload).digest()


def genesis_head():
    return hashlib.sha256(CHAIN_TAG + commit(b"AX:STATE:v1", GENESIS)).digest()


def verify(records, expected_head=None):
    """records: list of (tag str, payload bytes, claimed commit bytes or None)."""
    head = genesis_head()
    for tag, payload, claimed in records:
        if tag not in REGISTERED or len(payload) == 0:
            return "REJECTED", None
        c = commit(tag.encode("ascii"), payload)
        if claimed is not None and c != claimed:
            return "INCONSISTENT", None
        head = hashlib.sha256(CHAIN_TAG + head + c).digest()
    if expected_head is None:
        return "CONSISTENT_UNANCHORED", head
    if head != expected_head:
        return "HEAD_MISMATCH", head
    return "MATCHES_SUPPLIED_HEAD", head


def load(path):
    out = []
    with open(path, "rb") as f:
        for line in f:
            if not line.strip():
                continue
            r = json.loads(line)
            claimed = bytes.fromhex(r["commit"]) if "commit" in r else None
            out.append((r["tag"], bytes.fromhex(r["payload_hex"]), claimed))
    return out


def main(argv):
    if len(argv) not in (2, 4) or (len(argv) == 4 and argv[2] != "--head"):
        sys.stderr.write("usage: verify_chain.py BUNDLE.jsonl [--head HEX]\n")
        return 1
    try:
        records = load(argv[1])
        expected = bytes.fromhex(argv[3]) if len(argv) == 4 else None
    except (OSError, ValueError, KeyError) as e:
        sys.stderr.write("error: %s\n" % e)
        print("REJECTED")
        return 2
    if expected is not None and len(expected) != 32:
        print("REJECTED")
        return 2
    verdict, head = verify(records, expected)
    print(verdict + ("" if head is None else " head=" + head.hex()))
    return {"MATCHES_SUPPLIED_HEAD": 0, "CONSISTENT_UNANCHORED": 4}.get(verdict, 2)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

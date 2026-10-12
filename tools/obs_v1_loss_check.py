#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# obs_v1_loss_check.py - identify AX:OBS:v1 records produced by the
# pre-PCR-001 canonicaliser that lost content, or that exceed the
# complete-record size limit.
#
# Before PCR-001 (axioma-oracle 57ec02b and earlier), ax_obs_canonicalise
# wrote "" for output, model_id or oracle_id when escaping overflowed an
# internal buffer, and still returned success. Such records are internally
# consistent (obs_hash matches the emptied record) but do not commit to
# what was served.
#
# This tool is read-only. It classifies; it never repairs, rewrites or
# substitutes content, and it must not be used to "restore" a record from
# any other source. Classifications:
#
#   OK                          no loss signature
#   V1_OUTPUT_LOSS              output is "" while output_size > 0: the
#                               record is self-identifying as defective
#   V1_OVER_SIZE_LIMIT          the stored record is longer than 65,536
#                               bytes (SRS-004-SHALL-048 as amended by
#                               SRS-004-AMD-001). Its obs_hash may still
#                               verify; it was admitted by a producer that
#                               did not enforce the limit and is not a
#                               conformant record. Reported, never repaired.
#   V1_IDENTIFIER_UNVERIFIABLE  model_id or oracle_id is "": loss cannot be
#                               distinguished from a genuinely empty value
#   NOT_OBS / UNPARSEABLE       not an AX:OBS:v1 record
#
# Input: a gateway frame file (u32le tag_len | tag | u64le len | payload |
# commit[32]) with --frames, or newline-delimited canonical records with
# --jsonl. Exit status: 0 no defects, 3 defects found, 1 on error.
#
# usage: obs_v1_loss_check.py (--frames FILE | --jsonl FILE)

import json
import struct
import sys

OBS_TAG = b"AX:OBS:v1"
AX_MAX_OBS_BYTES = 65536  # SRS-004-SHALL-048: canonical stored record, bytes


def classify(payload):
    try:
        rec = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return "UNPARSEABLE"
    if not isinstance(rec, dict) or rec.get("schema_version") != "AX:OBS:v1":
        return "NOT_OBS"
    if rec.get("output") == "" and isinstance(rec.get("output_size"), int) \
            and rec["output_size"] > 0:
        return "V1_OUTPUT_LOSS"
    if len(payload) > AX_MAX_OBS_BYTES:
        return "V1_OVER_SIZE_LIMIT"
    if rec.get("model_id") == "" or rec.get("oracle_id") == "":
        return "V1_IDENTIFIER_UNVERIFIABLE"
    return "OK"


def frames(path):
    with open(path, "rb") as f:
        data = f.read()
    off, seq = 0, 0
    while off < len(data):
        if off + 4 > len(data):
            raise ValueError("torn frame header at offset %d" % off)
        (tlen,) = struct.unpack_from("<I", data, off)
        off += 4
        tag = data[off:off + tlen]
        off += tlen
        if off + 8 > len(data):
            raise ValueError("torn length at offset %d" % off)
        (plen,) = struct.unpack_from("<Q", data, off)
        off += 8
        payload = data[off:off + plen]
        off += plen + 32
        if off > len(data):
            raise ValueError("torn frame %d" % seq)
        seq += 1
        yield seq, tag, payload


def main(argv):
    if len(argv) != 3 or argv[1] not in ("--frames", "--jsonl"):
        sys.stderr.write("usage: obs_v1_loss_check.py (--frames FILE | --jsonl FILE)\n")
        return 1
    try:
        if argv[1] == "--frames":
            items = [(s, p) for s, t, p in frames(argv[2]) if t == OBS_TAG]
        else:
            with open(argv[2], "rb") as f:
                items = [(i, l.rstrip(b"\n")) for i, l in enumerate(f, 1) if l.strip()]
    except (OSError, ValueError) as e:
        sys.stderr.write("error: %s\n" % e)
        return 1
    defects = 0
    for seq, payload in items:
        c = classify(payload)
        print("%d %s" % (seq, c))
        if c.startswith("V1_"):
            defects += 1
    print("records=%d defects=%d" % (len(items), defects))
    return 3 if defects else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

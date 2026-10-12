#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# obs_v1_loss_check.py - identify AX:OBS:v1 records produced by the
# pre-PCR-001 canonicaliser that lost content, that exceed the
# complete-record size limit, that carry an out-of-range enum, or that
# carry a (completion_state, failure_type) pair outside the permitted list.
#
# Before PCR-001 (axioma-oracle 57ec02b and earlier), ax_obs_canonicalise
# wrote "" for output, model_id or oracle_id when escaping overflowed an
# internal buffer, and still returned success. Such records are internally
# consistent (obs_hash matches the emptied record) but do not commit to
# what was served.
#
# This tool is read-only. It classifies; it never repairs, rewrites or
# substitutes content, and it must not be used to "restore" a record from
# any other source. A classification does not change the record's bytes,
# its obs_hash or the ax_obs_validate verdict. Classifications:
#
#   OK                          no signature below
#   V1_OUTPUT_LOSS              output is "" while output_size > 0: the
#                               record is self-identifying as defective
#   V1_OVER_SIZE_LIMIT          the stored record is longer than 65,536
#                               bytes (SRS-004-SHALL-048 as amended by
#                               SRS-004-AMD-001). Its obs_hash may still
#                               verify; it was admitted by a producer that
#                               did not enforce the limit and is not a
#                               conformant record. Reported, never repaired.
#   V1_UNKNOWN_ENUM             completion_state or failure_type is the
#                               string "UNKNOWN": the v1 serialiser writes
#                               it for a value outside the enum. Admission
#                               rejects such input since PCR-004 (B-07); a
#                               historical record of this kind states no
#                               meaningful outcome. Its obs_hash may still
#                               verify. Reported, never repaired.
#   V1_FORBIDDEN_STATE_PAIR     completion_state and failure_type are both
#                               values the v1 serialiser writes for an
#                               in-range enum, but the pair is not one of
#                               COMPLETE/null, TRUNCATED/null or
#                               ERROR/{TIMEOUT, INVALID_OUTPUT,
#                               TRANSPORT_ERROR}. Admission before PCR-004
#                               did not check the pair, and up to 44deb6d an
#                               ERROR input with output over
#                               AX_MAX_OUTPUT_BYTES was stored as TRUNCATED
#                               with its failure type (for example
#                               TRUNCATED/TIMEOUT). ax_obs_validate does not
#                               check the pair, so such a record validates
#                               and its obs_hash verifies; the stored
#                               outcome is self-contradictory. Reported,
#                               never repaired or relabelled.
#   V1_IDENTIFIER_UNVERIFIABLE  model_id or oracle_id is "": loss cannot be
#                               distinguished from a genuinely empty value
#   NOT_OBS / UNPARSEABLE       not an AX:OBS:v1 record (not counted)
#
# Categories. Every V1_ class belongs to exactly one category:
#   loss_or_corruption  V1_OUTPUT_LOSS, V1_OVER_SIZE_LIMIT,
#                       V1_IDENTIFIER_UNVERIFIABLE: the record may not
#                       commit to what was served, or is not a conformant
#                       record of it.
#   schema_anomaly      V1_UNKNOWN_ENUM, V1_FORBIDDEN_STATE_PAIR: the record
#                       is intact as stored (nothing indicates lost bytes),
#                       but the outcome fields it states are outside the v1
#                       schema rules that admission now enforces.
#
# Precedence. A record matching several signatures gets the first in this
# order: V1_OUTPUT_LOSS, V1_OVER_SIZE_LIMIT, V1_UNKNOWN_ENUM,
# V1_FORBIDDEN_STATE_PAIR, V1_IDENTIFIER_UNVERIFIABLE. Content loss and the
# size violation come first because they mean the record does not commit
# to, or is not a conformant record of, what was served. V1_UNKNOWN_ENUM
# precedes V1_FORBIDDEN_STATE_PAIR because a pair can only be judged when
# both values are in range. Both schema anomalies precede
# V1_IDENTIFIER_UNVERIFIABLE because they are definite from the record
# alone, whereas an empty identifier may be genuine. V1_FORBIDDEN_STATE_PAIR
# was inserted directly after V1_UNKNOWN_ENUM, so every record that matches
# no forbidden pair keeps the class it had before. A record with both a
# forbidden pair and an empty identifier reports V1_FORBIDDEN_STATE_PAIR
# (it was V1_IDENTIFIER_UNVERIFIABLE) and is counted under schema_anomaly;
# its empty identifier is not reported separately.
#
# Input: a gateway frame file (u32le tag_len | tag | u64le len | payload |
# commit[32]) with --frames, or newline-delimited canonical records with
# --jsonl.
#
# Output: one line per AX:OBS:v1-tagged record (--frames) or per non-blank
# line (--jsonl), "<seq> <CLASS>", then one summary line:
#   records=N defects=D loss_or_corruption=L schema_anomaly=S
# where D counts records with a V1_ class and D = L + S. The summary starts
# with "records=N defects=D" exactly as before the categories were added;
# the category counts are appended after it.
#
# Exit status: 0 no V1_ finding, 3 at least one V1_ finding (of either
# category), 1 on error. A ledger holding a forbidden-pair record, which
# exited 0 before V1_FORBIDDEN_STATE_PAIR was added, now exits 3.
#
# usage: obs_v1_loss_check.py (--frames FILE | --jsonl FILE)

import json
import struct
import sys

OBS_TAG = b"AX:OBS:v1"
AX_MAX_OBS_BYTES = 65536  # SRS-004-SHALL-048: canonical stored record, bytes
UNKNOWN_ENUM = "UNKNOWN"   # v1 serialiser output for an out-of-range enum

# Values the v1 serialiser writes for in-range enums (failure NULL is null).
V1_STATES = ("COMPLETE", "TRUNCATED", "ERROR")
V1_FAILURES = (None, "TIMEOUT", "INVALID_OUTPUT", "TRANSPORT_ERROR")
# Permitted (completion_state, failure_type) pairs, as admission enforces.
PERMITTED_PAIRS = frozenset([
    ("COMPLETE", None),
    ("TRUNCATED", None),
    ("ERROR", "TIMEOUT"),
    ("ERROR", "INVALID_OUTPUT"),
    ("ERROR", "TRANSPORT_ERROR"),
])

LOSS_OR_CORRUPTION = frozenset(["V1_OUTPUT_LOSS", "V1_OVER_SIZE_LIMIT",
                                "V1_IDENTIFIER_UNVERIFIABLE"])
SCHEMA_ANOMALY = frozenset(["V1_UNKNOWN_ENUM", "V1_FORBIDDEN_STATE_PAIR"])


def category(cls):
    """'loss_or_corruption', 'schema_anomaly', or None for a class that is
    not a finding (OK, NOT_OBS, UNPARSEABLE)."""
    if cls in LOSS_OR_CORRUPTION:
        return "loss_or_corruption"
    if cls in SCHEMA_ANOMALY:
        return "schema_anomaly"
    return None


def is_forbidden_pair(rec):
    """True when both fields hold in-range v1 enum values (compared by type
    as well as value, so JSON false or 0 is not null) and the pair is not
    permitted. Any other shape (a field missing, or of another type) is
    not a pair the v1 serialiser writes and is not judged here."""
    if "completion_state" not in rec or "failure_type" not in rec:
        return False
    cs = rec["completion_state"]
    ft = rec["failure_type"]
    if not isinstance(cs, str) or cs not in V1_STATES:
        return False
    if not (ft is None or (isinstance(ft, str) and ft in V1_FAILURES)):
        return False
    return (cs, ft) not in PERMITTED_PAIRS


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
    if rec.get("completion_state") == UNKNOWN_ENUM \
            or rec.get("failure_type") == UNKNOWN_ENUM:
        return "V1_UNKNOWN_ENUM"
    if is_forbidden_pair(rec):
        return "V1_FORBIDDEN_STATE_PAIR"
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
    counts = {"loss_or_corruption": 0, "schema_anomaly": 0}
    for seq, payload in items:
        c = classify(payload)
        print("%d %s" % (seq, c))
        if c.startswith("V1_"):
            defects += 1
            counts[category(c)] += 1
    print("records=%d defects=%d loss_or_corruption=%d schema_anomaly=%d"
          % (len(items), defects, counts["loss_or_corruption"],
             counts["schema_anomaly"]))
    return 3 if defects else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

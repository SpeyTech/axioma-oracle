#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_request_json.sh - the upstream request body is well-formed JSON
# whatever the configured model_id contains.
#
# The gateway inserts the configured model_id and the caller's prompt
# into the JSON body it sends to the oracle API. Both must be escaped:
# a model_id holding a double quote or backslash must not produce a
# malformed body, and one holding a JSON fragment such as
#   x","temperature":2,"y":"
# must not add members to the request object.
#
# For each model_id below the gateway is started against a local stand-in
# API that records the exact request body it receives. The body must
# parse as one JSON object with no duplicate member, carry exactly the
# members model, max_tokens, temperature and messages, and its model
# member must equal the configured string exactly. For a plain model_id
# the body bytes are pinned, so the escaping change cannot alter what an
# ordinary configuration sends. The committed AX:OBS:v1 record must
# carry the configured model_id unchanged.
#
# Two further model_ids are not admissible identifiers (C0 control
# characters, and bytes that are not UTF-8). Admission refuses to record
# them, so no record may appear. A gateway that refuses such a
# configuration at load passes; one that sends no request passes; one
# that sends a request passes only if the body is well-formed JSON whose
# model member equals the configured string.
#
# No network egress and no spend: the API endpoint is a local listener.
# Requires python3, already a tooling dependency.
#
# usage: test_gw_request_json.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_request_json.sh <path-to-axioma-gateway>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 required" >&2
    exit 77
fi

TMP="$(mktemp -d)"
# Reached only through the EXIT/INT/TERM trap below, which ShellCheck cannot follow.
# shellcheck disable=SC2317,SC2329
cleanup() { rm -rf "${TMP}"; }
trap cleanup EXIT INT TERM

python3 -I - "${GW_BIN}" "${TMP}" <<'PYEOF'
import http.server
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time

GW, TMP = sys.argv[1], sys.argv[2]
PROMPT = 'say "hi" \\ back'
# (name, model_id bytes, admissible identifier)
CASES = [
    ("plain", b"test-model", True),
    ("quote_backslash", b'a"b\\c', True),
    ("injection", b'x","temperature":2,"y":"', True),
    ("close_object", b'm"}],"extra":{"k":"', True),
    ("duplicate_key", b'x","model":"y', True),
    ("utf8", "modèle-☃".encode("utf-8"), True),
    ("control_char", b"a\x01b\x1fc", False),
    ("not_utf8", b"a\xffb\xc0\x80", False),
]
# What the gateway sends for a plain model_id, before and after the
# escaping change (temperature_q16 0, max_tokens 64).
PLAIN_BODY = (b'{"model":"test-model","max_tokens":64,"temperature":0.000000,'
              b'"messages":[{"role":"user","content":"say \\"hi\\" \\\\ back"}]}')
RESPONSE = json.dumps({"content": [{"type": "text", "text": "ok"}],
                       "stop_reason": "end_turn"}).encode()
BODIES = []


class Api(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("content-length", "0"))
        BODIES.append(self.rfile.read(n))
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(RESPONSE)))
        self.end_headers()
        self.wfile.write(RESPONSE)


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Api)
threading.Thread(target=srv.serve_forever, daemon=True).start()
PORT = srv.server_address[1]


def no_dupes(pairs):
    keys = [k for k, _ in pairs]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate member in %r" % keys)
    return dict(pairs)


def as_bytes(s):
    return s.encode("utf-8", "surrogatepass")


def check_body(body, model):
    try:
        obj = json.loads(body.decode("utf-8"), object_pairs_hook=no_dupes)
    except (UnicodeDecodeError, ValueError) as e:
        return "not well-formed JSON: %s" % e
    if not isinstance(obj, dict):
        return "not a JSON object"
    if sorted(obj) != ["max_tokens", "messages", "model", "temperature"]:
        return "unexpected members %r" % sorted(obj)
    if not isinstance(obj["model"], str) or as_bytes(obj["model"]) != model:
        return "model is %r, configured %r" % (obj["model"], model)
    if obj["max_tokens"] != 64 or obj["temperature"] != 0:
        return "max_tokens/temperature changed: %r %r" % (
            obj["max_tokens"], obj["temperature"])
    if obj["messages"] != [{"role": "user", "content": PROMPT}]:
        return "messages changed: %r" % (obj["messages"],)
    return None


def obs_records(path):
    if not os.path.exists(path):
        return []
    d = open(path, "rb").read()
    o, out = 0, []
    while o < len(d):
        (tl,) = struct.unpack_from("<I", d, o)
        o += 4
        tag = d[o:o + tl]
        o += tl
        (pl,) = struct.unpack_from("<Q", d, o)
        o += 8
        p = d[o:o + pl]
        o += pl + 32
        if tag == b"AX:OBS:v1":
            out.append(json.loads(p))
    return out


def wait_for(pred, secs=15.0):
    end = time.time() + secs
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.05)
    return False


def run_case(name, model, admissible):
    """Returns (error or None, body bytes or a note)."""
    d = os.path.join(TMP, name)
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\n" % (sock, d, d, d)).encode("utf-8"))
        f.write(b"model_id=" + model + b"\n")
        f.write(("oracle_id=test-oracle\n"
                 "api_url=http://127.0.0.1:%d/v1/messages\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % PORT).encode("utf-8"))
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    before = len(BODIES)
    resp = b""
    gw = subprocess.Popen([GW, conf], env=env, stderr=subprocess.PIPE)
    try:
        if not wait_for(lambda: os.path.exists(sock) or gw.poll() is not None):
            return "gateway did not start", None
        if not os.path.exists(sock):
            msg = gw.stderr.read().decode("utf-8", "replace").strip()
            if not admissible:
                return None, "(refused at configuration load: %s)" % msg
            return "gateway refused the configuration: %s" % msg, None
        p = PROMPT.encode()
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock)
        s.sendall(b"max_tokens: 64\ntemperature_q16: 0\nprompt_len: %d\n\n"
                  % len(p) + p)
        while True:
            b = s.recv(65536)
            if not b:
                break
            resp += b
        s.close()
    finally:
        if gw.poll() is None:
            gw.send_signal(signal.SIGTERM)
        gw.wait(timeout=15)
        gw.stderr.close()
    recs = obs_records(os.path.join(d, "ev.bin"))
    if not admissible:
        if recs:
            return "a record was committed for an inadmissible model_id", None
        if len(BODIES) == before:
            return None, "(no upstream request sent; gateway replied %r)" % resp
    if len(BODIES) != before + 1:
        return "expected one upstream request, got %d" % (len(BODIES) - before), None
    body = BODIES[-1]
    err = check_body(body, model)
    if err is None and name == "plain" and body != PLAIN_BODY:
        err = "plain body bytes changed"
    if err is None and admissible:
        if b"status: ok\n" not in resp:
            err = "gateway response not ok: %r" % resp[:200]
        elif len(recs) != 1 or as_bytes(recs[0]["model_id"]) != model \
                or recs[0]["completion_state"] != "COMPLETE":
            err = "committed record wrong: %r" % recs
    return err, body


fails = 0
for name, model, admissible in CASES:
    err, body = run_case(name, model, admissible)
    print("%-16s model_id bytes: %r" % (name, model))
    if isinstance(body, bytes):
        print("    body bytes: %r" % body)
    elif body is not None:
        print("    %s" % body)
    if err is None:
        print("    PASS")
    else:
        fails += 1
        print("    FAIL: %s" % err)
srv.shutdown()
print("%d of %d cases passed" % (len(CASES) - fails, len(CASES)))
sys.exit(1 if fails else 0)
PYEOF

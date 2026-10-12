#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_response_header.sh - the socket response header is complete
# for every model_id the configuration accepts.
#
# The response to a served request is line-oriented:
#   status: / completion_state: / failure_type: / obs_hash: /
#   chain_head: / seq: / snapshot_id: / temperature_q16: / output_len:
# one "key: value" per LF-terminated line, then an empty line, then
# exactly output_len output bytes. snapshot_id echoes the configured
# model_id, which the configuration accepts up to 255 bytes.
#
# For model_ids of 1, 184, 200, 247, 248 and 255 bytes the gateway is
# started against a local stand-in API and sent one request. The
# response must hold exactly the nine header lines above in that order,
# an empty line, and then exactly output_len bytes; snapshot_id must be
# the configured model_id byte for byte; the response must contain no
# NUL byte. For model_id test-model the header text is pinned, so the
# change cannot alter what an ordinary configuration receives.
#
# Before the fix the header was built in a 512-byte buffer: from 248
# bytes of model_id the header was cut short with a NUL in place of the
# terminating empty line, and the full untruncated length was written
# from the 512-byte buffer (a stack over-read sent to the client).
#
# No network egress and no spend: the API endpoint is a local listener.
#
# usage: test_gw_response_header.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_response_header.sh <path-to-axioma-gateway>}"

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
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time

GW, TMP = sys.argv[1], sys.argv[2]
LENGTHS = [1, 184, 200, 247, 248, 255]
KEYS = [b"status", b"completion_state", b"failure_type", b"obs_hash",
        b"chain_head", b"seq", b"snapshot_id", b"temperature_q16",
        b"output_len"]
OUTPUT = b"ok"
BODY = b'{"content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn"}'
RESPONSE = (b"HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n"
            b"content-length: %d\r\nconnection: close\r\n\r\n" % len(BODY)) + BODY
# Pinned header text for model_id test-model, first request on a fresh
# ledger, with obs_hash and chain_head masked (they depend on time-free
# but configuration-bound inputs that this test does not fix).
PLAIN = (b"status: ok\ncompletion_state: COMPLETE\nfailure_type: NULL\n"
         b"obs_hash: <64 hex>\nchain_head: <64 hex>\nseq: 2\n"
         b"snapshot_id: test-model\ntemperature_q16: 0\noutput_len: 2\n\nok")

lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
lsock.bind(("127.0.0.1", 0))
lsock.listen(8)
PORT = lsock.getsockname()[1]


def serve_one(c):
    data = b""
    c.settimeout(10)
    try:
        while b"\r\n\r\n" not in data:
            b = c.recv(65536)
            if not b:
                break
            data += b
        c.sendall(RESPONSE)
    except OSError:
        pass
    finally:
        c.close()


def accept_loop():
    while True:
        try:
            c, _ = lsock.accept()
        except OSError:
            return
        threading.Thread(target=serve_one, args=(c,), daemon=True).start()


threading.Thread(target=accept_loop, daemon=True).start()


def wait_for(pred, secs=15.0):
    end = time.time() + secs
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.05)
    return False


def check(resp, model):
    if b"\x00" in resp:
        return "response contains a NUL byte at offset %d" % resp.index(b"\x00")
    sep = resp.find(b"\n\n")
    if sep < 0:
        return "no empty line terminating the header"
    lines = resp[:sep].split(b"\n")
    keys = [l.split(b": ", 1)[0] for l in lines]
    if keys != KEYS:
        return "header keys %r" % keys
    vals = dict(l.split(b": ", 1) for l in lines)
    if vals[b"snapshot_id"] != model:
        return "snapshot_id is %d bytes, configured %d" % (
            len(vals[b"snapshot_id"]), len(model))
    if vals[b"status"] != b"ok" or vals[b"output_len"] != b"%d" % len(OUTPUT):
        return "status/output_len %r %r" % (vals[b"status"], vals[b"output_len"])
    if resp[sep + 2:] != OUTPUT:
        return "body after header is %r" % resp[sep + 2:][:40]
    return None


def run_case(model):
    d = os.path.join(TMP, "len%d" % len(model))
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\n"
                 "api_url=http://127.0.0.1:%d/v1/messages\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % (sock, d, d, d, PORT)).encode())
        f.write(b"model_id=" + model + b"\n")
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    resp = b""
    gw = subprocess.Popen([GW, conf], env=env, stderr=subprocess.PIPE)
    try:
        if not wait_for(lambda: os.path.exists(sock) or gw.poll() is not None):
            return "gateway did not start", resp
        if not os.path.exists(sock):
            return "gateway refused the configuration: %s" % (
                gw.stderr.read().decode("utf-8", "replace").strip()), resp
        p = b"hello"
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock)
        s.sendall(b"max_tokens: 64\ntemperature_q16: 0\nprompt_len: %d\n\n"
                  % len(p) + p)
        while True:
            try:
                b = s.recv(65536)
            except OSError:
                break
            if not b:
                break
            resp += b
        s.close()
    finally:
        if gw.poll() is None:
            gw.send_signal(signal.SIGTERM)
        gw.wait(timeout=15)
        gw.stderr.close()
    return check(resp, model), resp


fails = 0
cases = [b"test-model"] + [b"m" * n for n in LENGTHS]
for model in cases:
    err, resp = run_case(model)
    if err is None and model == b"test-model":
        masked = re.sub(rb"(obs_hash|chain_head): [0-9a-f]{64}",
                        rb"\1: <64 hex>", resp)
        if masked != PLAIN:
            err = "plain response changed: %r" % masked
    print("model_id %4d bytes: response %d bytes" % (len(model), len(resp)))
    if err is None:
        print("    PASS")
    else:
        fails += 1
        print("    FAIL: %s" % err)
        print("    response tail: %r" % resp[-60:])
lsock.close()
print("%d of %d cases passed" % (len(cases) - fails, len(cases)))
sys.exit(1 if fails else 0)
PYEOF

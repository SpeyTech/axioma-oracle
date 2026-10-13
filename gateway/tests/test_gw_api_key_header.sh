#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_api_key_header.sh - the API key from the environment cannot add
# or split HTTP request headers, and is never printed.
#
# The gateway reads its API key from AX_ORACLE_ANTHROPIC_KEY and copies it
# into the "x-api-key:" request header line. A CR inside the key reaches
# the wire and starts a new header line. RFC 9110 section 5.5: a field
# value never contains CR, LF or NUL.
#
# For each key below the gateway is started against a local listener that
# records the raw request bytes (no HTTP parser in between).
#
#   valid     one upstream request; its header block holds the line
#             "x-api-key: <key>" once and no line holds a bare CR or LF.
#   cr, lf, ctl (0x01), tab, del (0x7F)
#             the gateway must refuse to start: exit non-zero, no socket,
#             zero upstream connections, no ledger or WAL file.
#
# Secret handling: every key carries a marker string. The test fails if
# the marker appears in the gateway's stderr or log file, and it never
# prints the key or the x-api-key line itself.
#
# No network egress and no spend: the API endpoint is a local listener.
# Requires python3, already a tooling dependency.
#
# usage: test_gw_api_key_header.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_api_key_header.sh <path-to-axioma-gateway>}"

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
import signal
import socket
import subprocess
import sys
import threading
import time

GW, TMP = sys.argv[1], sys.argv[2]
MARK = "SECRETMARKER7f3a"
# (name, key, must start). Keys are str: os.environ holds no NUL, and a
# NUL cannot be passed through an environment variable at all.
CASES = [
    ("valid", "test-" + MARK + "-ok", True),
    ("cr", "test-" + MARK + "\rX-Injected: 1", False),
    ("lf", "test-" + MARK + "\nX-Injected: 1", False),
    ("ctl", "test-" + MARK + "\x01", False),
    ("tab", "test-" + MARK + "\tx", False),
    ("del", "test-" + MARK + "\x7f", False),
]
BODY = b'{"content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn"}'
RESPONSE = (b"HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n"
            b"content-length: %d\r\nconnection: close\r\n\r\n" % len(BODY)) + BODY
REQUESTS = []
LOCK = threading.Lock()

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
        with LOCK:
            REQUESTS.append(data.partition(b"\r\n\r\n")[0])
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


def describe(head):
    """Header names only, for printing: never the key. A line that holds a
    bare CR or LF (a split header) is marked, with the names that follow."""
    out = []
    for l in head.split(b"\r\n")[1:]:
        parts = l.replace(b"\r", b"\n").split(b"\n")
        name = parts[0].split(b":", 1)[0].decode("ascii", "replace")
        if len(parts) > 1:
            extra = [p.split(b":", 1)[0].decode("ascii", "replace") for p in parts[1:]]
            name += " +BARE-CR/LF-> " + ",".join(extra)
        out.append(name)
    return out


def run_case(name, key, must_start):
    d = os.path.join(TMP, name)
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\nmodel_id=test-model\n"
                 "api_url=http://127.0.0.1:%d/v1/messages\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % (sock, d, d, d, PORT)).encode())
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY=key)
    with LOCK:
        before = len(REQUESTS)
    served = False
    gw = subprocess.Popen([GW, conf], env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    try:
        if not wait_for(lambda: os.path.exists(sock) or gw.poll() is not None):
            return "gateway neither served nor exited", ""
        if os.path.exists(sock):
            served = True
            p = b"hello"
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock)
            s.sendall(b"max_tokens: 64\ntemperature_q16: 0\nprompt_len: %d\n\n"
                      % len(p) + p)
            while s.recv(65536):
                pass
            s.close()
    finally:
        if gw.poll() is None:
            gw.send_signal(signal.SIGTERM)
        gw.wait(timeout=15)
        out = gw.stdout.read() + gw.stderr.read()
        gw.stdout.close()
        gw.stderr.close()
    time.sleep(0.2)
    with LOCK:
        got = REQUESTS[before:]
    logb = b""
    if os.path.exists(os.path.join(d, "gw.log")):
        logb = open(os.path.join(d, "gw.log"), "rb").read()
    if MARK.encode() in out:
        return "key material printed by the gateway", ""
    if MARK.encode() in logb:
        return "key material written to the gateway log", ""
    written = sorted(f for f in ("ev.bin", "wal") if os.path.exists(os.path.join(d, f)))
    msg = out.decode("utf-8", "replace").strip().splitlines()
    note = "exit %s; upstream connections %d; files %s%s" % (
        gw.returncode, len(got), written or "none",
        ("; stderr: " + msg[-1]) if msg else "")
    if must_start:
        if not served or len(got) != 1:
            return "positive control: served=%s, %d requests" % (served, len(got)), note
        lines = got[0].split(b"\r\n")
        if any(b"\r" in l or b"\n" in l for l in lines):
            return "a header line holds a bare CR or LF", note
        keyl = [l for l in lines if l.lower().startswith(b"x-api-key:")]
        if keyl != [b"x-api-key: " + key.encode()]:
            return "x-api-key line not exactly the key (%d lines)" % len(keyl), note
        return None, note + "; header names %r" % describe(got[0])
    if got:
        return ("%d upstream request(s) sent; header names %r"
                % (len(got), describe(got[0]))), note
    if served or gw.returncode == 0:
        return "gateway served or exited 0", note
    if written:
        return "files written: %s" % written, note
    return None, note


fails = 0
for name, key, must_start in CASES:
    err, note = run_case(name, key, must_start)
    print("%-6s key with %s" % (name, "no control character" if must_start
                                 else "a control character"))
    if note:
        print("    %s" % note)
    if err is None:
        print("    PASS")
    else:
        fails += 1
        print("    FAIL: %s" % err)
lsock.close()
print("%d of %d cases passed" % (len(CASES) - fails, len(CASES)))
sys.exit(1 if fails else 0)
PYEOF

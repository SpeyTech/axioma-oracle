#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_api_url.sh - an api_url that the HTTP client cannot parse is
# refused at startup, not at every request.
#
# The gateway hands the configured api_url to libcurl for each request.
# libcurl refuses a URL holding a control character, DEL, a space or no
# host, so each request failed and was committed as a TRANSPORT_ERROR
# observation: the configuration loaded and served, and every request
# produced a failure record. The URL is now parsed at startup with
# libcurl's own URL parser, with the flags that make it accept exactly
# what a transfer accepts, so no new URL grammar is introduced.
#
# Cases, each against a local listener that counts connections:
#   valid      http://127.0.0.1:PORT/v1/messages: one request (control)
#   schemeless 127.0.0.1:PORT/v1/messages: libcurl guesses http and the
#              transfer works today; pinned unchanged (one request)
#   invalid    CR injection, space, 0x01, TAB, DEL, no host, not a URL:
#              the gateway must refuse to start: exit non-zero, no socket,
#              zero upstream connections, no ledger or WAL file.
#
# No network egress and no spend: the API endpoint is a local listener.
#
# usage: test_gw_api_url.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_api_url.sh <path-to-axioma-gateway>}"

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
BODY = b'{"content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn"}'
RESPONSE = (b"HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n"
            b"content-length: %d\r\nconnection: close\r\n\r\n" % len(BODY)) + BODY
CONNECTIONS = [0]
LOCK = threading.Lock()

lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
lsock.bind(("127.0.0.1", 0))
lsock.listen(8)
PORT = lsock.getsockname()[1]
BASE = b"http://127.0.0.1:%d" % PORT

# (name, api_url bytes, must start)
CASES = [
    ("valid", BASE + b"/v1/messages", True),
    ("schemeless", b"127.0.0.1:%d/v1/messages" % PORT, True),
    ("cr_injection", BASE + b"/v1/mess\rX-Injected: 1", False),
    ("space", BASE + b"/v1/a b", False),
    ("ctl", BASE + b"/v1/\x01", False),
    ("tab", BASE + b"/v1/\tx", False),
    ("del", BASE + b"/v1/\x7f", False),
    ("no_host", b"http://", False),
    ("not_a_url", b"not a url", False),
]


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
        with LOCK:
            CONNECTIONS[0] += 1
        threading.Thread(target=serve_one, args=(c,), daemon=True).start()


threading.Thread(target=accept_loop, daemon=True).start()


def wait_for(pred, secs=15.0):
    end = time.time() + secs
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.05)
    return False


def run_case(name, url, must_start):
    d = os.path.join(TMP, name)
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\nmodel_id=test-model\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % (sock, d, d, d)).encode())
        f.write(b"api_url=" + url + b"\n")
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    with LOCK:
        before = CONNECTIONS[0]
    served = False
    resp = b""
    gw = subprocess.Popen([GW, conf], env=env, stderr=subprocess.PIPE)
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
        msg = gw.stderr.read().decode("utf-8", "replace").strip().splitlines()
        gw.stderr.close()
    time.sleep(0.2)
    with LOCK:
        calls = CONNECTIONS[0] - before
    written = sorted(f for f in ("ev.bin", "wal") if os.path.exists(os.path.join(d, f)))
    status = resp.split(b"\n", 3)[:3] if resp else []
    note = "exit %s; upstream connections %d; files %s; response %r%s" % (
        gw.returncode, calls, written or "none", status,
        ("; stderr: " + msg[-1]) if msg else "")
    if must_start:
        if not served or calls != 1 or not resp.startswith(b"status: ok\n"):
            return "served=%s, %d upstream connections" % (served, calls), note
        return None, note
    if calls:
        return "%d upstream connection(s)" % calls, note
    if served or gw.returncode == 0:
        return "gateway served or exited 0 with an unusable api_url", note
    if written:
        return "files written: %s" % written, note
    return None, note


fails = 0
for name, url, must_start in CASES:
    err, note = run_case(name, url, must_start)
    print("%-12s %r" % (name, url))
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

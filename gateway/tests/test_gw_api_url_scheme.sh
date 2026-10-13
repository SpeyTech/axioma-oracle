#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_api_url_scheme.sh - the gateway transmits credentials and
# prompts over HTTP or HTTPS only.
#
# api_url is parsed at startup with libcurl's URL parser (927906e), which
# accepts any scheme name and guesses a scheme for a schemeless URL. A
# file:, ftp:, dict:, gopher: or other URL therefore loaded, and libcurl
# then performed that protocol for every request: a file: URL read a local
# file into the gateway's error path. The scheme the parser reports, after
# any guess, must now be http or https, or the gateway refuses to start.
#
# Cases, each against a local TCP listener that counts connections and
# answers only HTTP POSTs:
#   http, HTTP (upper case), schemeless (guessed http): one request each,
#   served (unchanged).
#   file (a local file holding a marker), ftp, dict, gopher, telnet, and a
#   schemeless host starting "ftp." (libcurl guesses ftp): the gateway must
#   refuse to start: exit non-zero, no socket, zero connections, no ledger
#   or WAL, and the marker must appear in no output or log.
#
# No network egress and no spend: every endpoint is local.
#
# usage: test_gw_api_url_scheme.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_api_url_scheme.sh <path-to-axioma-gateway>}"

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
MARK = b"LOCALFILEMARKER-9a7d"
SECRET = os.path.join(TMP, "local-file.txt")
with open(SECRET, "wb") as f:
    f.write(MARK + b"\n")

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
HP = b"127.0.0.1:%d" % PORT

# (name, api_url bytes, must start)
CASES = [
    ("http", b"http://" + HP + b"/v1/messages", True),
    ("http_upper", b"HTTP://" + HP + b"/v1/messages", True),
    ("schemeless", HP + b"/v1/messages", True),
    ("file", b"file://" + SECRET.encode(), False),
    ("ftp", b"ftp://" + HP + b"/x", False),
    ("dict", b"dict://" + HP + b"/d:x", False),
    ("gopher", b"gopher://" + HP + b"/x", False),
    ("telnet", b"telnet://" + HP, False),
    ("guessed_ftp", b"ftp.example.invalid/x", False),
]


def serve_one(c):
    c.settimeout(2)
    try:
        data = b""
        while b"\r\n\r\n" not in data:
            b = c.recv(65536)
            if not b:
                break
            data += b
            if not data.startswith(b"POST"[:len(data)]):
                return          # not HTTP: count it, answer nothing
        if data.startswith(b"POST"):
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
                 "connect_timeout_s=3\nresponse_timeout_s=5\n"
                 % (sock, d, d, d)).encode())
        f.write(b"api_url=" + url + b"\n")
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    with LOCK:
        before = CONNECTIONS[0]
    served = False
    resp = b""
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
            while True:
                b = s.recv(65536)
                if not b:
                    break
                resp += b
            s.close()
    finally:
        if gw.poll() is None:
            gw.send_signal(signal.SIGTERM)
        gw.wait(timeout=20)
        out = gw.stdout.read() + gw.stderr.read()
        gw.stdout.close()
        gw.stderr.close()
    time.sleep(0.2)
    with LOCK:
        calls = CONNECTIONS[0] - before
    logb = b""
    if os.path.exists(os.path.join(d, "gw.log")):
        logb = open(os.path.join(d, "gw.log"), "rb").read()
    ledger = b""
    if os.path.exists(os.path.join(d, "ev.bin")):
        ledger = open(os.path.join(d, "ev.bin"), "rb").read()
    written = sorted(f for f in ("ev.bin", "wal") if os.path.exists(os.path.join(d, f)))
    msg = out.decode("utf-8", "replace").strip().splitlines()
    status = resp.split(b"\n", 3)[:3] if resp else []
    note = "exit %s; upstream connections %d; files %s; response %r%s" % (
        gw.returncode, calls, written or "none", status,
        ("; stderr: " + msg[-1]) if msg else "")
    leaks = [w for w, b in (("stdout/stderr", out), ("log", logb),
                            ("ledger", ledger), ("response", resp)) if MARK in b]
    if leaks:
        return "local file content reached: %s" % ", ".join(leaks), note
    if must_start:
        if not served or calls != 1 or not resp.startswith(b"status: ok\n"):
            return "served=%s, %d upstream connections" % (served, calls), note
        return None, note
    if calls:
        return "%d upstream connection(s) over a non-HTTP scheme" % calls, note
    if served or gw.returncode == 0:
        return "gateway started with a non-HTTP(S) api_url", note
    if written:
        return "files written: %s" % written, note
    return None, note


fails = 0
for name, url, must_start in CASES:
    err, note = run_case(name, url, must_start)
    shown = url.replace(SECRET.encode(), b"<tmp>/local-file.txt")
    print("%-12s %r" % (name, shown))
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

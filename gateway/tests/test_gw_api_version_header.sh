#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_api_version_header.sh - a configured anthropic_version cannot
# add or split HTTP request headers.
#
# The gateway copies anthropic_version into the "anthropic-version:"
# request header line. For each configuration below the gateway is
# started against a local listener that records the raw request bytes
# exactly as received (no HTTP library parses them first, so a bare CR
# is seen as sent).
#
#   plain     anthropic_version=2023-06-01: exactly one upstream request,
#             whose header block holds the line
#             "anthropic-version: 2023-06-01" once, every line ends CRLF
#             and no line holds a bare CR or LF.
#   default   key absent: same, with the default 2023-06-01.
#   inject_cr anthropic_version=2023-06-01<CR>X-Injected: 1
#   ctl       anthropic_version=2023-06-01<0x01>
#   del       anthropic_version=2023-06-01<0x7F>
#             The gateway must refuse the configuration at load: it exits
#             non-zero, never creates its socket, and the listener sees
#             zero connections.
#
# No network egress and no spend: the API endpoint is a local listener.
# Requires python3, already a tooling dependency.
#
# usage: test_gw_api_version_header.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_api_version_header.sh <path-to-axioma-gateway>}"

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
# (name, anthropic_version line bytes or None, must load)
CASES = [
    ("plain", b"anthropic_version=2023-06-01", True),
    ("default", None, True),
    ("inject_cr", b"anthropic_version=2023-06-01\rX-Injected: 1", False),
    ("ctl", b"anthropic_version=2023-06-01\x01", False),
    ("del", b"anthropic_version=2023-06-01\x7f", False),
]
RESPONSE_BODY = b'{"content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn"}'
RESPONSE = (b"HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n"
            b"content-length: %d\r\nconnection: close\r\n\r\n"
            % len(RESPONSE_BODY)) + RESPONSE_BODY
REQUESTS = []          # raw header blocks, in arrival order
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
        head, _, rest = data.partition(b"\r\n\r\n")
        n = 0
        for line in head.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                n = int(line.split(b":", 1)[1].strip())
        while len(rest) < n:
            b = c.recv(65536)
            if not b:
                break
            rest += b
        with LOCK:
            REQUESTS.append(head)
        c.sendall(RESPONSE)
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


def redact(head):
    """The header block with the x-api-key value masked, for printing."""
    return b"\r\n".join(
        b"x-api-key: <redacted>" if l.lower().startswith(b"x-api-key:") else l
        for l in head.split(b"\r\n"))


def check_head(head):
    lines = head.split(b"\r\n")
    for line in lines:
        if b"\r" in line or b"\n" in line:
            return "header line holds a bare CR or LF: %r" % line
    ver = [l for l in lines if l.lower().startswith(b"anthropic-version:")]
    if ver != [b"anthropic-version: 2023-06-01"]:
        return "anthropic-version lines are %r" % ver
    if any(l.lower().startswith(b"x-injected") for l in lines):
        return "injected header present"
    return None


def run_case(name, verline, must_load):
    d = os.path.join(TMP, name)
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\nmodel_id=test-model\n"
                 "api_url=http://127.0.0.1:%d/v1/messages\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % (sock, d, d, d, PORT)).encode())
        if verline is not None:
            f.write(verline + b"\n")
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    with LOCK:
        before = len(REQUESTS)
    note = ""
    gw = subprocess.Popen([GW, conf], env=env, stderr=subprocess.PIPE)
    try:
        if not wait_for(lambda: os.path.exists(sock) or gw.poll() is not None):
            return "gateway did not start or exit", note
        if not os.path.exists(sock):
            msg = gw.stderr.read().decode("utf-8", "replace").strip()
            if must_load:
                return "gateway refused the configuration: %s" % msg, note
            if gw.returncode == 0:
                return "gateway exited 0 without serving", note
            note = "refused at load (exit %d): %s" % (gw.returncode, msg)
        else:
            if not must_load:
                note = "configuration loaded; "
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
        gw.stderr.close()
    time.sleep(0.2)
    with LOCK:
        got = REQUESTS[before:]
    if not must_load:
        if got:
            return ("%d upstream request(s) sent; first header block: %r"
                    % (len(got), redact(got[0]))), note
        return None, note + "zero upstream connections"
    if len(got) != 1:
        return "expected one upstream request, got %d" % len(got), note
    err = check_head(got[0])
    return err, note + "header block %r" % redact(got[0])


fails = 0
for name, verline, must_load in CASES:
    err, note = run_case(name, verline, must_load)
    print("%-10s %r" % (name, verline))
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

#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_config_zero_upstream.sh - a configuration the gateway refuses at
# load makes zero upstream calls (PCR-004 residual (c); owner direction
# 2026-10-12, Round 7 item 7).
#
# Each invalid configuration below is a complete, otherwise valid file
# whose api_url points at a local TCP listener that counts every
# connection. The gateway is started and, if it serves, sent one request.
# For every invalid configuration the gateway must exit non-zero without
# creating its socket, the listener must see zero connections, and no
# ledger, WAL or AX:OBS:v1 record may be written.
#
# A positive control (a valid configuration) must make exactly one
# upstream connection, so a listener that cannot be reached cannot make
# the invalid cases pass.
#
# On 44deb6d (before residual (c)) the identifier cases load, the request
# reaches the listener, and admission then refuses the record: one paid
# upstream call per request for a configuration that can never commit.
#
# No network egress and no spend: the API endpoint is a local listener.
#
# usage: test_gw_config_zero_upstream.sh <path-to-axioma-gateway-binary>

set -eu

GW_BIN="${1:?usage: test_gw_config_zero_upstream.sh <path-to-axioma-gateway>}"

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
# (name, extra config bytes, valid)
CASES = [
    ("valid_control", b"model_id=test-model\n", True),
    ("model_id_ctl", b"model_id=a\x01b\n", False),
    ("model_id_tab", b"model_id=a\tb\n", False),
    ("model_id_del", b"model_id=a\x7fb\n", False),
    ("model_id_not_utf8", b"model_id=a\xffb\n", False),
    ("model_id_overlong", b"model_id=a\xc0\x80b\n", False),
    ("model_id_surrogate", b"model_id=a\xed\xa0\x80b\n", False),
    ("model_id_256_bytes", b"model_id=" + b"m" * 256 + b"\n", False),
    ("oracle_id_ctl", b"model_id=test-model\noracle_id=o\x1fp\n", False),
    ("oracle_id_not_utf8", b"model_id=test-model\noracle_id=o\xc3p\n", False),
    ("nul_in_file", b"model_id=m\x00x\n", False),
]
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


def run_case(name, extra, valid):
    d = os.path.join(TMP, name)
    os.mkdir(d)
    sock = os.path.join(d, "gw.sock")
    conf = os.path.join(d, "gw.conf")
    with open(conf, "wb") as f:
        f.write(("socket_path=%s\nledger_path=%s/ev.bin\nwal_path=%s/wal\n"
                 "log_path=%s/gw.log\n"
                 "api_url=http://127.0.0.1:%d/v1/messages\nmax_attempts=1\n"
                 "response_timeout_s=10\n" % (sock, d, d, d, PORT)).encode())
        f.write(extra)
    env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
    with LOCK:
        before = CONNECTIONS[0]
    served = False
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
            while s.recv(65536):
                pass
            s.close()
    finally:
        if gw.poll() is None:
            gw.send_signal(signal.SIGTERM)
        gw.wait(timeout=15)
        msg = gw.stderr.read().decode("utf-8", "replace").strip()
        gw.stderr.close()
    time.sleep(0.2)
    with LOCK:
        calls = CONNECTIONS[0] - before
    written = sorted(f for f in ("ev.bin", "wal") if os.path.exists(os.path.join(d, f)))
    note = "exit %s; upstream connections %d; files written %s" % (
        gw.returncode, calls, written or "none")
    if valid:
        if not served or calls != 1:
            return "positive control: served=%s, %d upstream connections" % (
                served, calls), note
        return None, note
    note += "; stderr: %s" % msg.splitlines()[-1] if msg else ""
    if calls != 0:
        return "%d upstream connection(s) for an invalid configuration" % calls, note
    if served or gw.returncode == 0:
        return "gateway served or exited 0 with an invalid configuration", note
    if written:
        return "files written for an invalid configuration: %s" % written, note
    return None, note


fails = 0
for name, extra, valid in CASES:
    err, note = run_case(name, extra, valid)
    print("%-20s %r" % (name, extra if len(extra) < 60 else extra[:40] + b"..."))
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

#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_signals.sh - signal delivery against the running service.
#
# E1 was found by deployment, not by the suite: nothing exercised
# signal delivery against a blocking accept. This closes that gap.
#
#   Check 1 (E2): with NOTIFY_SOCKET set, exactly one READY=1 datagram
#     arrives, and only after the listening socket exists.
#   Check 2 (E1): SIGTERM during blocking accept exits 0, logs
#     shutdown, unlinks the socket.
#   Check 3: SIGKILL mid-request leaves a replayable WAL intent; the
#     next start recovers it as a failure observation (wal_recovered
#     logged), clears the WAL, and still stops cleanly.
#
# No network egress and no spend: the oracle endpoint is a local
# listener that accepts and never responds, so the request parks
# in-flight and the kill lands mid-transfer. Requires python3, already
# a tooling dependency (gw-client.py, ax-rtm-verify.py).
#
# usage: test_gw_signals.sh <path-to-axioma-gateway-binary>

# SC2317: cleanup() and grep_intent() are reached indirectly (trap and
# wait_for "$@"); shellcheck's reachability analysis cannot see that.
# shellcheck disable=SC2317

set -eu

GW_BIN="${1:?usage: test_gw_signals.sh <path-to-axioma-gateway>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 required" >&2
    exit 77
fi

TMP="$(mktemp -d)"
GW_PID=""
HANG_PID=""
CLIENT_PID=""
NOTIFY_PID=""

cleanup() {
    if [ -n "${GW_PID}" ];     then kill -KILL "${GW_PID}"     2>/dev/null || true; fi
    if [ -n "${HANG_PID}" ];   then kill -KILL "${HANG_PID}"   2>/dev/null || true; fi
    if [ -n "${CLIENT_PID}" ]; then kill -KILL "${CLIENT_PID}" 2>/dev/null || true; fi
    if [ -n "${NOTIFY_PID}" ]; then kill -KILL "${NOTIFY_PID}" 2>/dev/null || true; fi
    rm -rf "${TMP}"
}
trap cleanup EXIT INT TERM

fail() { echo "FAIL: $1" >&2; exit 1; }

# Retry a predicate command for up to 15 seconds.
wait_for() {
    i=0
    while [ "${i}" -lt 150 ]; do
        if "$@"; then return 0; fi
        i=$((i + 1))
        sleep 0.1
    done
    return 1
}

SOCK="${TMP}/gw.sock"
LOG="${TMP}/gw.log"
WAL="${TMP}/intent.wal"
CFG="${TMP}/gateway.conf"
PORTF="${TMP}/port"

# Local endpoint that accepts one connection and never responds: the
# oracle call hangs in-flight for as long as the test needs.
python3 - "${PORTF}" <<'PYEOF' &
import socket, sys, time
s = socket.socket()
s.bind(("127.0.0.1", 0))
s.listen(1)
with open(sys.argv[1], "w") as f:
    f.write(str(s.getsockname()[1]))
# Keep the accepted connection referenced: an unbound accept() result
# is garbage-collected and closed, and curl then fails fast with
# "Empty reply from server" instead of parking in-flight.
conn, _ = s.accept()
time.sleep(600)
conn.close()
PYEOF
HANG_PID=$!

wait_for test -s "${PORTF}" || fail "hang endpoint never reported its port"
PORT="$(cat "${PORTF}")"

cat > "${CFG}" <<CFGEOF
socket_path=${SOCK}
ledger_path=${TMP}/evidence.bin
wal_path=${WAL}
log_path=${LOG}
model_id=test-model
api_url=http://127.0.0.1:${PORT}/v1/messages
max_attempts=1
connect_timeout_s=5
response_timeout_s=120
call_budget=5
CFGEOF

# Never sent anywhere: the endpoint is local and mute.
AX_ORACLE_ANTHROPIC_KEY="test-key-not-real"
export AX_ORACLE_ANTHROPIC_KEY

# ---- Check 1 (E2): READY=1 after bind+listen ----------------------------
# Plays systemd's side: a datagram socket in NOTIFY_SOCKET must receive
# exactly READY=1, and the gateway's listening socket must already
# exist when it does (readiness is bind+listen, not fork).

NREADY="${TMP}/notify.ok"
python3 - "${TMP}/notify.sock" "${SOCK}" "${NREADY}" <<'PYEOF' &
import os, socket, sys
ns = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
ns.bind(sys.argv[1])
ns.settimeout(15)
data = ns.recv(64)
ok = (data == b"READY=1") and os.path.exists(sys.argv[2])
with open(sys.argv[3], "w") as f:
    f.write("ok" if ok else "bad: %r sock_exists=%s"
            % (data, os.path.exists(sys.argv[2])))
PYEOF
NOTIFY_PID=$!
wait_for test -S "${TMP}/notify.sock" || fail "notify receiver never bound"

NOTIFY_SOCKET="${TMP}/notify.sock" "${GW_BIN}" "${CFG}" &
GW_PID=$!
rc=0
wait "${NOTIFY_PID}" || rc=$?
NOTIFY_PID=""
[ "${rc}" -eq 0 ] || fail "notify receiver errored"
[ "$(cat "${NREADY}")" = "ok" ] || fail "READY=1 wrong or early: $(cat "${NREADY}")"
grep -q '"event":"fault"' "${LOG}" && fail "notify send logged a fault"

kill -TERM "${GW_PID}"
rc=0
wait "${GW_PID}" || rc=$?
GW_PID=""
[ "${rc}" -eq 0 ] || fail "clean stop after notify check: exit status ${rc}"

# ---- Check 2 (E1): SIGTERM during blocking accept ----------------------

"${GW_BIN}" "${CFG}" &
GW_PID=$!
wait_for test -S "${SOCK}" || fail "socket never appeared (check 2)"

kill -TERM "${GW_PID}"
rc=0
wait "${GW_PID}" || rc=$?
GW_PID=""
[ "${rc}" -eq 0 ] || fail "SIGTERM in accept: exit status ${rc}, want 0"
grep -q '"event":"shutdown"' "${LOG}" || fail "no shutdown log line"
[ ! -S "${SOCK}" ] || fail "socket not unlinked on shutdown"

# ---- Check 3: SIGKILL mid-request leaves a replayable WAL intent -------

"${GW_BIN}" "${CFG}" &
GW_PID=$!
wait_for test -S "${SOCK}" || fail "socket never appeared (check 3)"

python3 - "${SOCK}" <<'PYEOF' &
import socket, sys
p = b"kill me mid-flight"
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.sendall(b"max_tokens: 16\nprompt_len: %d\n\n" % len(p) + p)
s.recv(65536)
PYEOF
CLIENT_PID=$!

grep_intent() { grep -q '"event":"intent"' "${LOG}"; }
wait_for grep_intent || fail "intent never logged; request did not reach the WAL"

kill -KILL "${GW_PID}"
wait "${GW_PID}" 2>/dev/null || true
GW_PID=""
[ -s "${WAL}" ] || fail "no WAL intent on disk after SIGKILL mid-request"

# SIGKILL leaves the old socket file; remove it so the readiness wait
# below is for the restarted instance's bind, not the stale inode.
rm -f "${SOCK}"

"${GW_BIN}" "${CFG}" &
GW_PID=$!
wait_for test -S "${SOCK}" || fail "socket never appeared after recovery start"
grep -q '"event":"wal_recovered"' "${LOG}" || fail "WAL intent not recovered"
[ ! -e "${WAL}" ] || fail "WAL not cleared after recovery"

kill -TERM "${GW_PID}"
rc=0
wait "${GW_PID}" || rc=$?
GW_PID=""
[ "${rc}" -eq 0 ] || fail "clean stop after recovery: exit status ${rc}"

echo "OK: SIGTERM-in-accept clean, SIGKILL-mid-request WAL replayed"
exit 0

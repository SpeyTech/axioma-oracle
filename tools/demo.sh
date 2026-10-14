#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# demo.sh - an isolated, end-to-end demonstration of the supported gateway
# (OPERATOR.md section 2). Everything runs as the calling user in a new
# temporary directory: its own stand-in upstream on 127.0.0.1, socket,
# configuration, ledger, intent and log. No network egress, no spend, no
# systemd. It refuses to run as root and never reads or writes the
# production paths (/var/lib/axioma-gateway, /etc/axioma-gateway*).
#
# usage: demo.sh [path-to-axioma-gateway]
#   default gateway: ./build-verify/axioma-gateway, then axioma-gateway on PATH
#   AXIOMA_DEMO_KEEP=1 keeps the working directory for inspection

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)

die() { echo "demo: $1" >&2; exit 1; }
step() { printf '\n== %s\n' "$1"; }

[ "$(id -u)" -ne 0 ] || die "run as an ordinary user, not root"

tool() { # resolve a tool next to this script, then on PATH
    if [ -x "$HERE/$1" ]; then echo "$HERE/$1"
    elif command -v "$1" >/dev/null 2>&1; then command -v "$1"
    else die "cannot find $1"; fi
}

GW=${1:-}
if [ -z "$GW" ]; then
    if [ -x "$HERE/../build-verify/axioma-gateway" ]; then GW="$HERE/../build-verify/axioma-gateway"
    else GW=$(tool axioma-gateway); fi
fi
[ -x "$GW" ] || die "gateway binary not executable: $GW"
STANDIN=$(tool axioma-standin)
CLIENT=$(tool axioma-client)
VERIFY=$(tool axioma-verify-ledger)

W=$(mktemp -d "${TMPDIR:-/tmp}/axioma-demo.XXXXXX")
case "$W" in /var/lib/axioma-gateway*|/etc/*) die "refusing working directory $W";; esac
GW_PID=""
SI_PID=""
# Reached through the EXIT trap only.
# shellcheck disable=SC2317,SC2329
cleanup() {
    if [ -n "$GW_PID" ]; then kill -KILL "$GW_PID" 2>/dev/null || true; fi
    if [ -n "$SI_PID" ]; then kill -KILL "$SI_PID" 2>/dev/null || true; fi
    if [ "${AXIOMA_DEMO_KEEP:-0}" = 1 ]; then echo "demo: kept $W"; else rm -rf "$W"; fi
}
trap cleanup EXIT INT TERM

LEDGER="$W/evidence.bin"
WAL="$W/intent.wal"
SOCK="$W/gw.sock"
export AX_ORACLE_ANTHROPIC_KEY="demo-key-not-a-credential"

wait_sock() {
    i=0
    while [ ! -S "$SOCK" ]; do
        i=$((i + 1)); [ "$i" -lt 100 ] || return 1
        if [ -n "$GW_PID" ] && ! kill -0 "$GW_PID" 2>/dev/null; then return 1; fi
        sleep 0.1
    done
}
start_gw() { # start_gw: background gateway, wait for the socket
    rm -f "$SOCK"
    "$GW" "$W/gw.conf" 2>>"$W/gateway.stderr" &
    GW_PID=$!
    wait_sock || die "gateway did not start (see $W/gw.log)"
}
stop_gw() {
    if [ -n "$GW_PID" ]; then kill -TERM "$GW_PID"; wait "$GW_PID" || true; GW_PID=""; fi
}
ask() { # ask <prompt>: one request, one summary line
    r=$(python3 -I "$CLIENT" "$SOCK" "$1" 2>&1 || true)
    printf '  %-26s -> %s\n' "\"$1\"" "$(printf '%s\n' "$r" | awk -F': ' '
        /^status: /{s=$2} /^completion_state: /{c=$2} /^failure_type: /{f=$2}
        /^seq: /{q=$2} /^reason: /{why=$2}
        END{ if (why != "") printf "error (reason: %s), no record written", why;
             else printf "%s %s/%s seq=%s", s, c, f, q }')"
}

step "1. Start a stand-in upstream and a gateway in $W"
python3 -I "$STANDIN" --port-file "$W/port" --slow-seconds 4 2>"$W/standin.log" &
SI_PID=$!
i=0; while [ ! -s "$W/port" ]; do i=$((i + 1)); [ "$i" -lt 50 ] || die "stand-in did not start"; sleep 0.1; done
cat >"$W/gw.conf" <<EOF
socket_path=$SOCK
ledger_path=$LEDGER
wal_path=$WAL
log_path=$W/gw.log
model_id=demo-model
oracle_id=demo-standin
api_url=http://127.0.0.1:$(cat "$W/port")/v1/messages
max_attempts=1
response_timeout_s=2
call_budget=50
EOF
start_gw
echo "  gateway pid $GW_PID; socket $SOCK; upstream is the stand-in only"

step "2. A refused request: no record, nothing sent upstream"
ask "$(printf 'e\314\201 is not NFC')"

step "3. Recorded outcomes: one record per accepted request"
ask "ok first request"
R=$(python3 -I "$CLIENT" "$SOCK" "ok checkpoint request")
SEQ=$(printf '%s\n' "$R" | awk -F': ' '$1=="seq"{print $2; exit}')
HEAD=$(printf '%s\n' "$R" | awk -F': ' '$1=="chain_head"{print $2; exit}')
if [ -z "$SEQ" ] || [ -z "$HEAD" ]; then die "no checkpoint in the reply"; fi
echo "  checkpoint kept from a reply: seq=$SEQ chain_head=$HEAD"
ask "trunc a long answer"
ask "fail upstream error"
ask "slow upstream (timeout)"
ask "bad body"
ask "tab in the output"

step "4. Verify the ledger offline"
python3 -I "$VERIFY" "$LEDGER" | sed 's/^/  /' || true
python3 -I "$VERIFY" --checkpoint "$SEQ:$HEAD" "$LEDGER" | sed 's/^/  /'

step "5. Tampering and truncation are detected"
cp "$LEDGER" "$W/edited.bin"
python3 -I -c 'import sys; p=sys.argv[1]; d=bytearray(open(p,"rb").read()); d[200]^=1; open(p,"wb").write(d)' "$W/edited.bin"
echo "  one byte changed:"
python3 -I "$VERIFY" "$W/edited.bin" | sed 's/^/    /' || true
cp "$LEDGER" "$W/cut.bin"
python3 -I -c '
import struct, sys
p, keep = sys.argv[1], int(sys.argv[2])
d = open(p, "rb").read(); o = 0
for _ in range(keep):
    (tl,) = struct.unpack_from("<I", d, o); (pl,) = struct.unpack_from("<Q", d, o + 4 + tl)
    o += 4 + tl + 8 + pl + 32
open(p, "wb").write(d[:o])' "$W/cut.bin" "$((SEQ - 1))"
echo "  history cut before the checkpoint (whole frames removed):"
python3 -I "$VERIFY" "$W/cut.bin" | sed 's/^/    /' || true
python3 -I "$VERIFY" --checkpoint "$SEQ:$HEAD" "$W/cut.bin" | sed 's/^/    /' || true

step "6. Restart recovery: the gateway is killed mid-request"
python3 -I "$CLIENT" "$SOCK" "hang until killed" >/dev/null 2>&1 &
CL=$!
i=0; while [ ! -f "$WAL" ]; do i=$((i + 1)); [ "$i" -lt 50 ] || die "no intent written"; sleep 0.1; done
echo "  open intent: $(grep ledger_seq "$WAL")"
kill -KILL "$GW_PID"; wait "$GW_PID" 2>/dev/null || true; GW_PID=""
wait "$CL" 2>/dev/null || true
start_gw
echo "  after restart: $(grep -c wal_recovered "$W/gw.log") recovery log line(s); intent cleared: $([ -f "$WAL" ] && echo no || echo yes)"
python3 -I "$VERIFY" --list "$LEDGER" | grep ' OBS ' | tail -n 1 | sed 's/^/  last record: /'
python3 -I "$VERIFY" --checkpoint "$SEQ:$HEAD" "$LEDGER" | tail -n 1 | sed 's/^/  /'

step "7. Storage fault: the gateway stops instead of serving on"
stop_gw
rm -f "$SOCK"
python3 -I -c '
import os, resource, signal, subprocess, sys, time
gw, conf, sock, ledger = sys.argv[1:5]
p = subprocess.Popen([gw, conf], preexec_fn=lambda: signal.signal(signal.SIGXFSZ, signal.SIG_IGN))
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.1)
lim = os.path.getsize(ledger) + 100
resource.prlimit(p.pid, resource.RLIMIT_FSIZE, (lim, lim))
open(sys.argv[5], "w").write("%d %d\n" % (p.pid, lim))
' "$GW" "$W/gw.conf" "$SOCK" "$LEDGER" "$W/limited" >/dev/null
read -r LPID LIMIT <"$W/limited"
echo "  gateway pid $LPID now limited to $LIMIT-byte files (a full disk, in effect)"
ask "ok this record cannot be written"
i=0; while kill -0 "$LPID" 2>/dev/null; do i=$((i + 1)); [ "$i" -lt 50 ] || die "gateway still running"; sleep 0.1; done
echo "  gateway exited; open intent kept: $([ -f "$WAL" ] && echo yes || echo no)"
grep -o '"message":"open intent after an internal fault[^"]*"' "$W/gw.log" | tail -n 1 | sed 's/^/  log: /'
echo "  restart with the fault still present:"
rc=0
python3 -I -c '
import resource, signal, subprocess, sys
lim = int(sys.argv[3])
def pre():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (lim, lim))
sys.exit(subprocess.call([sys.argv[1], sys.argv[2]], preexec_fn=pre))' "$GW" "$W/gw.conf" "$LIMIT" || rc=$?
echo "    refused to start (exit status $rc); intent still present: $([ -f "$WAL" ] && echo yes || echo no)"
echo "  fault cleared, normal start:"
start_gw
python3 -I "$VERIFY" --list "$LEDGER" | grep ' OBS ' | tail -n 1 | sed 's/^/    last record: /'

step "8. Which build wrote this segment"
python3 -I "$VERIFY" --list "$LEDGER" | grep ' STATE ' | tail -n 1 | sed 's/^/  /'
python3 -I "$VERIFY" --checkpoint "$SEQ:$HEAD" "$LEDGER" | sed 's/^/  /'

stop_gw
step "Done. Production paths were not used."

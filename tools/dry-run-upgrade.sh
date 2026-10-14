#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# dry-run-upgrade.sh - rehearse the production upgrade (UPGRADE.md) on a COPY
# of the production ledger, as an ordinary user, in a new temporary
# directory. It never writes to /var/lib/axioma-gateway or /etc, never uses
# the production socket or credentials, and talks only to a local stand-in.
#
# It proves, on real evidence:
#   1. the new gateway replays the existing ledger and starts;
#   2. the original bytes stay a byte-identical prefix after new records;
#   3. a checkpoint taken before the upgrade still matches afterwards;
#   4. restart recovery works on top of the existing history;
#   5. (rollback) the currently installed gateway still starts on the
#      upgraded copy, so going back keeps the ledger.
#
# usage: dry-run-upgrade.sh --snapshot FILE [--config FILE]
#                           [--new-gateway BIN] [--old-gateway BIN]
#   --snapshot     a copy of the production ledger (never the live file)
#   --config       production configuration to derive from
#                  (default /etc/axioma-gateway.conf; read only)
#   --new-gateway  default ./build-verify/axioma-gateway
#   --old-gateway  default /usr/local/bin/axioma-gateway; "none" skips the
#                  rollback check
#   AXIOMA_DRYRUN_KEEP=1 keeps the working directory

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
die() { echo "dry-run: FAIL: $1" >&2; exit 1; }
ok() { echo "  PASS  $1"; }
step() { printf '\n== %s\n' "$1"; }

[ "$(id -u)" -ne 0 ] || die "run as an ordinary user, not root"

SNAP="" CONF=/etc/axioma-gateway.conf NEW="" OLD=/usr/local/bin/axioma-gateway
while [ $# -gt 0 ]; do
    case "$1" in
        --snapshot) SNAP=$2; shift 2;;
        --config) CONF=$2; shift 2;;
        --new-gateway) NEW=$2; shift 2;;
        --old-gateway) OLD=$2; shift 2;;
        *) die "unknown argument $1";;
    esac
done
[ -n "$SNAP" ] || die "--snapshot is required"
[ -n "$NEW" ] || NEW="$HERE/../build-verify/axioma-gateway"
[ -x "$NEW" ] || die "new gateway not executable: $NEW"
[ -r "$SNAP" ] || die "cannot read snapshot $SNAP"
[ -r "$CONF" ] || die "cannot read configuration $CONF"
case "$(readlink -f "$SNAP")" in
    /var/lib/axioma-gateway/*) die "the snapshot must be a copy, not the live ledger";;
esac

tool() {
    if [ -x "$HERE/$1" ]; then echo "$HERE/$1"
    elif command -v "$1" >/dev/null 2>&1; then command -v "$1"
    else die "cannot find $1"; fi
}
STANDIN=$(tool axioma-standin)
CLIENT=$(tool axioma-client)
VERIFY=$(tool axioma-verify-ledger)

W=$(mktemp -d "${TMPDIR:-/tmp}/axioma-dryrun.XXXXXX")
GW_PID="" SI_PID=""
# Reached through the EXIT trap only.
# shellcheck disable=SC2317,SC2329
cleanup() {
    if [ -n "$GW_PID" ]; then kill -KILL "$GW_PID" 2>/dev/null || true; fi
    if [ -n "$SI_PID" ]; then kill -KILL "$SI_PID" 2>/dev/null || true; fi
    if [ "${AXIOMA_DRYRUN_KEEP:-0}" = 1 ]; then echo "dry-run: kept $W"; else rm -rf "$W"; fi
}
trap cleanup EXIT INT TERM
export AX_ORACLE_ANTHROPIC_KEY="dry-run-key-not-a-credential"
SOCK="$W/gw.sock"
LEDGER="$W/l6-evidence.bin"
WAL="$W/wal.open"

start_gw() { # start_gw <binary>
    rm -f "$SOCK"
    "$1" "$W/gw.conf" 2>>"$W/gateway.stderr" &
    GW_PID=$!
    i=0
    while [ ! -S "$SOCK" ]; do
        i=$((i + 1))
        if [ "$i" -ge 100 ] || ! kill -0 "$GW_PID" 2>/dev/null; then
            GW_PID=""; return 1
        fi
        sleep 0.1
    done
}
stop_gw() {
    if [ -n "$GW_PID" ]; then kill -TERM "$GW_PID"; wait "$GW_PID" || true; GW_PID=""; fi
}

step "1. Snapshot"
SIZE=$(wc -c <"$SNAP" | tr -d ' ')
SNAP_SHA=$(sha256sum "$SNAP" | cut -d' ' -f1)
echo "  $SNAP: $SIZE bytes, sha256 $SNAP_SHA"
# A fresh, writable copy: a snapshot is often kept read-only, and cp would
# carry that mode over, which the gateway (opening read-write) refuses.
(umask 027; cat "$SNAP" >"$LEDGER")
out=$(python3 -I "$VERIFY" "$LEDGER" || true)
printf '%s\n' "$out" | sed 's/^/  /'
N=$(printf '%s\n' "$out" | sed -n 's/^frames=\([0-9]*\) head=\([0-9a-f]*\).*/\1/p')
H=$(printf '%s\n' "$out" | sed -n 's/^frames=\([0-9]*\) head=\([0-9a-f]*\).*/\2/p')
printf '%s\n' "$out" | grep -q '^CONSISTENT_UNANCHORED' || die "the snapshot does not verify"
echo "  pre-upgrade checkpoint: $N:$H"

step "2. Derived configuration (paths moved into $W, upstream = local stand-in, UPGRADE.md B4 applied)"
python3 -I "$STANDIN" --port-file "$W/port" 2>"$W/standin.log" &
SI_PID=$!
i=0; while [ ! -s "$W/port" ]; do i=$((i + 1)); [ "$i" -lt 50 ] || die "stand-in did not start"; sleep 0.1; done
python3 -I - "$CONF" "$W" "$(cat "$W/port")" >"$W/gw.conf" <<'EOF'
import os, sys
conf, w, port = sys.argv[1], sys.argv[2], sys.argv[3]
seen = set()
moved = {"socket_path": "gw.sock", "ledger_path": "l6-evidence.bin",
         "wal_path": "wal.open", "log_path": "gw.log",
         "export_path": "l6-evidence-export.bin"}
for line in open(conf):
    s = line.strip()
    if not s or s.startswith("#"):
        continue
    k, _, v = s.partition("=")
    k = k.strip()
    seen.add(k)
    if k in moved:
        v = os.path.join(w, moved[k])
    elif k == "api_url":
        v = "http://127.0.0.1:%s/v1/messages" % port
    elif k == "fixture_capture_path":
        continue
    print("%s=%s" % (k, v))
if "max_attempts" not in seen:
    print("max_attempts=1")  # the line UPGRADE.md step B4 adds
EOF
sed 's/^/  /' "$W/gw.conf"
if grep -E '/var/lib/axioma-gateway|/etc/' "$W/gw.conf"; then die "derived configuration still names a production path"; fi

step "3. New gateway on the copy"
start_gw "$NEW" || die "the new gateway did not start on the existing ledger (see $W/gw.log)"
ok "new gateway replayed $N existing frames and started"
for p in "ok dry run one" "ok dry run two"; do
    python3 -I "$CLIENT" "$SOCK" "$p" | grep -E '^(status|seq):' | tr '\n' ' ' | sed 's/^/  /'; echo
done
python3 -I "$CLIENT" "$SOCK" "hang until killed" >/dev/null 2>&1 &
i=0; while [ ! -f "$WAL" ]; do i=$((i + 1)); [ "$i" -lt 50 ] || die "no intent written"; sleep 0.1; done
kill -KILL "$GW_PID"; wait "$GW_PID" 2>/dev/null || true; GW_PID=""
start_gw "$NEW" || die "restart after kill failed"
[ ! -f "$WAL" ] || die "intent not recovered"
ok "killed mid-request and restarted: the interrupted call is recorded"
stop_gw

step "4. Evidence checks"
cmp -n "$SIZE" "$SNAP" "$LEDGER" || die "the original bytes changed"
ok "original $SIZE bytes are a byte-identical prefix of the upgraded ledger"
[ "$(sha256sum "$SNAP" | cut -d' ' -f1)" = "$SNAP_SHA" ] || die "the snapshot file changed"
ok "snapshot file unchanged"
out=$(python3 -I "$VERIFY" --checkpoint "$N:$H" "$LEDGER" || true)
printf '%s\n' "$out" | sed 's/^/  /'
printf '%s\n' "$out" | grep -q '^MATCHES_CHECKPOINT' || die "the pre-upgrade checkpoint does not match"
ok "pre-upgrade checkpoint matches"
if [ -f "$W/l6-evidence-export.bin" ]; then
    cmp "$LEDGER" "$W/l6-evidence-export.bin" || die "export differs from the primary"
    ok "export mirror is byte-identical to the primary"
fi

step "5. Rollback: the installed gateway on the upgraded copy"
if [ "$OLD" = none ]; then
    echo "  skipped (--old-gateway none)"
elif [ -x "$OLD" ]; then
    start_gw "$OLD" || die "the installed gateway could not start on the upgraded ledger"
    stop_gw
    ok "installed gateway $OLD replays the upgraded ledger and starts"
    cmp -n "$SIZE" "$SNAP" "$LEDGER" || die "rollback changed the original bytes"
    python3 -I "$VERIFY" --checkpoint "$N:$H" "$LEDGER" | grep -q '^MATCHES_CHECKPOINT' \
        || die "checkpoint lost after rollback"
    ok "after rollback: original prefix and checkpoint intact"
else
    echo "  skipped ($OLD not executable)"
fi

step "DRY RUN PASSED (production files were not used)"

#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# test_gw_client.sh - PCR-004 residual (d): the reference client
# gateway/gw-client.py must send a request the gateway accepts.
#
# The gateway requires an explicit temperature_q16 line (G1) and refuses
# a request without it (reason temperature_required). The client
# predated that rule. This drives the real gateway against a local
# stand-in oracle endpoint (no network egress, no spend) and checks that
# a request from gw-client.py is served and committed:
#   1. default call: status ok, COMPLETE/NULL, temperature_q16 0 echoed;
#   2. explicit TEMPERATURE_Q16 argument (45875) echoed as sent;
#   3. the ledger holds two AX:OBS:v1 records, both COMPLETE/NULL,
#      with temperature 0 and 45875.
#
# usage: test_gw_client.sh <axioma-gateway binary> <gw-client.py>

set -eu

GW_BIN="${1:?usage: test_gw_client.sh <axioma-gateway> <gw-client.py>}"
CLIENT="${2:?usage: test_gw_client.sh <axioma-gateway> <gw-client.py>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 required" >&2
    exit 77
fi

TMP="$(mktemp -d)"
GW_PID=""
API_PID=""

# Reached only through the EXIT/INT/TERM trap below, which ShellCheck cannot follow.
# shellcheck disable=SC2317,SC2329
cleanup() {
    if [ -n "${GW_PID}" ];  then kill -KILL "${GW_PID}"  2>/dev/null || true; fi
    if [ -n "${API_PID}" ]; then kill -KILL "${API_PID}" 2>/dev/null || true; fi
    rm -rf "${TMP}"
}
trap cleanup EXIT INT TERM

fail() { echo "FAIL: $1" >&2; exit 1; }

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
PORTF="${TMP}/port"

# Local stand-in for the oracle endpoint: one well-formed reply per POST.
python3 -I - "${PORTF}" <<'PYEOF' &
import http.server, json, sys
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass
    def do_POST(self):
        self.rfile.read(int(self.headers.get("content-length", "0")))
        body = json.dumps({"content": [{"type": "text", "text": "served"}],
                           "stop_reason": "end_turn"}).encode()
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
srv = http.server.HTTPServer(("127.0.0.1", 0), H)
with open(sys.argv[1], "w") as f:
    f.write(str(srv.server_address[1]))
srv.serve_forever()
PYEOF
API_PID=$!

wait_for test -s "${PORTF}" || fail "stand-in endpoint never reported its port"

cat > "${TMP}/gw.conf" <<CFGEOF
socket_path=${SOCK}
ledger_path=${TMP}/evidence.bin
wal_path=${TMP}/intent.wal
log_path=${TMP}/gw.log
model_id=test-model
oracle_id=test-oracle
api_url=http://127.0.0.1:$(cat "${PORTF}")/v1/messages
max_attempts=1
connect_timeout_s=5
response_timeout_s=10
call_budget=5
CFGEOF

# Never sent anywhere but the local stand-in.
AX_ORACLE_ANTHROPIC_KEY="test-key-not-real"
export AX_ORACLE_ANTHROPIC_KEY

"${GW_BIN}" "${TMP}/gw.conf" &
GW_PID=$!
wait_for test -S "${SOCK}" || fail "gateway socket never appeared"

R1="$(python3 -I "${CLIENT}" "${SOCK}" "hello from the reference client")"
printf '%s\n' "--- default call" "${R1}"
printf '%s\n' "${R1}" | grep -qx 'status: ok' || fail "default call not served"
printf '%s\n' "${R1}" | grep -qx 'completion_state: COMPLETE' || fail "default call not COMPLETE"
printf '%s\n' "${R1}" | grep -qx 'temperature_q16: 0' || fail "default temperature_q16 not 0"

R2="$(python3 -I "${CLIENT}" "${SOCK}" "second call" 45875)"
printf '%s\n' "--- TEMPERATURE_Q16 45875" "${R2}"
printf '%s\n' "${R2}" | grep -qx 'status: ok' || fail "explicit temperature call not served"
printf '%s\n' "${R2}" | grep -qx 'temperature_q16: 45875' || fail "explicit temperature_q16 not echoed"

kill -TERM "${GW_PID}"
rc=0
wait "${GW_PID}" || rc=$?
GW_PID=""
[ "${rc}" -eq 0 ] || fail "gateway stop: exit status ${rc}"

RECS="$(python3 -I - "${TMP}/evidence.bin" <<'PYEOF'
import json, struct, sys
d = open(sys.argv[1], "rb").read()
o = 0
while o < len(d):
    (tl,) = struct.unpack_from("<I", d, o); o += 4
    tag = d[o:o + tl]; o += tl
    (pl,) = struct.unpack_from("<Q", d, o); o += 8
    p = d[o:o + pl]; o += pl + 32
    if tag == b"AX:OBS:v1":
        r = json.loads(p)
        print(r["completion_state"], r["failure_type"], r["params"]["temperature"])
PYEOF
)"
printf '%s\n' "--- committed AX:OBS:v1 records (completion_state failure_type temperature)" "${RECS}"
[ "${RECS}" = "$(printf 'COMPLETE None 0\nCOMPLETE None 45875')" ] || fail "ledger records not as expected"

echo "OK: gw-client.py requests accepted, served and committed"
exit 0

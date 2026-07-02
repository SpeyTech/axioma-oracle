#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# check_readiness.sh - E2 acceptance on the deployment host.
#
# 50 iterations of systemctl restart followed by an immediate client
# connect. Under Type=notify, restart blocks until the service has
# sent READY=1 after bind+listen, so every connect must succeed with
# zero refusals. Run as a user permitted both to restart the unit and
# to open the socket (mode 0660, group axgw).
#
# usage: check_readiness.sh <socket_path> [unit] [iterations]

set -eu

SOCK="${1:?usage: check_readiness.sh <socket_path> [unit] [iterations]}"
UNIT="${2:-axioma-gateway}"
N="${3:-50}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 required" >&2
    exit 77
fi

i=1
while [ "${i}" -le "${N}" ]; do
    # Each restart counts against StartLimitBurst (default 5 in 10s);
    # the harness resets the counter rather than weakening the unit.
    systemctl reset-failed "${UNIT}" 2>/dev/null || true
    systemctl restart "${UNIT}"
    if ! python3 -c 'import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.close()' "${SOCK}"; then
        echo "FAIL: connection refused on iteration ${i}/${N}" >&2
        exit 1
    fi
    i=$((i + 1))
done

echo "OK: ${N}/${N} immediate connects after restart, zero refusals"
exit 0

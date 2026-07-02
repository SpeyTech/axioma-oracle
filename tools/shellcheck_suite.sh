#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# tools/shellcheck_suite.sh - lint every shell script in the repo, as a
# ctest gate. E1 and the anchor guard were the same lesson twice in
# one day: deployment finding what no test exercises. Scripts are
# code and get a gate.

set -eu

cd "$(dirname "$0")/.."

if ! command -v shellcheck >/dev/null 2>&1; then
    echo "FAIL: shellcheck not installed (apt install shellcheck)" >&2
    exit 1
fi

found=0
for f in tools/*.sh gateway/tests/*.sh; do
    [ -e "${f}" ] || continue
    found=1
    shellcheck -x "${f}"
done

if [ "${found}" -ne 1 ]; then
    echo "FAIL: no shell scripts found; glob is wrong" >&2
    exit 1
fi

echo "OK: shellcheck clean"
exit 0

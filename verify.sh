#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# verify.sh - the one build-and-test procedure for axioma-oracle
# (INSTALL.md). Builds in ./build-verify (or $1), runs every test (fingerprint
# checks included), rebuilds in a second directory and compares the gateway
# binaries for reproducibility, and prints a summary. It installs nothing,
# needs no network and never touches /var/lib/axioma-gateway or /etc.
#
# Environment: CC (compiler, default cc), VERIFY_SANITIZE=1 adds an
# AddressSanitizer + UndefinedBehaviorSanitizer build and test pass.
#
# usage: ./verify.sh [build-dir]

set -eu

SRC=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$SRC/build-verify"}
CC=${CC:-cc}
export CC

fail() { echo "VERIFY FAILED: $1" >&2; exit 1; }

echo "== environment"
echo "arch:      $(uname -m)"
echo "compiler:  $("$CC" --version | head -n 1)"
echo "cmake:     $(cmake --version | head -n 1)"
echo "openssl:   $(openssl version 2>/dev/null || echo 'openssl CLI not installed')"
echo "curl:      $(curl --version 2>/dev/null | head -n 1 || echo 'curl CLI not installed')"
echo "python3:   $(python3 --version 2>&1)"
if git -C "$SRC" rev-parse --verify HEAD >/dev/null 2>&1; then
    echo "source:    $(git -C "$SRC" rev-parse HEAD)"
    if [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]; then
        echo "           (modified tracked files: source_commit will be 'unknown')"
    fi
else
    echo "source:    not a git checkout (source_commit will be 'unknown')"
fi

build() { # build <dir> [extra C flags]
    cmake -S "$SRC" -B "$1" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_FLAGS="${2:-}" \
          -DCMAKE_EXE_LINKER_FLAGS="${3:-}" >"$1.configure.log" 2>&1 \
        || { cat "$1.configure.log"; fail "configure ($1)"; }
    cmake --build "$1" -j "$(nproc 2>/dev/null || echo 2)" >"$1.build.log" 2>&1 \
        || { tail -n 40 "$1.build.log"; fail "build ($1)"; }
}

echo "== build and test ($OUT)"
mkdir -p "$OUT"
build "$OUT"
ctest --test-dir "$OUT" --output-on-failure >"$OUT.ctest.log" 2>&1 \
    || { grep -E 'Failed|\*\*\*' "$OUT.ctest.log"; fail "tests (see $OUT.ctest.log)"; }
grep -E 'tests passed' "$OUT.ctest.log"

echo "== reproducibility (second build in $OUT-repro)"
mkdir -p "$OUT-repro"
build "$OUT-repro"
a=$(sha256sum "$OUT/axioma-gateway" | cut -d' ' -f1)
b=$(sha256sum "$OUT-repro/axioma-gateway" | cut -d' ' -f1)
[ "$a" = "$b" ] || fail "two builds of the same source differ ($a / $b)"
echo "axioma-gateway sha256 $a (identical in both builds)"

if [ "${VERIFY_SANITIZE:-0}" = 1 ]; then
    echo "== sanitizers ($OUT-asan)"
    mkdir -p "$OUT-asan"
    build "$OUT-asan" "-fsanitize=address,undefined -fno-sanitize-recover=all" \
          "-fsanitize=address,undefined"
    ctest --test-dir "$OUT-asan" --output-on-failure >"$OUT-asan.ctest.log" 2>&1 \
        || { grep -E 'Failed|\*\*\*' "$OUT-asan.ctest.log"; fail "sanitizer tests"; }
    grep -E 'tests passed' "$OUT-asan.ctest.log"
fi

echo "== summary"
echo "source_commit recorded by this build: $(sed -n 's/^#define GW_SOURCE_COMMIT "\(.*\)"/\1/p' "$OUT/gateway-gen/gw_source.h")"
echo "gateway:  $OUT/axioma-gateway"
echo "VERIFY PASSED"

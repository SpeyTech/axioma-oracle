# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
#
# check_fingerprint.cmake - run a fingerprint program and compare the
# SHA-256 of its output with the first field of the pinned file.
# usage: cmake -DPROG=<program> -DPIN=<fingerprints file> -P check_fingerprint.cmake

execute_process(COMMAND ${PROG} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fingerprint program failed: ${rc}")
endif()
string(SHA256 got "${out}")
file(STRINGS "${PIN}" lines LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9a-f]+" want "${lines}")
if(NOT got STREQUAL want)
    message(FATAL_ERROR "fingerprint mismatch\n  got  ${got}\n  want ${want}")
endif()
message(STATUS "fingerprint ${got} matches ${PIN}")

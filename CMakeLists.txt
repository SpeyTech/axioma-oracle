cmake_minimum_required(VERSION 3.16)
project(axioma_oracle
    VERSION 0.1.0
    LANGUAGES C
    DESCRIPTION "Axioma L3 Oracle Boundary Gateway"
)

# ==============================================================================
# Hardened Determinism Configuration (Per Axioma Framework 2026-03-26)
# ==============================================================================

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# D1/D2 Determinism Flags — Mandatory for bit-identical cross-platform results
add_compile_options(
    # Standard strictness
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wconversion -Wstrict-prototypes
    -Wno-sign-conversion
    -fno-common
    
    # Determinism hardening
    -fno-strict-aliasing      # Prevent UB in type-punning
    -fwrapv                   # Defined signed overflow (wrap)
    -fno-tree-vectorize       # Prevent SIMD drift across platforms
    -fno-builtin              # Ensure library function purity
    -fno-omit-frame-pointer   # Required for reliable stack traces
    -fstack-protector-strong  # Security hardening
    
    # No floating point
    -ffp-contract=off
)

# Build type configuration
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# ==============================================================================
# Substrate: SHA-256 and domain-separated commitment over platform libcrypto
# (owner decision D5; substrate/src/axilog_crypto.c)
# ==============================================================================

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

add_library(axilog_substrate STATIC
    substrate/src/axilog_crypto.c
)
target_include_directories(axilog_substrate PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/substrate/include
)
target_link_libraries(axilog_substrate PUBLIC OpenSSL::Crypto)

# ==============================================================================
# Library Target
# ==============================================================================

add_library(axioma_oracle STATIC
    src/hash.c
    src/validate.c
    src/canonical.c
    src/obs.c
    src/admission.c
)

target_include_directories(axioma_oracle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


# axilog_sha256() and axilog_commit() (DVEC-001 section 4.3) come from the
# substrate library above; its headers are public through this link.
target_link_libraries(axioma_oracle PUBLIC axilog_substrate)

# ==============================================================================
# Core Test Targets
# ==============================================================================

enable_testing()

# Test: Canonicalisation
add_executable(test_obs_canonical tests/test_obs_canonical.c)
target_link_libraries(test_obs_canonical axioma_oracle)
add_test(NAME test_obs_canonical COMMAND test_obs_canonical)

# Test: Hash Computation
add_executable(test_obs_hash tests/test_obs_hash.c)
target_link_libraries(test_obs_hash axioma_oracle)
add_test(NAME test_obs_hash COMMAND test_obs_hash)

# Test: Encoding Validation
add_executable(test_encoding tests/test_encoding.c)
target_link_libraries(test_encoding axioma_oracle)
add_test(NAME test_encoding COMMAND test_encoding)

# Test: Ordering Enforcement
add_executable(test_ordering tests/test_ordering.c)
target_link_libraries(test_ordering axioma_oracle)
add_test(NAME test_ordering COMMAND test_ordering)

# Test: Truncation
add_executable(test_truncation tests/test_truncation.c)
target_link_libraries(test_truncation axioma_oracle)
add_test(NAME test_truncation COMMAND test_truncation)

# Test: Replay Identity
add_executable(test_replay_identity tests/test_replay_identity.c)
target_link_libraries(test_replay_identity axioma_oracle)
add_test(NAME test_replay_identity COMMAND test_replay_identity)

# ==============================================================================
# Audit Verification Tests
# ==============================================================================

# Test: Cross-Build Identity
add_executable(test_cross_build_identity tests/test_cross_build_identity.c)
target_link_libraries(test_cross_build_identity axioma_oracle)
add_test(NAME test_cross_build_identity COMMAND test_cross_build_identity)

# Test: obs_hash Domain Verification
add_executable(test_obs_hash_domain tests/test_obs_hash_domain.c)
target_link_libraries(test_obs_hash_domain axioma_oracle)
add_test(NAME test_obs_hash_domain COMMAND test_obs_hash_domain)

# Test: Rigorous Encoding
add_executable(test_encoding_rigorous tests/test_encoding_rigorous.c)
target_link_libraries(test_encoding_rigorous axioma_oracle)
add_test(NAME test_encoding_rigorous COMMAND test_encoding_rigorous)

# Test: Truncation Safety
add_executable(test_truncation_safety tests/test_truncation_safety.c)
target_link_libraries(test_truncation_safety axioma_oracle)
add_test(NAME test_truncation_safety COMMAND test_truncation_safety)

# PCR-001: canonical records never silently lose content.
add_executable(test_canonical_no_loss tests/test_canonical_no_loss.c)
target_link_libraries(test_canonical_no_loss axioma_oracle)
add_test(NAME test_canonical_no_loss COMMAND test_canonical_no_loss)

# SRS-004-SHALL-048: complete-record limit enforced (owner decision 2026-10-12).
add_executable(test_obs_record_max tests/test_obs_record_max.c)
target_link_libraries(test_obs_record_max axioma_oracle)
add_test(NAME test_obs_record_max COMMAND test_obs_record_max)

# PCR-004 (B-07): admission fails closed on malformed identifiers, enums,
# enum pairs and inputs; v1 empty-input hash pinned (owner decision 2026-10-12).
add_executable(test_admission_fail_closed tests/test_admission_fail_closed.c)
target_link_libraries(test_admission_fail_closed axioma_oracle)
add_test(NAME test_admission_fail_closed COMMAND test_admission_fail_closed)

# PCR-004 residual (a): ERROR input with output over AX_MAX_OUTPUT_BYTES is
# rejected before truncation can relabel it (owner direction 2026-10-12).
add_executable(test_admission_error_oversize tests/test_admission_error_oversize.c)
target_link_libraries(test_admission_error_oversize axioma_oracle)
add_test(NAME test_admission_error_oversize COMMAND test_admission_error_oversize)

# PCR-004 residual (b): NULL output with output_len > 0 is rejected; NULL
# output with output_len == 0 is pinned unchanged (owner direction 2026-10-12).
add_executable(test_admission_null_output tests/test_admission_null_output.c)
target_link_libraries(test_admission_null_output axioma_oracle)
add_test(NAME test_admission_null_output COMMAND test_admission_null_output)

# Historical record classifier boundary (SRS-004-AMD-001): stdlib Python only.
find_program(PYTHON3_EXE python3 REQUIRED)
add_test(NAME test_obs_v1_loss_check
         COMMAND ${PYTHON3_EXE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_obs_v1_loss_check.py
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools)
# The classifier as its consumers run it: exit status and summary format,
# including the 0 -> 3 change for forbidden-pair ledgers (tools/README.md).
add_test(NAME test_obs_v1_loss_check_cli
         COMMAND ${PYTHON3_EXE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_obs_v1_loss_check_cli.py
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/obs_v1_loss_check.py)

# Ledger (imported from axioma-audit c40810b; ledger/): append, genesis,
# commitment and chain recomputation.
add_library(axioma_ledger STATIC ledger/src/ledger.c)
target_include_directories(axioma_ledger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ledger/include)
target_link_libraries(axioma_ledger PUBLIC axilog_substrate)
foreach(t test_ledger test_commitment test_chain_verify)
    add_executable(${t} ledger/tests/${t}.c)
    target_link_libraries(${t} axioma_ledger)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# Cross-architecture fingerprints: the program's output hash must equal the
# pinned value on every build (ci/check_fingerprint.cmake).
add_executable(fingerprint_oracle ci/fingerprint.c)
target_link_libraries(fingerprint_oracle axioma_oracle)
add_test(NAME test_fingerprint_oracle
         COMMAND ${CMAKE_COMMAND} -DPROG=$<TARGET_FILE:fingerprint_oracle>
                 -DPIN=${CMAKE_CURRENT_SOURCE_DIR}/ci/fingerprints.txt
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/ci/check_fingerprint.cmake)
add_executable(fingerprint_ledger ci/fingerprint_ledger.c)
target_link_libraries(fingerprint_ledger axioma_ledger)
add_test(NAME test_fingerprint_ledger
         COMMAND ${CMAKE_COMMAND} -DPROG=$<TARGET_FILE:fingerprint_ledger>
                 -DPIN=${CMAKE_CURRENT_SOURCE_DIR}/ci/fingerprints_ledger.txt
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/ci/check_fingerprint.cmake)

# Test: NFC Limitation Documentation
add_executable(test_nfc_limitation tests/test_nfc_limitation.c)
target_link_libraries(test_nfc_limitation axioma_oracle)
add_test(NAME test_nfc_limitation COMMAND test_nfc_limitation)

# Test: L3 Hardening (H1-H4 audit closure)
add_executable(test_hardening tests/test_hardening.c)
target_link_libraries(test_hardening axioma_oracle)
add_test(NAME test_hardening COMMAND test_hardening)

# Test: Fault Attribution (E-ABI-1 regression)
# The defect was misattribution across the axilog_commit boundary and
# nothing previously exercised the fault path; this does.
add_executable(test_fault_attribution tests/test_fault_attribution.c)
target_link_libraries(test_fault_attribution axioma_oracle)
add_test(NAME test_fault_attribution COMMAND test_fault_attribution)

# ==============================================================================
# Sanitizer Builds (Optional)
# ==============================================================================

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

if(ENABLE_UBSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif()

# ==============================================================================
# Install Configuration
# ==============================================================================

# Installation is the gateway, its tools and their documentation only
# (INSTALL.md). The libraries are internal to this build.

# ==============================================================================
# Summary
# ==============================================================================

message(STATUS "")
message(STATUS "axioma-oracle configuration:")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  C Standard:  C${CMAKE_C_STANDARD}")
message(STATUS "  Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  ASAN:        ${ENABLE_ASAN}")
message(STATUS "  UBSAN:       ${ENABLE_UBSAN}")
message(STATUS "")

# ==============================================================================
# L3 Gateway Service (net-new service layer; the library is not modified)
# ==============================================================================
option(ORACLE_BUILD_GATEWAY "Build the gateway service" ON)
if(ORACLE_BUILD_GATEWAY)
    find_package(CURL REQUIRED)

    # TU group B: the ledger seam compiles against ledger + substrate only.
    # E-ABI-1 is fixed (ct_fault_flags_t is one type estate-wide) but the
    # disjoint include sets stay: defence in depth, per CONFORMANCE.md §12.
    add_library(gw-ledger STATIC
        gateway/src/gw_ledger.c
    )
    target_include_directories(gw-ledger PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gateway/include
    )
    target_link_libraries(gw-ledger PRIVATE axioma_ledger)

    # TU group A: everything else, against the oracle library.
    add_executable(axioma-gateway
        gateway/src/gw_main.c
        gateway/src/gw_config.c
        gateway/src/gw_log.c
        gateway/src/gw_json.c
        gateway/src/gw_wal.c
        gateway/src/gw_client.c
        gateway/src/gw_notify.c
    )
    target_include_directories(axioma-gateway PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gateway/include
    )
    target_link_libraries(axioma-gateway PRIVATE
        axioma_oracle gw-ledger CURL::libcurl)

    # Source identification for AX:STATE:v1 (CONTRACT.md section 6):
    # regenerated at every build; the header changes only when the value does.
    set(GW_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gateway-gen)
    file(MAKE_DIRECTORY ${GW_GEN_DIR})
    add_custom_target(gw_source_id
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUT=${GW_GEN_DIR}/gw_source.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/gateway/cmake/gw_source.cmake
        BYPRODUCTS ${GW_GEN_DIR}/gw_source.h
        COMMENT "Recording the axioma-oracle source commit"
        VERBATIM)
    add_dependencies(axioma-gateway gw_source_id)
    target_include_directories(axioma-gateway PRIVATE ${GW_GEN_DIR})

    add_executable(test_gw_json
        gateway/tests/test_gw_json.c gateway/src/gw_json.c)
    target_include_directories(test_gw_json PRIVATE gateway/include)
    add_test(NAME test_gw_json COMMAND test_gw_json)

    add_executable(test_gw_ledger gateway/tests/test_gw_ledger.c)
    target_include_directories(test_gw_ledger PRIVATE gateway/include)
    target_link_libraries(test_gw_ledger PRIVATE gw-ledger axioma_ledger)
    add_test(NAME test_gw_ledger COMMAND test_gw_ledger)

    add_executable(test_gw_export gateway/tests/test_gw_export.c)
    target_include_directories(test_gw_export PRIVATE gateway/include)
    target_link_libraries(test_gw_export PRIVATE gw-ledger axioma_ledger)
    add_test(NAME test_gw_export COMMAND test_gw_export)

    add_executable(test_gw_wal
        gateway/tests/test_gw_wal.c gateway/src/gw_wal.c)
    target_include_directories(test_gw_wal PRIVATE gateway/include)
    add_test(NAME test_gw_wal COMMAND test_gw_wal)

    # PCR-004 residual (c): model_id and oracle_id checked at config load
    # with the admission identifier rules (owner direction 2026-10-12).
    add_executable(test_gw_config
        gateway/tests/test_gw_config.c gateway/src/gw_config.c)
    target_include_directories(test_gw_config PRIVATE gateway/include)
    target_link_libraries(test_gw_config PRIVATE axioma_oracle)
    add_test(NAME test_gw_config COMMAND test_gw_config)

    add_executable(test_gw_fixture
        gateway/tests/test_gw_fixture.c gateway/src/gw_json.c)
    target_include_directories(test_gw_fixture PRIVATE gateway/include)
    target_link_libraries(test_gw_fixture PRIVATE axioma_oracle)
    add_test(NAME test_gw_fixture COMMAND test_gw_fixture)

    # E1 regression and kill-durability: signal delivery against the
    # running service. E1 was found by deployment because nothing in
    # the suite exercised it; this does.
    add_test(NAME test_gw_signals COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_signals.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_signals PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # PCR-004 residual (d): the reference client sends the required
    # temperature_q16 line and its requests are served and committed.
    add_test(NAME test_gw_client COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_client.sh
             $<TARGET_FILE:axioma-gateway>
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/gw-client.py)
    set_tests_properties(test_gw_client PROPERTIES
        TIMEOUT 60
        SKIP_RETURN_CODE 77)

    # PCR-004 residual (c): a configuration refused at load makes zero
    # upstream calls and writes nothing (owner direction 2026-10-12,
    # Round 7 item 7).
    add_test(NAME test_gw_config_zero_upstream COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_config_zero_upstream.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_config_zero_upstream PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # The upstream request body stays well-formed JSON for any configured
    # model_id (quotes, backslashes, JSON fragments); local stand-in API.
    add_test(NAME test_gw_request_json COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_request_json.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_request_json PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # anthropic_version is copied into an HTTP request header: a control
    # character in it is refused at configuration load (owner direction
    # 2026-10-12, Round 7 item 3).
    add_executable(test_gw_config_header
        gateway/tests/test_gw_config_header.c gateway/src/gw_config.c)
    target_include_directories(test_gw_config_header PRIVATE gateway/include)
    target_link_libraries(test_gw_config_header PRIVATE axioma_oracle)
    add_test(NAME test_gw_config_header COMMAND test_gw_config_header)

    add_test(NAME test_gw_api_version_header COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_api_version_header.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_api_version_header PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # The socket response header is complete and snapshot_id equals the
    # configured model_id for every accepted model_id length (up to 255
    # bytes); before the fix it overflowed a 512-byte buffer.
    add_test(NAME test_gw_response_header COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_response_header.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_response_header PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # The API key from the environment is copied into an HTTP request
    # header: a control character in it is refused at startup, and the key
    # is never printed (Round 7 independent review, item 3).
    add_test(NAME test_gw_api_key_header COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_api_key_header.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_api_key_header PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # An api_url the HTTP client cannot parse is refused at startup, using
    # libcurl's own URL parser (Round 7 independent review, item 3).
    add_test(NAME test_gw_api_url COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_api_url.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_api_url PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77)

    # Transfers use HTTP or HTTPS only: at startup (scheme check) and on
    # every transfer (CURLOPT_PROTOCOLS_STR) (owner decision D5, 2026-10-13).
    add_executable(test_gw_client_protocols
        gateway/tests/test_gw_client_protocols.c gateway/src/gw_client.c)
    target_include_directories(test_gw_client_protocols PRIVATE gateway/include)
    target_link_libraries(test_gw_client_protocols PRIVATE CURL::libcurl)
    add_test(NAME test_gw_client_protocols COMMAND test_gw_client_protocols)

    add_test(NAME test_gw_api_url_scheme COMMAND sh
             ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_api_url_scheme.sh
             $<TARGET_FILE:axioma-gateway>)
    set_tests_properties(test_gw_api_url_scheme PROPERTIES
        TIMEOUT 180
        SKIP_RETURN_CODE 77)

    # Evidence accounting and intent recovery (CONTRACT.md; recovery plan
    # revision 2, E3): B-02, open-intent exit, accounting, STATE source.
    foreach(t b02_1 b02_2 b02_3 b02_4 b02_5 oi_1 acc_1 state_source)
        add_test(NAME test_gw_recovery_${t}
                 COMMAND ${PYTHON3_EXE} -I
                         ${CMAKE_CURRENT_SOURCE_DIR}/gateway/tests/test_gw_recovery.py
                         $<TARGET_FILE:axioma-gateway> ${t}
                         ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(test_gw_recovery_${t} PROPERTIES
            TIMEOUT 180
            SKIP_RETURN_CODE 77)
    endforeach()
endif()

# Shell tooling lint gate: scripts are code. Fails loudly when
# shellcheck is not installed rather than passing vacuously.
add_test(NAME test_shellcheck COMMAND sh
         ${CMAKE_CURRENT_SOURCE_DIR}/tools/shellcheck_suite.sh)

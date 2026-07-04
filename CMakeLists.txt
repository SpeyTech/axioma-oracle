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


# ==============================================================================
# libaxilog Linkage (sdk-root)
# ==============================================================================

# axilog_sha256() and axilog_commit() — replaces standalone hash.c SHA-256
# Aligns oracle with DVEC-001 §4.3 domain-separated commitment format
set(AXILOG_SDK_ROOT "$ENV{HOME}/axilog/sdk-root"
    CACHE PATH "Path to axioma sdk-root installation")

target_include_directories(axioma_oracle PUBLIC
    ${AXILOG_SDK_ROOT}/include
)

target_link_libraries(axioma_oracle PRIVATE
    ${AXILOG_SDK_ROOT}/lib/libaxilog.a
)

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

install(TARGETS axioma_oracle
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/axilog
    DESTINATION include
)

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

    set(AXIOMA_AUDIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../axioma-audit"
        CACHE PATH "Path to axioma-audit checkout")

    # TU group B: the ledger seam compiles against audit + substrate only.
    # E-ABI-1 is fixed (ct_fault_flags_t is one type estate-wide) but the
    # disjoint include sets stay: defence in depth, per CONFORMANCE.md §12.
    add_library(gw-ledger STATIC
        gateway/src/gw_ledger.c
        ${AXIOMA_AUDIT_ROOT}/src/ledger.c
    )
    target_include_directories(gw-ledger PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gateway/include
        ${AXIOMA_AUDIT_ROOT}/include
        ${AXILOG_SDK_ROOT}/include
    )
    target_link_libraries(gw-ledger PRIVATE ${AXILOG_SDK_ROOT}/lib/libaxilog.a)

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

    add_executable(test_gw_json
        gateway/tests/test_gw_json.c gateway/src/gw_json.c)
    target_include_directories(test_gw_json PRIVATE gateway/include)
    add_test(NAME test_gw_json COMMAND test_gw_json)

    add_executable(test_gw_ledger gateway/tests/test_gw_ledger.c)
    target_include_directories(test_gw_ledger PRIVATE
        gateway/include ${AXIOMA_AUDIT_ROOT}/include ${AXILOG_SDK_ROOT}/include)
    target_link_libraries(test_gw_ledger PRIVATE gw-ledger
        ${AXILOG_SDK_ROOT}/lib/libaxilog.a)
    add_test(NAME test_gw_ledger COMMAND test_gw_ledger)

    add_executable(test_gw_wal
        gateway/tests/test_gw_wal.c gateway/src/gw_wal.c)
    target_include_directories(test_gw_wal PRIVATE gateway/include)
    add_test(NAME test_gw_wal COMMAND test_gw_wal)

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
endif()

# Shell tooling lint gate: scripts are code. Fails loudly when
# shellcheck is not installed rather than passing vacuously.
add_test(NAME test_shellcheck COMMAND sh
         ${CMAKE_CURRENT_SOURCE_DIR}/tools/shellcheck_suite.sh)

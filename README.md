# axioma-oracle

**Axioma L3, Oracle Boundary Gateway Layer**

A cryptographic containment boundary for non-deterministic systems.

## Overview

`axioma-oracle` implements the Oracle Boundary Gateway Contract defined in SRS-004 v0.3. It transforms non-deterministic oracle outputs (non-deterministic inference systems, ML models, external APIs) into immutable, canonical, ordered evidence before they can influence deterministic computation.

**Governing Principle:**

> Inference outputs are not trusted. They are recorded.
> Inference is not execution. Inference is evidence.

## This repository

One self-contained repository, built and tested by one script:

| Part | Where | What it is |
|---|---|---|
| Oracle library | `src/`, `include/` | AX:OBS:v1 admission, canonicalisation and hashing (SRS-004) |
| Ledger | `ledger/`, `substrate/` | Append-only, hash-chained evidence frames; SHA-256 and the commitment |
| Gateway | `gateway/` | `axioma-gateway`: a serial AF_UNIX service that records every accepted upstream call |
| Tools | `tools/` | `axioma-verify-ledger` (offline verifier), `axioma-client`, `axioma-standin` (local upstream for tests and the demo), `demo.sh`, `dry-run-upgrade.sh` |

Documents: [CONTRACT.md](CONTRACT.md) (what the gateway promises, and what
it does not), [INSTALL.md](INSTALL.md), [OPERATOR.md](OPERATOR.md) and
[UPGRADE.md](UPGRADE.md).

## Features

- **AX:OBS:v1 Record Construction**: canonical oracle observation format
- **RFC 8785 (JCS) Canonicalisation**: deterministic JSON serialisation
- **SHA-256**: the platform libcrypto (OpenSSL 3, EVP), behind
  `axilog_sha256()` and `axilog_commit()` in `substrate/`
- **UTF-8 + NFC Validation**: encoding canonicality enforcement
- **Line Ending Normalisation**: CRLF/CR to LF conversion
- **Ledger Sequence Ordering**: monotonic admission guard
- **Replay Identity**: bit-identical downstream behaviour

## Architecture

```
Oracle Output
     |
+---------------------------------+
|  ax_obs_admit()                 |
|  +-- Ordering guard (SHALL-038) |
|  +-- Size bound (SHALL-048)     |
|  +-- Encoding validation        |
|  +-- Line normalisation         |
|  +-- Input hash (SHA-256)       |
|  +-- obs_hash computation       |
+---------------------------------+
     |
AX:OBS:v1 Record
     |
L4 Policy Evaluation
```

## Building

### Requirements

- GCC or Clang (C99), CMake 3.16+
- OpenSSL 3 development files (`libssl-dev`) and libcurl (`libcurl4-openssl-dev`)
- Python 3 (tests and tools), shellcheck (one test)

### Build and test

```sh
./verify.sh                 # build, every test, reproducibility check
VERIFY_SANITIZE=1 ./verify.sh   # adds an ASan + UBSan pass
```

`verify.sh` builds in `./build-verify`. A plain CMake build also works:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Install

See [INSTALL.md](INSTALL.md). Stage with `cmake --install build --prefix <dir>`;
an existing installation is upgraded only by [UPGRADE.md](UPGRADE.md).

## Usage

```c
#include "axilog/obs.h"
#include "axilog/hash.h"

/* Prepare input from oracle */
ax_obs_input_t in;
memset(&in, 0, sizeof(in));
in.completion_state = AX_COMPLETION_COMPLETE;
in.failure_type = AX_FAILURE_NULL;
in.ledger_seq = 42;
in.oracle_id = "inference-provider-prod";
in.model_id = "model-2024-04-09";
in.input = "What is the meaning of life?";
in.input_len = 28;
in.output = "The answer is 42.";
in.output_len = 17;
in.params.max_tokens = 4096;
in.params.seed = AX_PARAMS_NULL_INT64;
in.params.temperature = 45875;  /* 0.7 in Q16.16 */
in.params.top_p = 58982;        /* 0.9 in Q16.16 */

/* Admit observation */
ax_obs_record_t obs;
char output_buf[4096];
ax_admission_ctx_t ctx;
ct_fault_flags_t faults;

ax_admission_ctx_init(&ctx);
ct_fault_clear(&faults);

int result = ax_obs_admit(&obs, output_buf, sizeof(output_buf), 
                          &in, &ctx, &faults);

if (result == AX_OK) {
    /* obs.obs_hash now contains the observation hash */
    /* obs.input_hash contains the input hash */
    /* Record is ready for L4 policy evaluation */
}
```

## API Reference

### Core Functions

| Function | Description |
|----------|-------------|
| `ax_obs_admit()` | Admit oracle output as AX:OBS:v1 |
| `ax_obs_validate()` | Validate observation record |
| `ax_obs_compute_hash()` | Compute obs_hash |
| `ax_compute_input_hash()` | Compute input_hash |

### Canonicalisation

| Function | Description |
|----------|-------------|
| `ax_obs_canonicalise()` | Canonicalise full record to JSON |
| `ax_params_canonicalise()` | Canonicalise params object |
| `ax_string_escape()` | Minimal JSON string escaping |

### Validation

| Function | Description |
|----------|-------------|
| `ax_validate_utf8()` | Validate UTF-8 encoding |
| `ax_normalise_line_endings()` | Convert CRLF/CR to LF |
| `ax_contains_forbidden_control()` | Check for control chars |

### Hashing

| Function | Description |
|----------|-------------|
| `ax_compute_input_hash()` | SHA-256 of the admitted input |
| `ax_obs_compute_hash()` | obs_hash over the canonical record |
| `axilog_sha256()`, `axilog_commit()` | SHA-256 and the tagged commitment (`substrate/`) |

## Test Suite

`ctest` runs the library suites (canonicalisation, hashing, encoding,
ordering, truncation, replay identity), the ledger and commitment tests, the
gateway unit and process tests (recovery, signals, client protocol, verifier,
file modes), the cross-architecture fingerprints and shellcheck: 53 entries.

## Conformance

See [CONFORMANCE.md](CONFORMANCE.md) for:
- Full requirement traceability matrix
- Test coverage report
- The project's own conformance statement (not a third-party certification)

**Status: SRS-004 v0.3 CONFORMANT (48/48 SHALL)**

## Constraints

The oracle library (`src/`) enforces the following. The gateway is an
ordinary service: it allocates, reads the clock for log lines and uses the
network.

- **Zero dynamic allocation**: All memory caller-provided
- **No floating point**: Integer and fixed-point only
- **No undefined behaviour**: C99 strict, UBSan clean
- **No time access**: Time via admitted Time Oracle only
- **Bit-identical results**: Cross-platform determinism

## License

Copyright (c) 2026 Spey Systems LTD

SPDX-License-Identifier: AGPL-3.0-or-later

## Related Documents

- SRS-004 v0.3: Oracle Boundary Gateway Contract
- SRS-001 v0.3: Axilog Substrate (L6)
- SRS-002 v0.3: Agent Totality (L5)
- SRS-003 v0.3: Policy Evaluation (L4)
- DVEC-001 v1.3: Deterministic Verification Contract

---

*axioma-oracle: A cryptographic containment boundary for non-determinism*

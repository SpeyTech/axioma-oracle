# axioma-oracle

**Axioma L3 — Oracle Boundary Gateway Layer**

A cryptographic containment boundary for non-deterministic systems.

## Overview

`axioma-oracle` implements the Oracle Boundary Gateway Contract defined in SRS-004 v0.3. It transforms non-deterministic oracle outputs (LLMs, ML models, external APIs) into immutable, canonical, ordered evidence before they can influence deterministic computation.

**Governing Principle:**

> LLMs are not trusted. They are recorded.
> Inference is not execution. Inference is evidence.

## Features

- **AX:OBS:v1 Record Construction** — Canonical oracle observation format
- **RFC 8785 (JCS) Canonicalisation** — Deterministic JSON serialisation
- **SHA-256 Hash Computation** — Pure C99 implementation
- **UTF-8 + NFC Validation** — Encoding canonicality enforcement
- **Line Ending Normalisation** — CRLF/CR → LF conversion
- **Ledger Sequence Ordering** — Monotonic admission guard
- **Replay Identity** — Bit-identical downstream behaviour

## Architecture

```
Oracle Output
     ↓
┌─────────────────────────────────┐
│  ax_obs_admit()                 │
│  ├── Ordering guard (SHALL-038) │
│  ├── Size bound (SHALL-048)     │
│  ├── Encoding validation        │
│  ├── Line normalisation         │
│  ├── Input hash (SHA-256)       │
│  └── obs_hash computation       │
└─────────────────────────────────┘
     ↓
AX:OBS:v1 Record
     ↓
L4 Policy Evaluation
```

## Building

### Requirements

- GCC 9+ or Clang 10+
- CMake 3.16+
- C99 standard library

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

Or run all tests verbosely:

```bash
ctest -V
```

### Build with Sanitizers

```bash
# UndefinedBehaviorSanitizer
cmake .. -DENABLE_UBSAN=ON
make

# AddressSanitizer  
cmake .. -DENABLE_ASAN=ON
make
```

### Install

```bash
cmake --install . --prefix /usr/local
```

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
in.oracle_id = "azure-openai-prod";
in.model_id = "gpt-4-turbo-2024-04-09";
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
| `ax_sha256()` | One-shot SHA-256 |
| `ax_sha256_init/update/final()` | Incremental SHA-256 |

## Test Suite

| Suite | Tests | Description |
|-------|-------|-------------|
| test_obs_canonical | 14 | JCS canonicalisation |
| test_obs_hash | 10 | SHA-256 and obs_hash |
| test_encoding | 21 | UTF-8 and control chars |
| test_ordering | 8 | Ledger sequence guard |
| test_truncation | 7 | Size bounds |
| test_replay_identity | 8 | Replay determinism |

**Total: 68 tests**

## Conformance

See [CONFORMANCE.md](CONFORMANCE.md) for:
- Full requirement traceability matrix
- Test coverage report
- Certification statement

**Status: SRS-004 v0.3 CONFORMANT (48/48 SHALL)**

## Constraints

This implementation enforces:

- **Zero dynamic allocation** — All memory caller-provided
- **No floating point** — Integer and fixed-point only
- **No undefined behaviour** — C99 strict, UBSan clean
- **No time access** — Time via admitted Time Oracle only
- **Bit-identical results** — Cross-platform determinism

## License

Copyright (c) 2026 The Murray Family Innovation Trust

SPDX-License-Identifier: GPL-3.0-or-later

Patent: UK GB2521625.0

## Related Documents

- SRS-004 v0.3 — Oracle Boundary Gateway Contract
- SRS-001 v0.3 — Axilog Substrate (L6)
- SRS-002 v0.3 — Agent Totality (L5)
- SRS-003 v0.3 — Policy Evaluation (L4)
- DVEC-001 v1.3 — Deterministic Verification Contract

---

*axioma-oracle — A cryptographic containment boundary for non-determinism*

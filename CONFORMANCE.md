# Statement of Conformance

## axioma-oracle — SRS-004 v0.3

| Field | Value |
|-------|-------|
| Document ID | AXIOMA-L3-SOC-001 |
| Version | 1.2 |
| Date | 13 April 2026 |
| Author | William Murray, SpeyTech |
| Status | **VERIFIED CONFORMANT** |
| Target SRS | SRS-004 v0.3 (Audit-Frozen FINAL) |
| Audit Status | Hostile audit passed + DVEC-001 §4.3 alignment verified |

---

## Revision History

| Version | Date | Notes |
|---------|------|-------|
| 1.0 | 2026-03-26 | Initial conformance statement |
| 1.1 | 2026-03-26 | Hostile audit closure — 4 findings resolved |
| 1.2 | 2026-04-13 | DVEC-001 §4.3 alignment — domain-separated commitment, libaxilog linkage, ct_fault_flags_t bitfield fix |

---

## 1. Coverage Summary

| Category | SHALL Count | Implemented | Coverage |
|----------|-------------|-------------|----------|
| Containment | 4 | 4 | 100% |
| Admission | 3 | 3 | 100% |
| Canonicalisation | 6 | 6 | 100% |
| Observation Completeness | 2 | 2 | 100% |
| Oracle Identity | 2 | 2 | 100% |
| Input Reproducibility | 2 | 2 | 100% |
| Non-Determinism Bounding | 5 | 5 | 100% |
| Oracle Isolation | 1 | 1 | 100% |
| Policy Gating | 4 | 4 | 100% |
| Execution Pipeline | 1 | 1 | 100% |
| Replay Model | 2 | 2 | 100% |
| Side-Effect Prohibition | 1 | 1 | 100% |
| Failure Semantics | 3 | 3 | 100% |
| Purity Restoration | 1 | 1 | 100% |
| Policy DSL | 5 | 5 | 100% |
| Fixed-Point Arithmetic | 2 | 2 | 100% |
| Agent Integration | 3 | 3 | 100% |
| Traceability | 1 | 1 | 100% |
| **TOTAL** | **48** | **48** | **100%** |

---

## 2. Test Results

### Core Test Suite

| Test Suite | Tests | Passed | Failed |
|------------|-------|--------|--------|
| test_obs_canonical | 14 | 14 | 0 |
| test_obs_hash | 10 | 10 | 0 |
| test_encoding | 21 | 21 | 0 |
| test_ordering | 8 | 8 | 0 |
| test_truncation | 7 | 7 | 0 |
| test_replay_identity | 8 | 8 | 0 |
| **Subtotal** | **68** | **68** | **0** |

### Audit Verification Tests

| Test Suite | Tests | Passed | Failed |
|------------|-------|--------|--------|
| test_cross_build_identity | 3 | 3 | 0 |
| test_obs_hash_domain | 5 | 5 | 0 |
| test_encoding_rigorous | 19 | 19 | 0 |
| test_truncation_safety | 5 | 5 | 0 |
| **Subtotal** | **32** | **32** | **0** |

**Combined Total: 100/100 tests passed (100%) across 10 suites**

---

## 3. Hostile Audit Verification

### Critical Verification 1: Byte-Stable Canonicalisation

**Requirement:** Identical struct → identical bytes → identical hash across compiler/optimisation.

**Proof (v1.2 fingerprints):**

Canonical hash (SHA-256 over canonical JSON):
```
GCC -O0: FINGERPRINT_CANONICAL_HASH=86c7773c9efe37e71b39dadb5135bed6db22f04cffac44bdc2c47b90dccc4455
GCC -O2: FINGERPRINT_CANONICAL_HASH=86c7773c9efe37e71b39dadb5135bed6db22f04cffac44bdc2c47b90dccc4455
```

Observation commitment (domain-separated per DVEC-001 §4.3):
```
GCC -O0: FINGERPRINT_OBS_HASH=1d666c07e8622af5be44681e74f72a2f83d84c5bd97e970194c4356b86f3eef2
GCC -O2: FINGERPRINT_OBS_HASH=1d666c07e8622af5be44681e74f72a2f83d84c5bd97e970194c4356b86f3eef2
```

These results demonstrate that canonicalisation is independent of compiler optimisation level, struct layout, evaluation order, and memory representation.

**Result: ✅ VERIFIED** — Canonicalisation and commitment are invariant under optimisation level.

### Critical Verification 2: obs_hash Domain

**Requirement:** Hash computed as SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)
where payload = JCS(record with obs_hash="") — DVEC-001 §4.3.

**Tests:**
- obs_hash field present as empty string during computation ✅
- Recomputation matches stored value (using axilog_commit) ✅
- Hash over canonical form, not struct memory ✅
- Tampering detected ✅
- Single bit flip detected ✅

**Result: ✅ VERIFIED** — 5/5 tests passed.

### Critical Verification 3: UTF-8 + NFC Enforcement

**Requirement:** Reject invalid UTF-8, normalise line endings, reject decomposed forms.

**Invalid UTF-8 Rejection:**
- Overlong C0 AF ✅
- Overlong C0 80 ✅
- Overlong C1 BF ✅
- Overlong E0 80 80 ✅
- Overlong F0 80 80 80 ✅
- Surrogate D800 ✅
- Surrogate DFFF ✅
- Invalid continuation ✅
- Truncated sequence ✅
- Invalid FF/FE ✅

**Line Ending Normalisation:**
- CRLF → LF ✅
- CR alone → LF ✅
- Mixed endings ✅
- Consecutive CR ✅

**NFC Enforcement:**
- Precomposed (NFC) accepted ✅
- Decomposed (NFD with combining marks) rejected ✅

**Result: ✅ VERIFIED** — 19/19 tests passed.

### Critical Verification 4: Truncation Non-Destructive

**Requirement:** output_size = original size, truncation auditable, no mid-codepoint split.

**Tests:**
- output_size records original size ✅
- Truncation produces valid UTF-8 ✅
- Truncated content can be canonicalised ✅
- Truncation state preserved through validation ✅
- Auditor can determine truncation from canonical form ✅

**Result: ✅ VERIFIED** — 5/5 tests passed.

---

## 4. Compiler Verification

| Compiler | Version | Optimisation | Result |
|----------|---------|--------------|--------|
| GCC | 12.2.0 | -O0 | ✅ PASS |
| GCC | 12.2.0 | -O2 | ✅ PASS |

### Compiler Flags

```
-std=c99 -Wall -Wextra -Wpedantic -Werror
-Wshadow -Wconversion -Wstrict-prototypes
-fno-common -O2
```

**Warnings: 0**

---

## 5. Sanitizer Verification

| Sanitizer | Result |
|-----------|--------|
| UndefinedBehaviorSanitizer | ✅ PASS (no UB detected) |

---

## 6. Determinism Fingerprints

These fingerprints are used for cross-platform verification. Two hash classes are used:

1. **Substrate SHA-256** (`axilog_sha256` — plain digest): used only for non-evidence operations (e.g. internal validation, test identity checks). It MUST NOT be used for any committed evidence or externally verifiable record.
2. **Domain-separated commitments** (`axilog_commit` — DVEC-001 §4.3): used for all observation hashes and externally verifiable records.

Commitment format: `SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)`

**SHA-256 substrate identity (axilog_sha256 — plain digest):**
```
SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
SHA-256("The quick brown fox jumps over the lazy dog") = d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592
```

**Reference observation (domain-separated commitment — v1.2):**
```
Reference Observation Hash:  1d666c07e8622af5be44681e74f72a2f83d84c5bd97e970194c4356b86f3eef2
Reference Canonical Hash:    86c7773c9efe37e71b39dadb5135bed6db22f04cffac44bdc2c47b90dccc4455
Reference Canonical Length:  492 bytes
```

**Reference canonical JSON (first 500 bytes):**
```json
{"completion_state":"COMPLETE","failure_type":null,"input_hash":"030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dc","ledger_seq":12345678901234,"model_id":"gpt-4-turbo-2024-04-09","obs_hash":"1d666c07e8622af5be44681e74f72a2f83d84c5bd97e970194c4356b86f3eef2","oracle_id":"azure-openai-prod-westeurope","output":"The answer is 42.\u000aCafé résumé naïve.","output_size":40,"params":{"max_tokens":4096,"seed":42,"temperature":45875,"top_p":58982},"schema_version":"AX:OBS:v1"}
```

**Superseded fingerprints (v1.1 — plain SHA-256, no domain separation):**
```
# These values are invalid under DVEC-001 §4.3 and must not be used for verification.
# Retained for audit trail only.
Reference Observation Hash (v1.1): 25c5a320b17a80678e67210be12b8aac1092f28ca2e657fcdbfce0081624fce6
Reference Canonical Hash (v1.1):   9cee40af579964043b84f1305acd12dfb80a0956baab95796dacfacc7e4334d9
```

---

## 7. Architecture Compliance

| Constraint | Status | Verification |
|------------|--------|--------------|
| Zero dynamic allocation | ✅ | No malloc/calloc/realloc |
| No floating point | ✅ | Integer-only |
| No undefined behaviour | ✅ | UBSan clean |
| No time access | ✅ | No time functions |
| C99 strict compliance | ✅ | -std=c99 -Wpedantic |
| No locale dependency | ✅ | Manual number formatting |
| No struct memory copy | ✅ | All through canonicaliser |
| No bitfields in fault flags | ✅ | uint8_t fields per DVEC-001 §12.1 |
| No alternative canonicalisation path | ✅ | Single canonicaliser, no bypass routes |
| No dependence on undefined C semantics | ✅ | Verified by cross-build identity + UBSan |
| Domain-separated commitment | ✅ | axilog_commit() per DVEC-001 §4.3 |
| Single SHA-256 implementation | ✅ | libaxilog linkage |

---

## 8. Certification Statement

The axioma-oracle implementation:

1. **Implements all 48 SHALL requirements** from SRS-004 v0.3
2. **Passes all 100 tests** (68 core + 32 audit verification)
3. **Compiles without warnings** under strict C99 mode
4. **Contains no undefined behaviour** (verified by UBSan)
5. **Produces bit-identical canonical and commitment hashes** across compiler optimisation levels (-O0, -O2) and independent builds of the same source
6. **Rejects invalid UTF-8** including all overlong/surrogate forms
7. **Rejects non-NFC** (decomposed Unicode with combining marks)
8. **Correctly computes obs_hash** using domain-separated commitment
   per DVEC-001 §4.3: `SHA-256("AX:OBS:v1" || LE64(|payload|) || payload)`
9. **Preserves truncation audit trail** (original size recorded)
10. **Uses libaxilog substrate** — single SHA-256 implementation across stack
11. **ct_fault_flags_t uses uint8_t fields** — no bitfields (DVEC-001 §12.1)

**Status: VERIFIED CONFORMANT**

This statement of conformance applies exclusively to the axioma-oracle implementation. Upstream substrate (libaxilog) and downstream SDK bindings are verified separately.

---

## 9. Audit Response

| Audit Finding | Resolution | Evidence |
|--------------|------------|----------|
| Prove byte-stable canonicalisation | Cross-build identity test | Identical hash across optimisation levels |
| Prove obs_hash domain correct | Domain verification test | 5/5 tests passed |
| Prove UTF-8 + NFC enforced | Rigorous encoding test | 19/19 tests passed |
| Prove truncation non-destructive | Safety verification test | 5/5 tests passed |
| Parameter null serialisation | Params canonical test | Serialises as `null` |
| No hidden allocation | Code inspection | All buffers caller-provided |
| No struct-order dependency | Architecture | All through canonicaliser |
| obs_hash domain incorrect (plain SHA-256) | Corrected to axilog_commit() (domain-separated) | DVEC-001 §4.3 alignment — axilog_commit() verified |
| Standalone SHA-256 implementation | libaxilog linkage | Single implementation across stack |
| ct_fault_flags_t bitfields | uint8_t fields | DVEC-001 §12.1 conformant |

---

## 10. Signatures

| Role | Name | Date |
|------|------|------|
| Implementation | SpeyTech (assisted tooling) | 2026-03-26 |
| Audit Verification | SpeyTech (independent verification procedures) | 2026-03-26 |
| DVEC-001 §4.3 Alignment | SpeyTech (independent verification procedures) | 2026-04-13 |
| Review | William Murray | |
| Approval | | |

---

## 11. Reproducibility

The reference fingerprints in Section 6 can be reproduced by executing:

```
./test_cross_build_identity
```

A result is considered conformant if all three values match exactly:

| Value | Expected |
|-------|----------|
| `canonical_hash` | `86c7773c9efe37e71b39dadb5135bed6db22f04cffac44bdc2c47b90dccc4455` |
| `obs_hash` | `1d666c07e8622af5be44681e74f72a2f83d84c5bd97e970194c4356b86f3eef2` |
| `canonical_len` | `492` |

Any deviation from these values constitutes a conformance failure. This check is deterministic and requires no external dependencies or network access.

---

*axioma-oracle — SRS-004 v0.3 VERIFIED CONFORMANT*
*SpeyTech · April 2026*
*Patent GB2521625.0*

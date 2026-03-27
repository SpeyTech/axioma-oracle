# axioma-oracle Architecture

## Layer Responsibilities

```
L7  axioma-governance   Proof-carrying policies
L6  axioma-audit        Cryptographic audit ledger
L5  axioma-agent        Behavioural FSM, defensive sequence checks
L4  axioma-policy       Policy evaluation, operational envelope
L3  axioma-oracle  ←    Oracle Boundary Gateway (this layer)
L2  certifiable-*       Deterministic ML ecosystem
L1  libaxilog           DVM substrate
```

---

## Ledger Sequence Authority (H3)

### Authoritative Layer: L3

The `ledger_seq` field provides a monotonically increasing ordering anchor
for all oracle observations. **L3 is the authoritative source** for sequence
ordering enforcement.

### Ordering Invariants

1. **Strictly Monotonic**: Each observation's `ledger_seq` must be greater
   than the previous observation's `ledger_seq`.

2. **No Duplicates**: Duplicate `ledger_seq` values are rejected with
   `AX_ERR_ORDERING`.

3. **No Regression**: Sequence values that go backward are rejected.

### Enforcement

```c
/* In ax_obs_admit() — SRS-004-SHALL-038 */
if (ctx->initialized && in->ledger_seq <= ctx->last_ledger_seq) {
    faults->ordering = 1;
    out->completion_state = AX_COMPLETION_ERROR;
    out->failure_type = AX_FAILURE_INVALID_OUTPUT;
    return AX_ERR_ORDERING;
}
```

### L5 Defensive Checks

L5 (axioma-agent) performs **defensive checks** on ledger sequences but
does not override L3 authority. L5 may:

- Verify sequences are in expected ranges
- Detect gaps in sequences
- Log anomalies for audit

However, L3 remains the **single source of truth** for sequence validity.

### Cross-Layer Contract

| Layer | Responsibility |
|-------|----------------|
| L3 | **Authoritative**: Rejects invalid sequences at admission |
| L5 | **Defensive**: Validates sequences received from L3 |
| L6 | **Audit**: Records all sequence decisions in ledger |

---

## Canonicalisation Authority (H2)

### Single Source of Truth

All observation canonicalisation flows through a single function:

```c
int ax_obs_canonicalise(
    char *dst,
    size_t dst_size,
    const ax_obs_record_t *obs,
    int include_hash
);
```

This ensures:

1. **Consistency**: All canonical forms are identical for identical inputs
2. **Determinism**: No variation based on code path
3. **Auditability**: Single point of verification

### Hash Computation

Hash computation uses canonicalisation with `include_hash=0`:

```c
/* obs_hash = "" during computation */
temp.obs_hash[0] = '\0';
len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &temp, 0);
ax_sha256(hash, (const uint8_t *)canonical_buf, len);
```

---

## Encoding Rejection (H4)

### Totality Requirement

All encoding validation occurs **before** hash computation. Invalid
encodings produce ERROR records, never abort.

### Validation Order

```
1. UTF-8 validation (SRS-004-SHALL-042)
2. Control character rejection (SRS-004-SHALL-044)
3. Line ending normalisation (SRS-004-SHALL-043)
4. Size limit check (SRS-004-SHALL-048)
5. Hash computation (SRS-004-SHALL-046)
```

### Fault Propagation

Invalid encodings set:
- `faults->encoding = 1`
- `obs->completion_state = AX_COMPLETION_ERROR`
- `obs->failure_type = AX_FAILURE_INVALID_OUTPUT`

This ensures the observation record is still valid (not aborted) but
marked as failed for downstream processing.

---

## Hash Self-Reference (H1)

### Edge Case: obs_hash Field

The `obs_hash` field is included in the canonical form but must be
**empty** during hash computation to avoid circular dependency:

```json
{
  "completion_state": "COMPLETE",
  ...
  "obs_hash": "",  /* Must be empty during hash computation */
  ...
}
```

After hash computation, the field is populated with the hex-encoded
SHA-256 hash of the canonical form.

### Verification

```c
/* Recomputation must match stored value */
temp = *obs;
temp.obs_hash[0] = '\0';
len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &temp, 0);
ax_sha256(recomputed, (const uint8_t *)canonical_buf, len);
ax_format_hash_hex(recomputed_hex, sizeof(recomputed_hex), recomputed, 32);
assert(strcmp(obs->obs_hash, recomputed_hex) == 0);
```

---

*axioma-oracle v0.1.0 — L3 Oracle Boundary Gateway*
*SpeyTech · March 2026*

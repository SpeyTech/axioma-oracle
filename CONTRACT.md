# axioma-gateway: supported contract

This is the whole of what the gateway promises. Anything not stated here is not
claimed. It describes the code on this branch. On `main` at `b37bb04`, two of
its rules did not hold; §8 lists them.

## 1. Scope

**What is supported:**
- one serial gateway process on one Linux host;
- the AF_UNIX request and response framing in `gateway/README.md`;
- one configured upstream messages API;
- single-turn prompts within the configured limits, including the per-run
  call budget.

**What is not supported:** HTTP compatibility, streaming, multiple providers,
concurrency, system prompts, multi-turn input and tools.

**The supported configuration** sets `max_attempts=1`, which is the default.
A larger value is accepted by the configuration loader, but it is outside
this contract (rule R-4).

## 2. Requests and evidence

**What counts as accepted.** A request is *accepted* when its write-ahead
intent is durable: written, fsynced and its directory fsynced. That happens
before any network I/O.

**What counts as refused.** A request refused before that point produces no
record and never reaches the upstream. The refusals are:
- a malformed request;
- a prompt that is not UTF-8, not NFC, or contains a control character;
- an exhausted call budget;
- a failure to write the intent;
- a request body that cannot be built.

**Outcomes of accepted requests:**

| Outcome | When | Record (AX:OBS:v1) |
|---|---|---|
| Completed | HTTP 200 with a parseable body | `COMPLETE`, or `TRUNCATED` when the stop reason is the token limit |
| Failed, response received | A non-200 status, a 200 body that does not parse, or output the record cannot admit | `ERROR` with `TRANSPORT_ERROR` (HTTP failure) or `INVALID_OUTPUT` |
| No response | Timeout or transport failure | `ERROR` with `TIMEOUT` or `TRANSPORT_ERROR` |
| Interrupted | A crash or internal fault between the intent and its record | `ERROR` with `TRANSPORT_ERROR`, written at the next successful start; its input is `unclosed-intent input_hash=<hex>` |

**R-1, accounting.** Every accepted request produces exactly one AX:OBS:v1
record, no later than the end of the next successful start.

**R-2, uncertainty.** `TIMEOUT` and `TRANSPORT_ERROR` mean that no valid
response reached the gateway. They do **not** mean the upstream did nothing:
it may have processed, and charged for, the request. The record never asserts
the upstream's outcome in these cases.

**R-3, Interrupted records.** These use the existing `TRANSPORT_ERROR` form.
Their `input_hash` is the hash of the synthetic input, not of the prompt.
AX:OBS:v1 has no "unknown" type, so the ledger alone cannot tell an
Interrupted record from a transport failure; the gateway log can.

**R-4, transmissions.** A record accounts for one accepted request.
- **Under the supported configuration,** an accepted request is transmitted
  upstream at most once. Requests are transmitted only after acceptance.
- **With `max_attempts` above 1,** up to that many transmissions, each after
  a 429 or 5xx response, map to one record. The attempt count then appears
  only in the unchained log.

**R-5, output admission.** SRS-004-SHALL-044 forbids control characters other
than LF in the output, and the record admits only NFC text. A response that
breaks either rule is recorded as `INVALID_OUTPUT` with empty output, and the
received text is not kept. Real responses containing a TAB or a CR, such as
code, fall into this class.

## 3. Write-ahead intent and recovery

**What the intent holds.** The intent is a single file holding the prompt's
`input_hash`, the request parameters, and `ledger_seq`: the sequence number
its record will occupy. It is cleared only after the record is durable.

**What happens at start.** If an intent is present, the gateway decides as
follows, before it writes anything else:

| Ledger state | Action |
|---|---|
| Frame `ledger_seq` is the last frame and is the AX:OBS:v1 record for this intent (same `ledger_seq`, and the prompt's `input_hash` or the Interrupted input's) | Clear the intent. Write nothing |
| The ledger ends at `ledger_seq − 1` | Write the Interrupted record, then clear the intent |
| Anything else: a different record at that position, the ledger shorter, or the ledger already past it | **Refuse to start** and leave the ledger and the intent untouched for the operator |
| The intent has no `ledger_seq` line (written by an earlier binary) | Write the Interrupted record (logged as legacy). This may duplicate a call that was already recorded; it never loses one |

**Torn tail.** A torn tail is truncated only when an open intent accounts for
it.

**Internal faults.** On an internal fault that leaves an intent open, the
gateway logs the fault and **exits non-zero**. The shipped systemd unit
restarts it, and the start-up procedure above records the call. A fault that
persists, such as a full disk, shows as a failing unit. The gateway never
serves on while hiding an open intent.

## 4. Commitment and history

**Commitment:** `commit = SHA-256(tag || LE64(len) || payload)`.

**Frame:** `u32le tag_len | tag | u64le payload_len | payload | commit[32]`.

**Chain:** `L0 = SHA-256("AX:LEDGER:v1" || commit("AX:STATE:v1", GENESIS))`,
then `Ln = SHA-256("AX:LEDGER:v1" || Ln-1 || commit_n)`. Chain values are
recomputed, never stored. Start-up replay recomputes every commit and refuses
to start on a mismatch.

**Record types:**
- **AX:STATE:v1**, written at every start, records:
  - `config_sha256`, the configuration file's digest;
  - `source_commit`, the axioma-oracle commit the binary was built from (§6).
- **AX:OBS:v1** is unchanged; its fingerprint is in `ci/fingerprints.txt`.

No Merkle construction is part of this system.

## 5. What the ledger proves, and what it does not

**The ledger alone establishes:**
- every frame matches its commit;
- the frames form one chain from genesis to a head.

**It does not establish:**
- that no trailing records were removed, because a cut at a frame boundary is
  undetectable;
- that the history was not rewritten consistently, because anyone who can
  write the file can recompute every commit;
- when anything happened. Records carry no time; order is sequence only.

**Checkpoints.** Every recorded reply carries `seq` and `chain_head`. A party
that keeps any such pair can later show that the first `seq` records are
unaltered, by recomputing to that position. Records after `seq` remain
unanchored. The export file is **not** a checkpoint: it is rewritten from the
primary ledger, on the same host, at every start.

**Verification.** `axioma-verify-ledger` (in `tools/`) recomputes a ledger
offline, from a copy, with or without a held `seq:chain_head` pair. Its
verdicts and exit statuses:

| Verdict | Meaning | Exit |
|---|---|---|
| `REJECTED` | Not a ledger: a bad tag or payload length, or an unregistered tag | 2 |
| `INCONSISTENT` | A frame does not match its commit, or the file ends inside a frame | 2 |
| `CONSISTENT_UNANCHORED` | Every frame matches and chains; nothing anchors it | 4 |
| `MATCHES_CHECKPOINT` | Frames 1 to `seq` reproduce the held head | 0 |
| `CHECKPOINT_MISMATCH` | The ledger is shorter than `seq`, or frame `seq` does not reproduce the held head | 2 |

Usage and I/O errors exit 1. The verifier is Python standard library only
and shares no code with the writer, so it is independently executable. It is
not independently developed.

## 6. Source identification (`source_commit`)

**How it is set.** The build records `git rev-parse HEAD` only when the
axioma-oracle checkout has no modified tracked files at build time. Otherwise,
including outside a git checkout, it records `unknown`.

**What it identifies.** The axioma-oracle source tree, which since the
consolidation includes the ledger and commitment code (`ledger/`,
`substrate/`).

**What it does not identify:**
- untracked files;
- the platform libraries linked in (libcrypto, libcurl, libc);
- the compiler and flags.

It is the builder's statement, not an attestation, and it lets a reader tell
which ledger segments were written by which build.

## 7. Confidentiality and threat model

**Confidentiality is not claimed:**
- outputs are stored in clear;
- `input_hash` is an unsalted SHA-256, so it reveals any prompt that can be
  guessed;
- the export is created world-readable (0644), by design, for a separate
  reader.

The ledger is created 0640 and the log 0640; an existing file keeps its
mode. Protect `ledger_path`, `wal_path`, `export_path` and the log with
directory permissions as well: for example, a directory owned by the service
user with mode 0750.

**Threat model:**
- **Clients** sending hostile input get a refusal.
- **A hostile upstream** is recorded, not trusted.
- **Crashes** are covered by §3.
- **Storage corruption inside a frame** is detected at start.
- **A host-privileged attacker** can rewrite, truncate or replace the ledger,
  intent and export, and read the API key. Only a checkpoint held off the
  host detects this. This contract does not defend against such an attacker.

## 8. Status

On `main` at `b37bb04`:
- **R-1 did not hold** (B-02). Recovery treated an intent as already recorded
  whenever the last record carried the same prompt hash, so a repeated
  prompt interrupted in flight disappeared from the history. Ledgers written
  by earlier binaries may lack such calls, and they cannot be reconstructed.
- **§3, internal faults, did not hold.** After an internal fault the gateway
  kept accepting connections, but refused every request until it was
  restarted.

AX:STATE:v1 records written before this change carry no `source_commit`.

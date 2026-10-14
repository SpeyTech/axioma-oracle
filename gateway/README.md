# axioma-gateway

The L3 service layer. Wraps the axioma-oracle library around one
upstream messages API so that every accepted request, successful or
not, yields exactly one validated AX:OBS:v1 record on a durable L6
evidence chain. The oracle is recorded, not trusted; the wrapper is
trusted, and it is deterministic C.

What the gateway promises, and what it does not, is stated in
`CONTRACT.md` at the repository root. Where this README and the contract
differ, the contract applies.

The library is not modified. The gateway is net-new code under
`gateway/`, built by the `ORACLE_BUILD_GATEWAY` option in the
repository CMakeLists.

## Build (on axioma)

    cd ~/axilog/axioma-oracle
    cmake -B build -DAXILOG_SDK_ROOT=$HOME/axilog/sdk-root \
          -DAXIOMA_AUDIT_ROOT=$HOME/axilog/axioma-audit
    cmake --build build
    ctest --test-dir build

Requires libcurl development headers (`apt install libcurl4-openssl-dev`)
and an axioma-audit checkout (default: sibling directory).

## Run

    AX_ORACLE_ANTHROPIC_KEY=... ./build/axioma-gateway gateway.conf

or install the systemd unit (`gateway/axioma-gateway.service`), which
holds the key in a root-owned 0600 EnvironmentFile.

## Request protocol (Unix socket, line-oriented)

Request: header lines, a blank line, then the prompt bytes.

    max_tokens: 64
    temperature_q16: 0
    prompt_len: 21

    What is a monad, aye?

`temperature_q16` and `top_p_q16` are Q16.16 integers (65536 = 1.0).
`temperature_q16` is required: a request without it is refused with
`reason: temperature_required`. An omitted `top_p_q16` is recorded as
null and not sent. `seed` is rejected: the upstream API carries no seed
parameter, and recording a parameter that was not sent would be false
evidence.

Response: header lines, a blank line, then the output bytes.

    status: ok
    completion_state: COMPLETE
    failure_type: NULL
    obs_hash: <64 hex>
    chain_head: <64 hex>
    seq: 4
    snapshot_id: <configured model_id>
    temperature_q16: 0
    output_len: 49

Failures return `status: error` with the failure taxonomy in
`failure_type` (TIMEOUT, INVALID_OUTPUT, TRANSPORT_ERROR); the
observation is on the chain either way. TIMEOUT and TRANSPORT_ERROR
mean no valid response reached the gateway, not that the upstream did
nothing (CONTRACT.md R-2). Output containing a control character other
than LF (for example a TAB), or text that is not NFC, is recorded as
INVALID_OUTPUT with empty output (R-5). Pre-spend rejections
(encoding, budget, protocol) return `status: error` with a `reason`
line, produce no observation and send nothing upstream.

Upstream request body: the gateway sends the oracle API one JSON object
with the members `model`, `max_tokens`, optional `temperature` and
`top_p`, and `messages`. The configured `model_id` and the normalised
prompt are JSON-escaped (`"` and `\` backslash-escaped, `\n` `\r` `\t`
in short form, other characters below 0x20 as `\u00XX`, well-formed
UTF-8 copied unchanged), so no configured or supplied string can end a
string early or add members. A string that is not well-formed UTF-8 is
not sent: the request fails with `reason: internal` before any network
I/O. Escaping applies to the HTTP request only; the committed record
carries the unescaped values as before.

### Response framing (normative for clients)

- The header is a sequence of lines, each terminated by a single LF
  byte (0x0A). There is no CR. An empty line (LF LF) ends the header;
  exactly `output_len` bytes of output follow it.
- Each header line is `key`, then the two bytes `": "`, then the value.
  The value is every byte after `": "` up to, not including, the LF. It
  is not quoted, escaped or trimmed. Split on the first `": "` only: a
  value may itself contain `:`.
- A served request sends these keys in this order: `status`,
  `completion_state`, `failure_type`, `obs_hash`, `chain_head`, `seq`,
  `snapshot_id`, `temperature_q16`, `output_len`. A pre-spend refusal
  sends `status` and `reason` only. Ignore keys you do not know.
- Values are UTF-8. `snapshot_id` is the configured `model_id`
  byte for byte, the value committed as `model_id` in the
  `AX:OBS:v1` record. The configuration check at startup guarantees it
  is non-empty, valid UTF-8, at most 255 bytes, and holds no C0
  control character or DEL, so it never contains LF or CR. It may
  contain spaces, including trailing ones (the configuration parser
  trims only leading blanks), and any other Unicode character,
  including U+0085, U+2028 and U+2029.
- `temperature_q16` is the temperature the gateway served, as a Q16.16
  integer, or `null` when the request left it unpinned.

A client must therefore split the header on the byte LF only, decode
values as strict UTF-8, and keep values exactly as received. Splitting
on Unicode line boundaries (for example Python `str.splitlines()`),
trimming values, or decoding with replacement can change
`snapshot_id`; do not do any of them.

## Evidence and durability

- `ledger_path` is the append-only evidence file: framed records
  (tag, payload, commit). Startup replays it from genesis through
  axioma-audit, recomputing every commit and the chain head; a frame
  that does not match its commit refuses service. Chain links are not
  stored in the file, so replay alone detects altered frames but not a
  removed tail or a consistently rewritten history: those need a
  `(seq, chain_head)` pair held elsewhere, such as one kept from a
  reply (CONTRACT.md section 5). The file persists across restarts and
  is never truncated except for a torn tail under an open WAL intent.
- `wal_path` is the durable intent: written and fsynced before any
  network I/O, cleared after the observation is on the chain. It
  records `ledger_seq`, the sequence its observation will occupy. At
  start, an unclosed intent whose record is already at that sequence is
  cleared; one whose record is absent is recorded as a TRANSPORT_ERROR
  observation (the Interrupted record); any other ledger state refuses
  start. An intent from an earlier binary, without `ledger_seq`, is
  always recorded (CONTRACT.md section 3). Before this rule, a repeated
  prompt interrupted in flight could be lost (B-02).
- After an internal fault that leaves an intent open, the gateway exits
  non-zero so the unit restarts it and start-up records the call.
- `max_attempts` defaults to 1, the supported configuration: at most
  one upstream transmission per record (CONTRACT.md R-4).
- AX:STATE:v1 is committed at every start with the config file's
  SHA-256 and `source_commit`, the axioma-oracle commit the binary was
  built from, or `unknown` when that cannot be established (CONTRACT.md
  section 6).

## Log

JSON lines at `log_path`. The `message` field is upstream-verbatim
(curl error strings, provider error messages); the API key is
scrubbed at the formatting layer. This log is the harvest source for
the EXP-1 error-string corpus.

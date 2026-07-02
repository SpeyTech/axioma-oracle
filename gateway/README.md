# axioma-gateway

The L3 service layer. Wraps the axioma-oracle library around the
Anthropic Messages API so that every oracle invocation, successful or
not, becomes a validated AX:OBS:v1 record appended to a durable L6
evidence chain. The oracle is recorded, not trusted; the wrapper is
trusted, and it is deterministic C.

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

`temperature_q16` and `top_p_q16` are Q16.16 integers (65536 = 1.0);
omitted params are recorded as null and not sent. `seed` is rejected:
the Messages API carries no seed parameter, and recording a parameter
that was not sent would be false evidence.

Response: header lines, a blank line, then the output bytes.

    status: ok
    completion_state: COMPLETE
    failure_type: NULL
    obs_hash: <64 hex>
    chain_head: <64 hex>
    seq: 4
    output_len: 49

Failures return `status: error` with the failure taxonomy in
`failure_type` (TIMEOUT, INVALID_OUTPUT, TRANSPORT_ERROR); the
observation is on the chain either way. Pre-spend rejections
(encoding, budget, protocol) return `status: error` with a `reason`
line and produce no observation.

## Evidence and durability

- `ledger_path` is the append-only evidence file: framed records
  (tag, payload, commit). Startup replays it from genesis through
  axioma-audit, recomputing every commit and link; divergence refuses
  service. The file persists across restarts and is never truncated
  except for a torn tail under an open WAL intent.
- `wal_path` is the durable intent: written and fsynced before any
  network I/O, cleared after the observation is on the chain. An
  unclosed intent found at startup is replayed into a TRANSPORT_ERROR
  observation.
- The config file's SHA-256 is committed as AX:STATE:v1 at every start.

## Log

JSON lines at `log_path`. The `message` field is upstream-verbatim
(curl error strings, Anthropic error messages); the API key is
scrubbed at the formatting layer. This log is the harvest source for
the EXP-1 error-string corpus.

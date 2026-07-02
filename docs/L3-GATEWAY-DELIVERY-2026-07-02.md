# L3 Gateway Service Wrapper: Delivery Report

Date: 2 July 2026
Engine: Claude (container build, x86_64, GCC 13.3.0)
Deployed: axioma (ARM64, GCC 12.2.0), 2 July 2026
Brief: L3-GATEWAY-WRAPPER-BRIEF-2026-07-02-revB

## What was built

The gateway service under axioma-oracle/gateway/: a serial Unix-socket
service that wraps the unmodified axioma-oracle library around the
Anthropic Messages API and appends every post-spend outcome to a
durable L6 evidence chain via axioma-audit.

Components: gw_config (fail-closed key=value loader, raw bytes
retained), gw_log (JSON lines, upstream-verbatim messages, key
scrubbed at the formatting layer, fsync per line), gw_json (strict
purpose-built parser for exactly the Messages API shapes; structural
surprises map to INVALID_OUTPUT), gw_ledger (the durable chain seam),
gw_wal (single-file durable intent), gw_client (libcurl, retry
contract as ruled), gw_main (startup sequence and the ten-step request
path), gw-client.py (reference client).

## Test results

16/16 on both architectures: the 12 oracle library tests unchanged,
plus test_gw_json, test_gw_ledger, test_gw_wal, test_gw_fixture. The
fixture is the first production capture (Haiku-class shakedown call,
obs seq 5): 694 raw bytes, 675 canonical bytes, obs_hash
974c53ff9d238999174a286498442d53e113d7523ae6b8cdde08e9a52397e856,
bit-identical x86_64/aarch64, pinned in the test.

## Acceptance on axioma (production chain)

Full loop (COMPLETE, seq 5), live 401 with verbatim upstream error
(container shakedown), restart replay at every service start,
TRANSPORT_ERROR (seq 8), TIMEOUT (seq 13), TRUNCATED via stop_reason
max_tokens (seq 15, unplanned but welcome), INVALID_OUTPUT via
response cap (seq 18), WAL crash recovery (seq 20), spend guard
refusal (log-only, no observation). Config hash committed as
AX:STATE:v1 at every start; the induced-failure experiment is itself
recorded on the chain as a sequence of config commitments.

## Finding for the register: ct_fault_flags_t ABI collision (estate-level)

axioma-oracle's include/axilog/types.h defines its own
ct_fault_flags_t: 16 bytes, ten flags, domain at offset 3, encoding at
offset 5, under the same include guard (AXILOG_TYPES_H) and the same
path (axilog/types.h) as the substrate's type, which is 8 bytes with
domain at offset 5 (axioma-l0's l0_errors.h asserts the 8-byte size).

Because the guard suppresses the second inclusion, oracle TUs see only
the 16-byte type, yet oracle/src/hash.c passes a pointer to it into
axilog_commit in libaxilog, which was compiled against the 8-byte
layout. On a commit fault, the substrate writes domain at its offset 5,
which lands in the oracle struct's encoding field.

Severity, honestly qualified: fault misattribution only. ct_fault_any
ORs every byte, so the failure is still detected and the fail-closed
paths still close. The substrate writes within the caller's (larger)
struct, so there is no out-of-bounds access. The green path writes
nothing, so all passing tests are unaffected. The fix is a library
change (oracle should include the substrate's types.h and define its
richer L3 fault set under its own name). Recorded as E-ABI-1 in
CONFORMANCE.md §12 (close-out session); default scheduling, pending
Principal override, is after gateway shakedown and before EXP-1
stage 3.

The gateway mitigates by construction: strict TU partitioning. Group B
(gw_ledger.c plus audit's ledger.c) compiles against audit and
substrate headers only; group A (everything else) against oracle
headers only; the seam (gw_ledger.h) passes bare bytes and no axilog
type crosses it.

## The four rulings, as implemented

1. Non-NFC or otherwise unadmittable COMPLETE output degrades to an
   INVALID_OUTPUT observation with empty output; the rejection is the
   recorded fact.
2. Budget refusal is pre-spend and log-only; no observation.
3. stop_reason max_tokens maps to AX_COMPLETION_TRUNCATED (proven
   live at seq 15).
4. The WAL intent branch is the design: durable intent before network
   I/O, unclosed intent replayed to TRANSPORT_ERROR at startup, stale
   intent detected by input_hash presence in the last chain payload.

One brief line resolved against schema: the config hash is not in the
observation record (no field exists); it is committed to the chain as
AX:STATE:v1 at every service start.

## Ledger durability model

L6 provides chain arithmetic in memory only, so the gateway owns the
durable form: an append-only evidence file of framed records (u32le
tag_len, tag, u64le payload_len, payload, 32-byte commit). Startup
replays from genesis, recomputing every commit and every link;
divergence refuses service. Torn tail is truncated only under an open
WAL intent; a torn tail with no intent refuses to start. Append order:
frame written and fsynced before the in-memory chain extends. The
evidence file and log live in /var/lib/axioma-gateway and belong in
the backup set, not the repo.

## Errata

E1 (fixed in-tree, first commit): signal handlers were installed with
signal(), which on glibc sets SA_RESTART; SIGTERM's handler ran but
accept() restarted and the stop flag was never rechecked, so the
service ignored systemd stops until the 90-second SIGKILL. Fixed with
sigaction and sa_flags=0. Found by deployment, not by the 16 tests:
nothing in the suite exercises signal delivery against a blocking
accept. The kill test would have masked it later for the wrong reason.

E2 (fixed in-tree, close-out session): Type=simple means systemctl
returns at fork, but the socket binds after ledger replay and
recovery. The readiness gap raced three separate times during
acceptance (client connect, socket probe, log grep). Fixed with
Type=notify and a hand-rolled sd_notify (gw_notify.c, one READY=1
datagram to $NOTIFY_SOCKET after bind+listen); no libsystemd
dependency. Deployment acceptance is tools/check_readiness.sh, a
50-iteration restart-and-connect loop requiring zero refusals.

The E1 test gap is also closed: gateway/tests/test_gw_signals.sh
(in ctest as test_gw_signals) exercises SIGTERM against a blocking
accept and SIGKILL mid-request against a local mute endpoint, and
proves the WAL intent replays as a failure observation on the next
start. A shellcheck gate over the repo's shell tooling
(test_shellcheck) joins the suite in both this repo and axioma-l0;
E1 and the anchor guard were the same lesson twice in one day.

## Post-delivery state

The corpus harvest (session item 3) is unblocked: the production log
carries five distinct verbatim upstream strings toward the
eight-string done-criterion.

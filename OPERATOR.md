# axioma-gateway operator guide

What the gateway promises is [CONTRACT.md](CONTRACT.md). This guide is how to
run it. Paths are the shipped defaults.

## 1. What it does

One process, one socket (`/var/lib/axioma-gateway/gw.sock`), one upstream. A
client sends a prompt; the gateway writes a durable intent, calls the upstream
once, appends exactly one AX:OBS:v1 record to the ledger, clears the intent and
replies with the record's `seq` and `chain_head`. A request refused before the
intent (bad encoding, budget exhausted, malformed) gets an error reply and no
record. Every start appends one AX:STATE:v1 record naming the configuration
digest and the build's `source_commit`.

| File | Mode | Holds |
|---|---|---|
| `l6-evidence.bin` | 0640 | The ledger. Append-only. Never edit, truncate or replace it |
| `wal.open` | 0640 | The open intent, present only while a call is in flight |
| `gw.log` | 0640 | JSON lines: faults, refusals, recovery. Not chained |
| export file | 0644 | Optional mirror for a separate reader; rewritten from the ledger at every start |

## 2. The demonstration

`sh tools/demo.sh` (ordinary user, no network) runs every behaviour below
against a local stand-in in a temporary directory: a refused request, each
recorded outcome, offline verification with a checkpoint, a one-byte edit and
a cut history being detected, a kill mid-request recovered at restart, a full
disk stopping the gateway instead of serving on, and the STATE record naming
the build.

## 3. Sending a request

```sh
axioma-client /var/lib/axioma-gateway/gw.sock "your prompt" [temperature_q16]
```

The reply's `seq` and `chain_head` are a checkpoint (section 4). Each request
is one paid upstream call; `call_budget` caps the number per service run.

## 4. Checkpoints and verification

Verify a copy, never the live file, and preferably on another host:

```sh
sudo cat /var/lib/axioma-gateway/l6-evidence.bin > l6.bin
axioma-verify-ledger l6.bin                      # self-consistency only
axioma-verify-ledger --checkpoint N:H l6.bin     # against a held checkpoint
axioma-verify-ledger --list l6.bin               # one line per record
```

`MATCHES_CHECKPOINT` (exit 0) is the answer that means something: the first N
records are exactly those that existed when H was taken. `CONSISTENT_UNANCHORED`
(exit 4) only says the file is well formed: a truncated or rewritten ledger
gets it too. Anything else (exit 2) is a finding. `INCONSISTENT` with "torn
frame" on a copy taken while the gateway was writing: copy again.

Keep checkpoints off this host, with the date taken. A checkpoint that lives
only beside the ledger proves nothing against anyone who can write the ledger.

## 5. Routine checks

```sh
systemctl status axioma-gateway
sudo ls -l /var/lib/axioma-gateway
sudo grep '"level":"error"' /var/lib/axioma-gateway/gw.log | tail
```

`wal.open` present while the service is idle means an intent is open: see
section 6.

## 6. Failure handling

| Symptom (log `message`) | Meaning | Action |
|---|---|---|
| `wal_recovered` at start | A call was interrupted (crash, kill, fault); its Interrupted record was written, or found already written | None. It is logged for the record. The upstream may still have processed and charged for it (R-2) |
| `legacy intent without ledger_seq` | An intent left by a pre-upgrade binary was recorded as Interrupted | None. It may duplicate a recorded call; it never loses one |
| Unit failing; `ledger append failed` or `open intent after an internal fault` | Storage fault (full disk, I/O error). The gateway stopped rather than serve with an open intent | Fix the storage. The next start records the call. Do not remove `wal.open` |
| `refusing to start` (ledger and intent disagree) | The ledger is not where the intent says it should be: something else changed the ledger | Stop. Copy the ledger, the intent and the log aside, verify the copy against your last checkpoint, and decide before touching anything. Do not delete the intent to make it start |
| `corrupt WAL intent; refusing to start` | The intent file is partial or malformed | Inspect the file. If the log shows `WAL intent write failed` just before, the request was refused before any call: move the file aside and start. Otherwise treat it as the row above |
| `ledger open/replay failed` | The ledger cannot be opened read-write, or a frame does not match its commit | Check ownership (`axgw`) and mode (0640); then verify a copy. A commit mismatch is a finding, not a repair job |
| `ledger export enable failed` | The export path cannot be written | Check the export directory exists, is owned by `axgw`, and is listed in the unit's `ReadWritePaths` |
| `budget_refused` | `call_budget` reached for this run | Intended. Restart resets the count; raise the budget deliberately |
| Record `INVALID_OUTPUT` with empty output | The response contained a TAB, CR or non-NFC text (R-5) | None. The text is not kept, by specification |

## 7. What not to do

Do not edit, truncate, rotate or replace `l6-evidence.bin`, and do not delete
`wal.open` to make the service start. Do not set `max_attempts` above 1: extra
transmissions would not appear in the record. Do not restore the ledger from a
backup over a longer live ledger: that deletes history. Upgrades follow
[UPGRADE.md](UPGRADE.md).

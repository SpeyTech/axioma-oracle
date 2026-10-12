# tools

## obs_v1_loss_check.py: historical AX:OBS:v1 record classifier

Read-only. It classifies stored `AX:OBS:v1` records and never repairs, rewrites or relabels them. The full classification rules are in the header of the script.

    python3 -I tools/obs_v1_loss_check.py --frames LEDGER_FILE
    python3 -I tools/obs_v1_loss_check.py --jsonl  RECORDS_FILE

Output is one `<seq> <CLASS>` line per record, then one summary line:

    records=N defects=D loss_or_corruption=L schema_anomaly=S

| Exit status | Meaning |
|---|---|
| 0 | no `V1_` finding |
| 3 | at least one `V1_` finding, of either category |
| 1 | usage error, unreadable file or torn frame |

### Behaviour change: forbidden-pair diagnostic (2026-10-12)

**Read this before upgrading any automation that runs this tool.**

| What changed | Before | After |
|---|---|---|
| A record whose (`completion_state`, `failure_type`) pair is not permitted, e.g. `TRUNCATED`/`TIMEOUT`, written before PCR-004 residual (a) | `OK` | `V1_FORBIDDEN_STATE_PAIR` |
| Exit status of a ledger whose only defects are such records | **0** | **3** |
| Summary line | `records=N defects=D` | `records=N defects=D loss_or_corruption=L schema_anomaly=S` (the old text is an unchanged prefix) |
| Label of a record with an empty identifier and an unknown enum | `V1_UNKNOWN_ENUM` | `V1_IDENTIFIER_UNVERIFIABLE` (loss findings are listed before schema anomalies); the record still counts in both categories |
| `defects=` for a ledger with forbidden-pair records | excludes them | includes them |

Unchanged:
- the meaning of exit statuses 0, 3 and 1;
- every other classification;
- the `--frames` and `--jsonl` inputs;
- records with no finding.

The stored records, their `obs_hash` and the `ax_obs_validate` verdict are never affected: the tool only reads.

**Known consumers** (checked across the axioma repositories and `exp1-runner` on 2026-10-12):
- The CI step "Historical-record loss checker smoke test" in `.github/workflows/stage1-ci.yml` runs the tool on empty input and expects exit 0. That behaviour is unchanged.
- No other script, workflow or repository invokes the tool or reads its output.

`tests/test_obs_v1_loss_check_cli.py` runs the tool as that consumer does and pins every exit status and the summary format above. The unit-level classification tests are in `tests/test_obs_v1_loss_check.py`.

**Operator action.** If a cron job, monitoring check or report script treats a non-zero exit as an alert, the first run after upgrade raises one for any ledger that holds historical forbidden-pair records. That is the intended diagnostic. Review the listed records; do not edit them (SRS-004-AMD-001).

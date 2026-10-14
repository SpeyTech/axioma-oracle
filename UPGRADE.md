# Upgrading an existing axioma-gateway installation

This procedure upgrades the installation on `axioma` (installed 5 July 2026)
to a build of this branch. It continues the existing evidence chain: the
ledger is never reset, truncated, rewritten or replaced, and the pre-upgrade
bytes remain a byte-identical prefix. It is run by the owner. Part A changes
nothing on the host's service; Part B (the production upgrade) needs separate
owner approval (A6) after Part A has passed and this text has been reviewed.

Values recorded for this host on 14 October 2026:

| Item | Value |
|---|---|
| Installed binary | `/usr/local/bin/axioma-gateway`, 79,104 bytes, sha256 `98539ff35262853c2da821c9dffb39a7e60a6c9d331119c4a39be12766c30ac4` |
| Configuration | `/etc/axioma-gateway.conf`, sha256 `b3ded76a9ac8f00cf6a3852aa8b2e1080518bc11967de7af5aaf2bb946231567`; `max_attempts` not set (the installed binary defaults to 3) |
| Ledger snapshot | 97,166 bytes, sha256 `678c72442bcccaf5f06cdcbed019731129b14f02623ad7b0050053aacf61d452` |
| Snapshot checkpoint | `214:c84aacce8138339317aed9025236a484c776f8413fd75db7611f79dd91f11558` |

## What changes

| Item | Change |
|---|---|
| Binary | Replaced by the reviewed build. The old binary is kept for rollback |
| Configuration | One line added: `max_attempts=1`. The new binary defaults to 1 anyway; the line makes it explicit and survives a rollback, where the old default would be 3. The new digest is recorded in the next AX:STATE:v1 |
| `gw.log` | Mode 0644 to 0640 (new files are created 0640; an existing file keeps its mode) |
| Ledger | Nothing. The new binary appends one AX:STATE:v1 record at start, carrying `source_commit` |
| Export file | Rewritten from the ledger at start, as the installed binary already does |
| Unit, environment file | Nothing. The installed unit already lists the export directory |

Behaviour changes for clients: one upstream transmission per request; a
repeated prompt interrupted in flight is now recorded (B-02); after an
internal fault the gateway exits for systemd to restart it rather than refuse
requests until restarted.

## Part A: dry run on axioma (no production change)

As your ordinary user, not root. This reads `/etc/axioma-gateway.conf` and
executes the installed binary against copies only; it does not use the
production socket, ledger, intent, log or environment file.

```sh
mkdir -p ~/e5 && cd ~/e5
git clone https://github.com/SpeyTech/axioma-oracle.git
cd axioma-oracle
git checkout <reviewed commit>
./verify.sh 2>&1 | tee ~/e5/verify.log          # must end VERIFY PASSED
sh tools/demo.sh 2>&1 | tee ~/e5/demo.log
sha256sum /usr/local/bin/axioma-gateway          # must be 98539ff3...
tools/dry-run-upgrade.sh --snapshot ~/l6-snapshot-2026-10-14.bin \
    2>&1 | tee ~/e5/dry-run.log                  # must end DRY RUN PASSED
sha256sum /usr/local/bin/axioma-gateway /etc/axioma-gateway.conf
systemctl status axioma-gateway --no-pager       # unchanged, still running
```

Point `--snapshot` at your copy of the 14 October snapshot. The dry run
replays it with the new binary, writes new records, kills the gateway
mid-request and restarts it, then checks the original bytes are a prefix, the
pre-upgrade checkpoint still matches and the export equals the ledger. Its
rollback step starts the installed binary on the upgraded copy.

Send the three logs for review. Part B waits for A6.

**E5 record.** Part A was run on `axioma` on 14 October 2026 from the
verified archive `axioma-oracle-rc-024ca38` (unpacked to
`~/e5/axioma-oracle-rc-024ca38/axioma-oracle`) rather than a clone. All three
stages passed, and the build there is gateway sha256
`1f8931c2a2487599938da5cf639b6170f0e14e45ce470069a92575082f27921c`. Part B
installs that build.

## Part B: production upgrade (only after A6)

Run as your ordinary user; `sudo` asks for your password. Allow twenty
minutes at a time when you are well and unhurried, and when no client needs
the gateway. Nothing in Part B is time-critical: you can abort at any step,
and until B4 aborting only means starting the unchanged old service.

**Use tmux** so that a dropped connection does not kill a running command:
`tmux new -s a6` to start, `tmux attach -t a6` to return after a disconnect.

### Settings (paste first, and again after any reconnect)

```sh
umask 077
R=~/e5/axioma-oracle-rc-024ca38/axioma-oracle
B=/root/axioma-a6-backup
G=/var/lib/axioma-gateway
v() { python3 -I "$R/tools/axioma-verify-ledger" "$@"; }
mkdir -p ~/e5/a6
```

`R` is the build verified in Part A. Every other path is fixed, and every
result Part B depends on is kept in a file under `~/e5/a6` or `$B`, so
nothing is lost if the terminal disconnects.

### Where am I? (safe at any time; changes nothing)

```sh
systemctl is-active axioma-gateway
sha256sum /usr/local/bin/axioma-gateway | cut -c1-8
sudo test -e $B/SHA256SUMS && echo "backup: complete" || echo "backup: not complete"
ls ~/e5/a6
sudo test -e $G/wal.open && echo "intent: PRESENT" || echo "intent: none"
```

| Service | Binary | Backup | Files in `~/e5/a6` | Resume at |
|---|---|---|---|---|
| active | `98539ff3` | not complete | any | P (nothing has changed yet) |
| inactive | `98539ff3` | not complete | any | B2 |
| inactive | `98539ff3` | complete | no `pre-checkpoint.txt` | B3 |
| inactive | `98539ff3` | complete | `pre-checkpoint.txt` | B4 |
| inactive | `1f8931c2` | complete | `pre-checkpoint.txt` | B4 again (it is safe to repeat), then B5 |
| active | `1f8931c2` | complete | `pre-checkpoint.txt` | B5 |
| active | `98539ff3` | complete | any | An earlier abort or a rollback. Stop and review; to try again, use the retry step in B3 |

### P. Pre-flight (service still running; changes nothing)

```sh
sha256sum $R/build-verify/axioma-gateway /usr/local/bin/axioma-gateway /etc/axioma-gateway.conf /etc/systemd/system/axioma-gateway.service
systemctl show -p FragmentPath -p DropInPaths -p ExecStart axioma-gateway
systemctl is-active axioma-gateway
sudo test -e $B && echo "STOP: $B exists; use Where am I?" || echo "backup dir free"
sudo cat $G/l6-evidence.bin > ~/e5/a6/live.bin
v --checkpoint 214:c84aacce8138339317aed9025236a484c776f8413fd75db7611f79dd91f11558 ~/e5/a6/live.bin | tail -n 1
```

**Go only if all of these hold:**

| Check | Expected |
|---|---|
| New build | `1f8931c2a2487599938da5cf639b6170f0e14e45ce470069a92575082f27921c` |
| Installed binary | `98539ff35262853c2da821c9dffb39a7e60a6c9d331119c4a39be12766c30ac4` |
| Configuration | `b3ded76a9ac8f00cf6a3852aa8b2e1080518bc11967de7af5aaf2bb946231567` |
| Unit | `1beba2ef27f6ed09c6f50ce0f7ca9583e15ef8238dacca95301958db45b3aa25` |
| Unit location | `FragmentPath=/etc/systemd/system/axioma-gateway.service`, and `DropInPaths=` empty |
| `ExecStart` | Names `/usr/local/bin/axioma-gateway` |
| Service | `active` |
| Backup directory | `backup dir free` |
| Ledger | `MATCHES_CHECKPOINT: frames 1..214 …` |

A `torn frame` result only means the copy caught a write in progress: copy
again. **Any other difference: stop.** Nothing has been touched.

### B1. Stop, only when idle

```sh
sudo test ! -e $G/wal.open && sudo systemctl stop axioma-gateway; systemctl is-active axioma-gateway
sudo test -e $G/wal.open && echo "INTENT PRESENT" || echo "no intent"
```

The first line stops the service only if no call is in flight at that
moment. If it prints `active`, a call was in flight: wait a minute and run it
again.

**What happens to a request that arrives between the check and the stop.**
- On SIGTERM the gateway finishes the call it is handling, writes its record,
  clears the intent and then exits. `systemctl stop` can therefore take up to
  90 seconds (systemd's default stop timeout).
- A connection that arrives after the stop has begun is refused before it is
  accepted. No intent, no record and no upstream call.
- Only if the upstream takes longer than 90 seconds does systemd kill the
  gateway, leaving the intent open. In that case the second line prints
  `INTENT PRESENT`.

| Second line | Action |
|---|---|
| `no intent` | Continue to B2 |
| `INTENT PRESENT` | Continue to B2, which backs the intent up. If B3 then verifies cleanly, the new binary records the call at start without loss. If B3 reports a torn frame, follow B3's abort rule |

### B2. Back up (every copy is checked; any failure stops the step)

```sh
sudo sh -eu -s "$B" "$G" <<'BACKUP'
B=$1; G=$2
test ! -e "$B/SHA256SUMS" || { echo "STOP: backup already complete"; exit 1; }
test "$(systemctl is-active axioma-gateway || true)" != active || { echo "STOP: service running"; exit 1; }
echo "98539ff35262853c2da821c9dffb39a7e60a6c9d331119c4a39be12766c30ac4  /usr/local/bin/axioma-gateway" | sha256sum -c --quiet
echo "b3ded76a9ac8f00cf6a3852aa8b2e1080518bc11967de7af5aaf2bb946231567  /etc/axioma-gateway.conf" | sha256sum -c --quiet
echo "1beba2ef27f6ed09c6f50ce0f7ca9583e15ef8238dacca95301958db45b3aa25  /etc/systemd/system/axioma-gateway.service" | sha256sum -c --quiet
EXP=$(sed -n 's/^export_path=//p' /etc/axioma-gateway.conf)
if test -z "$EXP" || test ! -f "$EXP"; then echo "STOP: export file missing: '$EXP'"; exit 1; fi
install -d -m 0700 "$B"
cp -p /usr/local/bin/axioma-gateway "$B/axioma-gateway.pre-upgrade"
cmp /usr/local/bin/axioma-gateway "$B/axioma-gateway.pre-upgrade"
for f in /etc/axioma-gateway.conf /etc/systemd/system/axioma-gateway.service \
         "$G/l6-evidence.bin" "$G/gw.log" "$EXP"; do
    cp -p "$f" "$B/"; cmp "$f" "$B/$(basename "$f")"
done
if test -e "$G/wal.open"; then cp -p "$G/wal.open" "$B/"; cmp "$G/wal.open" "$B/wal.open"; fi
cd "$B" || exit 1; chmod 0400 -- *
sums=$(sha256sum -- *); printf '%s\n' "$sums" > SHA256SUMS; chmod 0400 SHA256SUMS
cat SHA256SUMS
echo BACKUP-OK
BACKUP
```

- **Expected:** the digest list, then `BACKUP-OK`.
- The script stops at the first failure without printing `BACKUP-OK`. A failure is any of: a digest that differs from the recorded value, a missing export file, a failed copy, or a copy that differs from its source.
- It refuses to run while the service is up, or a second time once a backup is complete. A repeat can therefore never record the new binary as the "pre-upgrade" one.
- **Abort** if `BACKUP-OK` is missing: `sudo systemctl start axioma-gateway`. The old service runs exactly as before. Then send me the output.

### B3. Verify the backup and take the pre-upgrade checkpoint

```sh
sudo cat $B/l6-evidence.bin > ~/e5/a6/pre.bin
v --checkpoint 214:c84aacce8138339317aed9025236a484c776f8413fd75db7611f79dd91f11558 ~/e5/a6/pre.bin | tail -n 1
v ~/e5/a6/pre.bin | sed -n 's/^frames=\([0-9]*\) head=\([0-9a-f]*\).*/\1:\2/p' > ~/e5/a6/pre-checkpoint.tmp && test -s ~/e5/a6/pre-checkpoint.tmp && mv ~/e5/a6/pre-checkpoint.tmp ~/e5/a6/pre-checkpoint.txt && cat ~/e5/a6/pre-checkpoint.txt
```

**Expected:** `MATCHES_CHECKPOINT: frames 1..214 …`, then one line `N:H`.
**Write `N:H` down off the host now**, on paper or your phone. It is the
pre-upgrade checkpoint.

**Abort rules:**
- **Checkpoint fails and `INTENT PRESENT` was reported in B1:** the stop interrupted a write. Abort the window with `sudo systemctl start axioma-gateway`. The old binary repairs the torn tail and records the call, as it does today. Retry another day from P after the retry step below.
- **Checkpoint fails with no intent:** abort the same way, and do not retry. Send the output for review.

**Before any retry after an abort,** set the earlier attempt's files aside.
Never delete them: the backup now describes an older state, and B2 refuses to
overwrite a complete backup.

```sh
sudo mv /root/axioma-a6-backup /root/axioma-a6-backup.$(date -u +%Y%m%dT%H%M%SZ)
mv ~/e5/a6 ~/e5/a6.$(date -u +%Y%m%dT%H%M%SZ)
```

### B4. Install (safe to repeat)

```sh
sudo install -m 0755 -o root -g root $R/build-verify/axioma-gateway /usr/local/bin/axioma-gateway
sha256sum /usr/local/bin/axioma-gateway
grep -q '^max_attempts=' /etc/axioma-gateway.conf || printf '\nmax_attempts=1\n' | sudo tee -a /etc/axioma-gateway.conf
grep '^max_attempts=' /etc/axioma-gateway.conf
sudo chmod 0640 $G/gw.log
```

- **Expected:** `1f8931c2…`, and exactly one line `max_attempts=1`.
- **Abort** on anything else: go to Rollback.

### B5. Start and accept

```sh
sudo systemctl start axioma-gateway; systemctl is-active axioma-gateway
PID=$(systemctl show -p MainPID --value axioma-gateway); echo "main pid $PID"
sudo sha256sum /proc/$PID/exe
systemctl show -p ExecMainStartTimestamp axioma-gateway
sudo test -S $G/gw.sock && echo SOCKET-OK
sudo tail -n 4 $G/gw.log
sudo cat $G/l6-evidence.bin > ~/e5/a6/post.bin
cmp -n "$(stat -c %s ~/e5/a6/pre.bin)" ~/e5/a6/pre.bin ~/e5/a6/post.bin && echo PREFIX-IDENTICAL
v --checkpoint "$(cat ~/e5/a6/pre-checkpoint.txt)" ~/e5/a6/post.bin | tail -n 1
v --list ~/e5/a6/post.bin | tail -n 1
sudo cmp $G/l6-evidence.bin "$(sed -n 's/^export_path=//p' /etc/axioma-gateway.conf)" && echo EXPORT-OK
sudo ls -l $G/gw.log
v ~/e5/a6/post.bin | sed -n 's/^frames=\([0-9]*\) head=\([0-9a-f]*\).*/\1:\2/p' | tee ~/e5/a6/post-checkpoint.txt
```

After a minute, run this once more:

```sh
systemctl show -p MainPID --value axioma-gateway
```

It must print the same PID, which shows no crash-and-restart loop.

**Accept only if all of these hold:**
1. `active`.
2. `/proc/<pid>/exe` is `1f8931c2a2487599938da5cf639b6170f0e14e45ce470069a92575082f27921c`. This is the binary systemd is actually running, not merely the file on disk.
3. The main PID is unchanged a minute later.
4. `SOCKET-OK`.
5. The log lines include `export_enabled`, `config_commit` and `startup`, and none has `"level":"error"`. A `wal_recovered` line is expected if B1 reported an intent.
6. `PREFIX-IDENTICAL`.
7. `MATCHES_CHECKPOINT: frames 1..N …`, where N is from B3.
8. The last record is `STATE … source=024ca3833bb5db39ae501b7accc0c17078e39e53`.
9. `EXPORT-OK`.
10. `gw.log` is `-rw-r-----`.

The post-upgrade checkpoint is N+1 frames, or N+2 if an intent was recorded.
**If any check fails, roll back.** A paid test request is not part of
acceptance.

**After the window,** from your MacBook, keep both checkpoints and the
pre-upgrade ledger copy off the host:

```sh
scp axioma:e5/a6/pre-checkpoint.txt axioma:e5/a6/post-checkpoint.txt axioma:e5/a6/pre.bin .
```

## Rollback

Rollback reinstalls the old binary. It keeps:
- **the ledger:** the July binary replays records written by the new one, as proven in Part A;
- **the unit:** it was never changed;
- **`max_attempts=1`:** the July binary accepts it, as proven in Part A. Restoring the old configuration would bring back up to three paid transmissions per record, only one of them visible in the record.

After a rollback the old defects return until the upgrade is retried:
- B-02, the loss of a repeated prompt interrupted in flight;
- the old open-intent behaviour.

The old binary's own STATE record makes the rollback visible in the ledger.

**R1. Stop and look for an intent.** Paste the settings block first if you reconnected.

```sh
sudo systemctl stop axioma-gateway
sudo test -e $G/wal.open && echo "INTENT PRESENT" || echo "no intent"
```

With `no intent`, go to R3.

**R2. An intent is present: decide whether the old binary can process it.**
The old binary ignores the intent's `ledger_seq`. It treats the intent as
already recorded whenever the last record carries the same input hash, which
is right only if that record really is this call. So:

1. **If the new binary was running normally** (you are rolling back for
   another reason), let it resolve the intent first:
   ```sh
   sudo systemctl start axioma-gateway; sleep 3; sudo tail -n 3 $G/gw.log; sudo systemctl stop axioma-gateway
   sudo test -e $G/wal.open && echo "INTENT PRESENT" || echo "no intent"
   ```
   If the log shows `wal_recovered` and now `no intent`, go to R3. If it shows
   `refusing to start`, **stop here**: the ledger and the intent disagree and
   need review, and the old binary must not be started on them.

2. **If the new binary cannot start**, read the intent and the ledger:
   ```sh
   sudo cat $G/wal.open
   sudo cat $G/l6-evidence.bin > ~/e5/a6/rb.bin
   v ~/e5/a6/rb.bin | head -n 1
   v --list ~/e5/a6/rb.bin | tail -n 1
   ```
   Let `S` be the intent's `ledger_seq` line, `N` the `frames=` count, and
   `M` whether the last record is an `OBS` whose `input=` equals the first 12
   characters of the intent's `input_hash`.

   | Intent and ledger | Old binary will | Action |
   |---|---|---|
   | No `ledger_seq` line (left by the old binary itself) | Handle it as it does today | Recoverable: go to R3 |
   | `S` = N and `M` yes | Clear it; the call is already recorded | Recoverable: go to R3 |
   | `S` = N+1 and `M` no | Record the call as Interrupted | Recoverable: go to R3 |
   | `S` = N+1 and `M` yes | Clear it wrongly: the call would be lost (B-02) | **Not safe. Stop.** Leave the service stopped and the files untouched, and send me the output |
   | Anything else, including a `torn frame` result | Unpredictable | **Not safe. Stop**, as above |

**R3. Reinstall the old binary and check.**

```sh
sudo install -m 0755 -o root -g root $B/axioma-gateway.pre-upgrade /usr/local/bin/axioma-gateway
sha256sum /usr/local/bin/axioma-gateway
sudo systemctl start axioma-gateway; systemctl is-active axioma-gateway
PID=$(systemctl show -p MainPID --value axioma-gateway); sudo sha256sum /proc/$PID/exe
sudo cat $G/l6-evidence.bin > ~/e5/a6/rollback.bin
cmp -n "$(stat -c %s ~/e5/a6/pre.bin)" ~/e5/a6/pre.bin ~/e5/a6/rollback.bin && echo PREFIX-IDENTICAL
v --checkpoint "$(cat ~/e5/a6/pre-checkpoint.txt)" ~/e5/a6/rollback.bin | tail -n 1
```

**Expected:** `98539ff3…` on disk and for the running process, `active`,
`PREFIX-IDENTICAL` and `MATCHES_CHECKPOINT`.

## Never, at any point

- Never copy, move or redirect anything onto
  `/var/lib/axioma-gateway/l6-evidence.bin`, and never truncate, edit or
  "restore" it, including from `$B`. The backup copy is evidence of the
  pre-upgrade state, not a replacement: restoring it would delete every
  record written since.
- Never delete `wal.open` to make the service start.
- Never remove `$B` until you decide to, after the upgrade is accepted.

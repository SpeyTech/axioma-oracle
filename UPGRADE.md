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

## Part B: production upgrade (only after A6)

Allow fifteen minutes during which no client needs the gateway. `B` below is
the backup directory; `~/e5/axioma-oracle` is the build verified in Part A.

```sh
B=/root/axioma-upgrade-$(date -u +%Y%m%dT%H%M%SZ)
sudo install -d -m 0700 "$B"; echo "$B"
```

**B1. Stop and check for an open intent.**

```sh
sudo systemctl stop axioma-gateway
systemctl is-active axioma-gateway               # inactive
sudo ls -l /var/lib/axioma-gateway
```

If `wal.open` is present, a call was in flight. Leave it: the new binary
records it at start as an Interrupted record (logged as a legacy intent).

**B2. Snapshot everything, with digests.**

```sh
sudo cp -p /usr/local/bin/axioma-gateway "$B/axioma-gateway.pre-upgrade"
sudo cp -p /etc/axioma-gateway.conf /etc/systemd/system/axioma-gateway.service "$B/"
sudo cp -p /var/lib/axioma-gateway/l6-evidence.bin /var/lib/axioma-gateway/gw.log "$B/"
sudo cp -p /var/lib/axioma-gateway-export/l6-evidence-export.bin "$B/" 2>/dev/null || true
sudo sh -c "cd '$B' && sha256sum * > SHA256SUMS && cat SHA256SUMS"
```

The environment file holding the API key is not copied.

**B3. Verify the snapshot and take the pre-upgrade checkpoint.**

```sh
sudo cat "$B/l6-evidence.bin" > ~/e5/pre.bin
A=~/e5/axioma-oracle/tools
python3 -I $A/axioma-verify-ledger --checkpoint 214:c84aacce8138339317aed9025236a484c776f8413fd75db7611f79dd91f11558 ~/e5/pre.bin
python3 -I $A/axioma-verify-ledger ~/e5/pre.bin
```

The first must say `MATCHES_CHECKPOINT` (the history up to 14 October is
unaltered). The second prints `frames=N head=H`: write `N:H` down off the host.
It is the pre-upgrade checkpoint. If either check fails, stop here and restart
the old service with `sudo systemctl start axioma-gateway`; nothing has been
changed.

**B4. Install.**

```sh
sudo install -m 0755 -o root -g root ~/e5/axioma-oracle/build-verify/axioma-gateway /usr/local/bin/axioma-gateway
grep -q '^max_attempts=' /etc/axioma-gateway.conf || printf '\nmax_attempts=1\n' | sudo tee -a /etc/axioma-gateway.conf
sudo chmod 0640 /var/lib/axioma-gateway/gw.log
sha256sum /usr/local/bin/axioma-gateway          # equals the Part A verify.sh value
```

**B5. Start and check.**

```sh
sudo systemctl start axioma-gateway
systemctl status axioma-gateway --no-pager
sudo tail -n 5 /var/lib/axioma-gateway/gw.log    # export_enabled; no fault
sudo cat /var/lib/axioma-gateway/l6-evidence.bin > ~/e5/post.bin
cmp -n "$(stat -c %s ~/e5/pre.bin)" ~/e5/pre.bin ~/e5/post.bin && echo PREFIX-IDENTICAL
python3 -I $A/axioma-verify-ledger --checkpoint N:H ~/e5/post.bin   # MATCHES_CHECKPOINT
python3 -I $A/axioma-verify-ledger --list ~/e5/post.bin | tail -n 1 # STATE, source_commit = reviewed commit
sudo cmp /var/lib/axioma-gateway/l6-evidence.bin /var/lib/axioma-gateway-export/l6-evidence-export.bin && echo EXPORT-OK
sudo ls -l /var/lib/axioma-gateway               # gw.log -rw-r-----
```

Replace `N:H` with the B3 checkpoint. Keep `B` until you decide otherwise.

## Rollback

Rollback restores the old binary and configuration. It keeps the ledger: the
installed binary replays records written by the new one (Part A shows this on
the copy). Never restore `l6-evidence.bin` from the backup; that would delete
every record written since.

```sh
sudo systemctl stop axioma-gateway
sudo ls -l /var/lib/axioma-gateway
```

If `wal.open` is present, first let the new binary record it:
`sudo systemctl start axioma-gateway`, wait for `wal_recovered` in the log,
then stop again. The old binary would ignore the intent's `ledger_seq` and
fall back to the prompt comparison that can lose a repeated call (B-02).

```sh
sudo install -m 0755 -o root -g root "$B/axioma-gateway.pre-upgrade" /usr/local/bin/axioma-gateway
sudo install -m 0644 -o root -g root "$B/axioma-gateway.conf" /etc/axioma-gateway.conf
sudo systemctl start axioma-gateway
systemctl status axioma-gateway --no-pager
sudo cat /var/lib/axioma-gateway/l6-evidence.bin > ~/e5/rollback.bin
python3 -I $A/axioma-verify-ledger --checkpoint N:H ~/e5/rollback.bin   # MATCHES_CHECKPOINT
```

After a rollback the old defects are back (B-02, open-intent handling,
`max_attempts` 3 unless the line is kept). Keeping `max_attempts=1` in the
configuration is safe with the old binary; restoring the backed-up file is
shown only to return exactly to the pre-upgrade state.

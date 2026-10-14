# Installing axioma-gateway

This is a fresh installation on a host with no existing gateway. An existing
installation is upgraded only by [UPGRADE.md](UPGRADE.md), never by these
steps: step 5 onwards would overwrite a running service's files.

Reference platform: Debian 13, aarch64, GCC 14, OpenSSL 3.5. CI also covers
Ubuntu 24.04 on x86_64 (GCC and Clang) and aarch64.

## 1. Build and test (ordinary user)

```sh
sudo apt-get install -y build-essential cmake git python3 shellcheck \
     libcurl4-openssl-dev libssl-dev
git clone https://github.com/SpeyTech/axioma-oracle.git
cd axioma-oracle
git checkout <reviewed commit>
./verify.sh
```

`verify.sh` builds in `./build-verify`, runs every test, builds a second time
and compares the two gateway binaries, then prints the binary's sha256 and the
`source_commit` it will record. It must end `VERIFY PASSED`. Build from a clean
checkout: modified tracked files make `source_commit` read `unknown`.

## 2. Demonstrate (ordinary user)

```sh
sh tools/demo.sh
```

Runs entirely in a temporary directory against a local stand-in upstream, with
no network and no spend. See OPERATOR.md section 2 for what it shows.

## 3. Service account and directories

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin axgw
sudo install -d -o axgw -g axgw -m 0750 /var/lib/axioma-gateway
# Optional: only if export_path is set (a mirror for a separate reader)
sudo install -d -o axgw -g axgw -m 0755 /var/lib/axioma-gateway-export
```

## 4. Install the files

```sh
sudo install -m 0755 -o root -g root build-verify/axioma-gateway /usr/local/bin/
sudo install -m 0755 -o root -g root tools/axioma-verify-ledger tools/axioma-client /usr/local/bin/
sudo install -m 0644 -o root -g root gateway/gateway.conf.example /etc/axioma-gateway.conf
sudo install -m 0644 -o root -g root gateway/axioma-gateway.service /etc/systemd/system/
```

Alternatively stage everything with `cmake --install build-verify --prefix <dir>`
and copy from there; the staged tree holds the binaries, the tools under
`share/axioma/tools` and these documents under `share/axioma`.

## 5. Configure

Edit `/etc/axioma-gateway.conf`. Keep `max_attempts=1` (CONTRACT.md R-4). Set
`call_budget` deliberately: it bounds paid calls per service run. Unknown keys
are refused at start.

The API key lives only in a root-owned environment file:

```sh
sudo install -m 0600 -o root -g root /dev/null /etc/axioma-gateway.env
sudoedit /etc/axioma-gateway.env      # one line: AX_ORACLE_ANTHROPIC_KEY=...
```

## 6. Start

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now axioma-gateway
systemctl status axioma-gateway
sudo tail -n 5 /var/lib/axioma-gateway/gw.log
```

The unit is `Type=notify`: `systemctl start` returns once the socket accepts.
The socket is mode 0660, group `axgw`: add client users to that group.

## 7. Record the first checkpoint

```sh
sudo cat /var/lib/axioma-gateway/l6-evidence.bin > /tmp/l6.bin
axioma-verify-ledger /tmp/l6.bin
```

Keep the printed `frames=N head=H` pair somewhere other than this host. From
then on, `axioma-verify-ledger --checkpoint N:H` on any later copy proves the
first N records are unaltered (OPERATOR.md section 4).

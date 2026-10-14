#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Spey Systems Ltd (SC889983)
"""test_gw_recovery.py - evidence accounting and intent recovery tests.

Each test id is one ctest entry (CONTRACT.md, recovery plan revision 2):

  b02_1        repeated prompt; the second request is interrupted by SIGKILL
               while in flight. Both accepted requests must be on the chain.
  b02_2        the intent's record is already at its expected sequence:
               the intent is cleared and nothing is duplicated.
  b02_3        the previous record carries the same prompt but the intent's
               own record is absent: one Interrupted record is written (the
               offline form of B-02).
  b02_4        legacy intent (no ledger_seq line): recorded conservatively as
               Interrupted, never assumed to be evidenced.
  b02_5        the intent's sequence position is occupied by an incompatible
               record, or the ledger is shorter or longer than the intent
               allows: the gateway refuses to start and changes nothing.
  oi_1         an internal append failure leaves the intent open: the gateway
               exits non-zero instead of serving on, and the next start
               records the call as Interrupted.
  acc_1        accounting: accepted requests equal evidence records equal
               upstream transmissions under the default configuration, with
               every outcome class present; refused requests produce neither.
  state_source AX:STATE:v1 carries the configuration digest and a
               source_commit that is either the clean checkout's HEAD or
               "unknown" (D8).

Local stand-in upstreams only: no network egress, no spend. Standard library
only. The ledger reader below is written from the frame format of record,
not from the gateway's code.

usage: test_gw_recovery.py <axioma-gateway> <test-id> [<source-dir>]
exit:  0 pass, 1 fail, 77 skip
"""

import hashlib
import http.server
import json
import os
import resource
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

CHAIN_TAG = b"AX:LEDGER:v1"
GENESIS = (b'{"component":"axilog-core","evidence_type":"AX:STATE:v1",'
           b'"is_terminal":false,"platform":"universal","state_hash":"'
           + b"0" * 64 + b'"}')


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def interrupted_hash(intent_hash_hex):
    """input_hash of the Interrupted record written for an open intent."""
    return sha(b"unclosed-intent input_hash=" + intent_hash_hex.encode())


# ---- ledger reader (frame format of record) --------------------------------

def _commit(tag, payload):
    return hashlib.sha256(tag + struct.pack("<Q", len(payload)) + payload).digest()


def read_frames(path):
    """[(seq, tag, payload)] with every commit and the chain recomputed."""
    with open(path, "rb") as f:
        data = f.read()
    head = hashlib.sha256(CHAIN_TAG + _commit(b"AX:STATE:v1", GENESIS)).digest()
    out, off, seq = [], 0, 0
    while off < len(data):
        (tl,) = struct.unpack_from("<I", data, off)
        off += 4
        tag = data[off:off + tl]
        off += tl
        (pl,) = struct.unpack_from("<Q", data, off)
        off += 8
        payload = data[off:off + pl]
        off += pl
        stored = data[off:off + 32]
        off += 32
        check(len(stored) == 32, "torn frame in %s" % path)
        c = _commit(tag, payload)
        check(c == stored, "commit mismatch at frame %d" % (seq + 1))
        head = hashlib.sha256(CHAIN_TAG + head + c).digest()
        seq += 1
        out.append((seq, tag.decode(), payload))
    return out


def obs_records(path):
    return [(s, json.loads(p)) for s, t, p in read_frames(path) if t == "AX:OBS:v1"]


# ---- stand-in upstream ------------------------------------------------------

class StandIn:
    """Local HTTP endpoint with behaviour chosen by the prompt's first word.

    ok* / okbig* 200 COMPLETE (okbig: about 4 KiB of output)
    trunc*       200 TRUNCATED
    fail*        500 error
    r429*        429 error
    slow*        200 after 3 s (beyond a 1 s response timeout)
    bad*         200 with a body that is not JSON
    tab*         200 whose text contains a TAB (refused by SHALL-044)

    receipts counts every request received. Requests numbered hang_from or
    later block until release() is called.
    """

    def __init__(self):
        self.receipts = 0
        self.hang_from = 0
        self.release_ev = threading.Event()
        self.lock = threading.Lock()
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("content-length", "0")))
                with outer.lock:
                    outer.receipts += 1
                    n = outer.receipts
                if outer.hang_from and n >= outer.hang_from:
                    outer.release_ev.wait(60)
                    return
                p = json.loads(body)["messages"][0]["content"]
                code, stop, text, raw = 200, "end_turn", "echo: " + p, None
                if p.startswith("okbig"):
                    text = "echo: " + p + " " + "x" * 4000
                elif p.startswith("trunc"):
                    stop = "max_tokens"
                elif p.startswith("fail"):
                    code = 500
                elif p.startswith("r429"):
                    code = 429
                elif p.startswith("slow"):
                    time.sleep(3)
                elif p.startswith("bad"):
                    raw = b'{"content": [ not json'
                elif p.startswith("tab"):
                    text = "a\tb"
                if code != 200:
                    out = json.dumps({"type": "error", "error": {
                        "type": "api_error", "message": "stand-in %d" % code}}).encode()
                elif raw is not None:
                    out = raw
                else:
                    out = json.dumps({"content": [{"type": "text", "text": text}],
                                      "stop_reason": stop}).encode()
                try:
                    self.send_response(code)
                    self.send_header("content-type", "application/json")
                    self.send_header("content-length", str(len(out)))
                    self.end_headers()
                    self.wfile.write(out)
                except OSError:
                    pass

        self.srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.srv.daemon_threads = True
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def release(self):
        self.release_ev.set()

    def close(self):
        self.release()
        self.srv.shutdown()


# ---- gateway process and client ----------------------------------------------

class Env:
    def __init__(self, gw_bin, tmp, standin):
        self.gw_bin, self.tmp, self.si = gw_bin, tmp, standin
        self.sock = os.path.join(tmp, "gw.sock")
        self.ledger = os.path.join(tmp, "evidence.bin")
        self.wal = os.path.join(tmp, "intent.wal")
        self.log = os.path.join(tmp, "gw.log")
        self.conf = os.path.join(tmp, "gw.conf")
        self.proc = None

    def write_conf(self, extra=""):
        with open(self.conf, "w") as f:
            f.write("socket_path=%s\nledger_path=%s\nwal_path=%s\nlog_path=%s\n"
                    "model_id=test-model\noracle_id=test-oracle\n"
                    "api_url=http://127.0.0.1:%d/v1/messages\n"
                    "connect_timeout_s=5\ncall_budget=50\n%s"
                    % (self.sock, self.ledger, self.wal, self.log, self.si.port, extra))

    def start(self, fsize_limit=None, expect_ready=True):
        env = dict(os.environ, AX_ORACLE_ANTHROPIC_KEY="test-key-not-real")
        env.pop("NOTIFY_SOCKET", None)

        def limit():
            if fsize_limit is not None:
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE, (fsize_limit, fsize_limit))

        self.proc = subprocess.Popen([self.gw_bin, self.conf], env=env,
                                     stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, preexec_fn=limit)
        deadline = time.time() + 15
        while time.time() < deadline:
            if self.proc.poll() is not None:
                check(not expect_ready, "gateway exited at start, status %s"
                      % self.proc.returncode)
                return self.proc.returncode
            if os.path.exists(self.sock):
                try:
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    s.connect(self.sock)
                    s.close()
                    check(expect_ready, "gateway started; it should have refused")
                    return None
                except OSError:
                    pass
            time.sleep(0.05)
        raise Fail("gateway neither became ready nor exited")

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            self.proc.wait(20)
        self.proc = None

    def kill(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(20)
        self.proc = None

    def request(self, prompt, timeout=30.0):
        """Send one request; return (headers dict, output bytes)."""
        p = prompt.encode() if isinstance(prompt, str) else prompt
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(self.sock)
        s.sendall(b"max_tokens: 64\ntemperature_q16: 0\nprompt_len: %d\n\n" % len(p) + p)
        data = b""
        try:
            while True:
                b = s.recv(65536)
                if not b:
                    break
                data += b
        except OSError:
            pass
        s.close()
        hdr, _, body = data.partition(b"\n\n")
        h = {}
        for line in hdr.decode(errors="replace").splitlines():
            k, _, v = line.partition(": ")
            h[k] = v
        return h, body

    def write_intent(self, input_hash, ledger_seq=None):
        lines = ["input_hash=%s" % input_hash, "model_id=test-model",
                 "max_tokens=64", "seed=-1", "temperature_q16=0",
                 "top_p_q16=-2147483648"]
        if ledger_seq is not None:
            lines.append("ledger_seq=%d" % ledger_seq)
        with open(self.wal, "w") as f:
            f.write("\n".join(lines) + "\n")

    def log_text(self):
        try:
            with open(self.log) as f:
                return f.read()
        except OSError:
            return ""


def seeded(e, prompt="ok same prompt"):
    """Start, serve one COMPLETE request, stop. Returns its input_hash."""
    e.start()
    h, _ = e.request(prompt)
    check(h.get("status") == "ok", "seed request not served: %r" % h)
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 1, "seed: expected one OBS record")
    return recs[0][1]["input_hash"]


# ---- tests --------------------------------------------------------------------

def t_b02_1(e):
    e.write_conf("max_attempts=1\nresponse_timeout_s=30\n")
    e.start()
    h, _ = e.request("ok same prompt")
    check(h.get("status") == "ok", "first request not served")
    ph = obs_records(e.ledger)[0][1]["input_hash"]
    e.si.hang_from = 2
    t = threading.Thread(target=lambda: e.request("ok same prompt", 30), daemon=True)
    t.start()
    deadline = time.time() + 10
    while e.si.receipts < 2 and time.time() < deadline:
        time.sleep(0.05)
    check(e.si.receipts == 2, "second request never reached the stand-in")
    e.kill()
    e.si.release()
    check(os.path.exists(e.wal), "no open intent after SIGKILL mid-request")
    e.start()
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 2, "B-02: %d OBS record(s) for 2 accepted requests; "
          "the interrupted repeat of the prompt is missing" % len(recs))
    r = recs[1][1]
    check(r["input_hash"] == interrupted_hash(ph), "second record is not the "
          "Interrupted record for the open intent")
    check(r["failure_type"] == "TRANSPORT_ERROR", "Interrupted record type")
    check(not os.path.exists(e.wal), "intent not cleared after recovery")
    check(e.si.receipts <= len(recs), "more transmissions than records")


def t_b02_2(e):
    e.write_conf("max_attempts=1\n")
    ph = seeded(e)
    e.write_intent(ph, ledger_seq=2)
    e.start()
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 1, "record duplicated: %d OBS records" % len(recs))
    check(not os.path.exists(e.wal), "intent not cleared")
    frames = read_frames(e.ledger)
    check(frames[-1][1] == "AX:STATE:v1" and len(frames) == 3,
          "expected STATE, OBS, STATE; got %r" % [f[1] for f in frames])


def t_b02_3(e):
    e.write_conf("max_attempts=1\n")
    ph = seeded(e)
    e.write_intent(ph, ledger_seq=3)
    e.start()
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 2, "B-02 (offline form): the intent's record was skipped "
          "because the previous record has the same prompt (%d OBS)" % len(recs))
    seq, r = recs[1]
    check(seq == 3 and r["ledger_seq"] == 3, "Interrupted record not at seq 3")
    check(r["input_hash"] == interrupted_hash(ph), "not the Interrupted record")
    check(not os.path.exists(e.wal), "intent not cleared")


def t_b02_4(e):
    e.write_conf("max_attempts=1\n")
    ph = seeded(e)
    e.write_intent(ph, ledger_seq=None)
    e.start()
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 2, "legacy intent assumed evidenced (%d OBS); it must be "
          "recorded as Interrupted" % len(recs))
    check(recs[1][1]["input_hash"] == interrupted_hash(ph), "not the Interrupted record")
    check("legacy" in e.log_text(), "legacy intent handling not logged")
    check(not os.path.exists(e.wal), "intent not cleared")


def t_b02_5(e):
    e.write_conf("max_attempts=1\n")
    seeded(e)
    with open(e.ledger, "rb") as f:
        before = f.read()
    other = sha(b"a different prompt")
    for label, h, n in (("occupied by an incompatible record", other, 2),
                        ("ledger shorter than the intent allows", other, 5),
                        ("ledger already past the intent", other, 1)):
        e.write_intent(h, ledger_seq=n)
        rc = e.start(expect_ready=False)
        check(rc is not None and rc != 0, "%s: gateway did not refuse to start"
              % label)
        with open(e.ledger, "rb") as f:
            check(f.read() == before, "%s: ledger changed" % label)
        check(os.path.exists(e.wal), "%s: intent removed" % label)


def t_oi_1(e):
    e.write_conf("max_attempts=1\n")
    e.start()
    for i in range(8):
        h, _ = e.request("ok filler %d" % i)
        check(h.get("status") == "ok", "filler request %d not served" % i)
    e.stop()
    os.unlink(e.log)
    limit = os.path.getsize(e.ledger) + 600     # room for STATE, not for okbig
    e.start(fsize_limit=limit)
    prompt = "okbig append will fail"
    h, _ = e.request(prompt)
    check("obs_hash" not in h, "request reported as recorded: %r" % h)
    try:
        e.proc.wait(10)
    except subprocess.TimeoutExpired:
        raise Fail("gateway kept accepting connections with an open intent "
                   "after an internal append failure")
    rc = e.proc.returncode
    e.proc = None
    check(rc != 0, "gateway exited with status 0 after an internal fault")
    check(os.path.exists(e.wal), "open intent not preserved for recovery")
    check('"event":"fault"' in e.log_text(), "no diagnostic logged")
    e.start()
    e.stop()
    recs = obs_records(e.ledger)
    check(len(recs) == 9, "expected 8 records plus 1 Interrupted, got %d" % len(recs))
    check(recs[-1][1]["input_hash"] == interrupted_hash(sha(prompt.encode())),
          "last record is not the Interrupted record for the failed request")
    check(not os.path.exists(e.wal), "intent not cleared after recovery")


def t_acc_1(e):
    # No max_attempts line: the default is the supported configuration (D6).
    e.write_conf("response_timeout_s=1\n")
    e.start()
    plan = (["ok a", "ok b", "ok c", "ok d", "ok e", "ok f"] +
            ["trunc a", "trunc b", "fail a", "fail b", "r429 a", "r429 b",
             "slow a", "slow b", "bad a", "bad b", "tab a", "tab b"])
    refused = ["é not nfc", "é again"]
    accepted = 0
    for p in plan + refused:
        h, _ = e.request(p)
        if "obs_hash" in h:
            accepted += 1
        else:
            check(h.get("status") == "error" and "reason" in h,
                  "unrecorded request without a refusal reason: %r" % h)
    e.stop()
    recs = obs_records(e.ledger)
    check(accepted == len(plan), "accepted %d of %d" % (accepted, len(plan)))
    check(len(recs) == accepted, "records %d != accepted %d" % (len(recs), accepted))
    check(e.si.receipts == len(recs), "upstream transmissions %d != records %d "
          "(retries are outside the supported configuration)"
          % (e.si.receipts, len(recs)))
    classes = {}
    for _, r in recs:
        k = r["failure_type"] or r["completion_state"]
        classes[k] = classes.get(k, 0) + 1
    want = {"COMPLETE": 6, "TRUNCATED": 2, "TRANSPORT_ERROR": 4,
            "TIMEOUT": 2, "INVALID_OUTPUT": 4}
    check(classes == want, "outcome classes %r != %r" % (classes, want))


def t_state_source(e, srcdir):
    e.write_conf("max_attempts=1\n")
    e.start()
    e.stop()
    frames = read_frames(e.ledger)
    check(frames and frames[0][1] == "AX:STATE:v1", "first frame is not STATE")
    st = json.loads(frames[0][2])
    with open(e.conf, "rb") as f:
        check(st.get("config_sha256") == sha(f.read()), "config digest wrong")
    check(set(st) == {"component", "config_sha256", "evidence_type", "source_commit"},
          "unexpected STATE keys %r" % sorted(st))
    v = st["source_commit"]
    if v == "unknown":
        return
    check(len(v) == 40 and all(c in "0123456789abcdef" for c in v),
          "source_commit is neither a commit id nor 'unknown': %r" % v)
    if srcdir:
        try:
            head = subprocess.run(["git", "-C", srcdir, "rev-parse", "HEAD"],
                                  capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            raise Fail("source_commit claims %s but %s is not a git checkout" % (v, srcdir))
        check(v == head, "source_commit %s is not HEAD %s" % (v, head))


TESTS = {"b02_1": t_b02_1, "b02_2": t_b02_2, "b02_3": t_b02_3, "b02_4": t_b02_4,
         "b02_5": t_b02_5, "oi_1": t_oi_1, "acc_1": t_acc_1}


def main(argv):
    if len(argv) not in (3, 4) or (argv[2] not in TESTS and argv[2] != "state_source"):
        sys.stderr.write("usage: test_gw_recovery.py <axioma-gateway> <%s|state_source> "
                         "[<source-dir>]\n" % "|".join(TESTS))
        return 2
    gw_bin, tid = os.path.abspath(argv[1]), argv[2]
    srcdir = argv[3] if len(argv) == 4 else None
    si = StandIn()
    tmp = tempfile.mkdtemp(prefix="gwrec-")
    e = Env(gw_bin, tmp, si)
    try:
        if tid == "state_source":
            t_state_source(e, srcdir)
        else:
            TESTS[tid](e)
        print("PASS %s" % tid)
        return 0
    except Fail as x:
        print("FAIL %s: %s" % (tid, x))
        return 1
    finally:
        e.kill()
        si.close()
        subprocess.run(["rm", "-rf", tmp], check=False)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

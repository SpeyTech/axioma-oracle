"""Reference client for the gateway's Unix-socket line protocol.

usage: gw-client.py SOCKET PROMPT [TEMPERATURE_Q16]

TEMPERATURE_Q16 is a Q16.16 integer (65536 = 1.0), default 0. The gateway
requires the temperature_q16 line and refuses an unpinned request with
reason temperature_required, so it is always sent.
"""
import socket
import sys

if len(sys.argv) not in (3, 4):
    sys.exit("usage: gw-client.py SOCKET PROMPT [TEMPERATURE_Q16]")
prompt = sys.argv[2].encode()
temperature_q16 = int(sys.argv[3]) if len(sys.argv) == 4 else 0
if not 0 <= temperature_q16 <= 131072:
    sys.exit("gw-client.py: TEMPERATURE_Q16 must be 0..131072 (0.0 to 2.0)")
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
hdr = b"max_tokens: 64\ntemperature_q16: %d\nprompt_len: %d\n\n" % (
    temperature_q16, len(prompt))
s.sendall(hdr + prompt)
data = b""
while True:
    b = s.recv(65536)
    if not b:
        break
    data += b
print(data.decode(errors="replace"))

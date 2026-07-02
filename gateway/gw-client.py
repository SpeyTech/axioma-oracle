import socket, sys
prompt = sys.argv[2].encode()
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
hdr = b"max_tokens: 64\nprompt_len: %d\n\n" % len(prompt)
s.sendall(hdr + prompt)
data = b""
while True:
    b = s.recv(65536)
    if not b: break
    data += b
print(data.decode(errors="replace"))

#!/usr/bin/env python3
"""Run shell commands on the GO 630 over Telnet, bound to the USB gadget interface."""
import os
import socket
import subprocess
import sys
import time

HOST = os.environ.get("HOMTOM_HOST", "192.168.1.10")
HOST_MAC = "02:54:54:00:06:31"
IP_BOUND_IF = 25
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


def gadget_interface():
    for name in subprocess.check_output(["/sbin/ifconfig", "-l"], text=True).split():
        out = subprocess.run(["/sbin/ifconfig", name], capture_output=True, text=True).stdout
        if HOST_MAC in out and "status: active" in out:
            return name
    raise SystemExit("TomTom USB interface not active")


class Session:
    def __init__(self, timeout=30):
        iface = gadget_interface()
        self.sock = socket.socket()
        self.sock.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF, socket.if_nametoindex(iface))
        self.sock.settimeout(timeout)
        self.sock.connect((HOST, 23))
        self.buf = b""
        self.expect(b"login: ")
        self.send(b"root\r\n")
        data = self.expect_any([b"Password: ", b"# "])
        if data.endswith(b"Password: "):
            self.send(b"\r\n")
            self.expect(b"# ")
        self.send(b"stty -echo 2>/dev/null; PS1='# '\r\n")
        self.expect(b"# ")

    def send(self, data):
        self.sock.sendall(data)

    def _recv(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise EOFError("connection closed")
        out = bytearray()
        i = 0
        while i < len(chunk):
            b = chunk[i]
            if b == IAC and i + 1 < len(chunk):
                cmd = chunk[i + 1]
                if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(chunk):
                    opt = chunk[i + 2]
                    if cmd == DO:
                        self.send(bytes([IAC, WONT, opt]))
                    elif cmd == WILL:
                        self.send(bytes([IAC, DONT, opt]))
                    i += 3
                    continue
                if cmd == SB:
                    end = chunk.find(bytes([IAC, SE]), i)
                    i = len(chunk) if end < 0 else end + 2
                    continue
                i += 2
                continue
            out.append(b)
            i += 1
        self.buf += bytes(out)

    def expect_any(self, markers):
        while True:
            for m in markers:
                pos = self.buf.find(m)
                if pos >= 0:
                    data = self.buf[: pos + len(m)]
                    self.buf = self.buf[pos + len(m):]
                    return data
            self._recv()

    def expect(self, marker):
        return self.expect_any([marker])

    def run(self, command):
        tag = "__TT_%d__" % int(time.time() * 1000)
        # The marker is split in the command so the terminal echo cannot match it.
        self.send(("%s; echo %s\"\"%s$?\r\n" % (command, tag[:5], tag[5:])).encode())
        self.expect(b"\n")
        data = self.expect(tag.encode())
        status = self.expect(b"\n").strip()
        text = data[: -len(tag)].decode("latin-1").replace("\r\n", "\n")
        return text, int(status or b"0")


if __name__ == "__main__":
    s = Session(timeout=float(sys.argv[2]) if len(sys.argv) > 2 else 60)
    text, code = s.run(sys.argv[1])
    sys.stdout.write(text)
    sys.exit(code)

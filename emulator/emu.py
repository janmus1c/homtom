#!/usr/bin/env python3
"""homtom emulator: build, run and drive the userspace emulator from the Mac (stdlib only).

    python3 emulator/emu.py up [--port 8630] [--usb]      # or: emulator/run
    python3 emulator/emu.py screenshot out.png
    python3 emulator/emu.py tap 240 136
    python3 emulator/emu.py exec 'ps; cat /proc/mounts'
    python3 emulator/emu.py down
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import time
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "emulator/user"
RUNTIME = ROOT / ".runtime/emu"
LOGS = RUNTIME / "logs"
TOOLCHAIN = ROOT / "artifacts/source/toolchain/gcc-3.3.4_glibc-2.3.2"
BUILDER = "tomtom-revival-build:gcc334-host"
IMAGE = "homtom-emu-user"
NAME = "homtom-emu"
W, H = 480, 272
GUEST = "/run/emu/rootfs"
GUEST_ENV = ["HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin:/mnt/sdcard/opentom/bin",
             "LD_LIBRARY_PATH=/mnt/sdcard/opentom/lib", "LD_PRELOAD=/.emu/shim.so"]


def docker(*args, check=True, **kw):
    r = subprocess.run(["docker", *args], capture_output=True, text=True, **kw)
    if check and r.returncode:
        raise RuntimeError("docker %s failed:\n%s%s" % (args[0], r.stdout[-2000:], r.stderr[-2000:]))
    return r


def docker_available():
    try:
        return docker("version", "--format", "{{.Server.Version}}", check=False, timeout=10).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def source_hash():
    h = hashlib.sha256()
    for path in sorted(SRC.rglob("*")):
        if path.is_file():
            h.update(str(path.relative_to(SRC)).encode() + b"\0" + path.read_bytes())
    return h.hexdigest()[:12]


def build(force=False):
    """Compile the guest shim with the TomTom toolchain and build the container image; cached by source hash."""
    tag = "%s:%s" % (IMAGE, source_hash())
    if not force and docker("image", "inspect", tag, check=False).returncode == 0:
        return tag
    ctx = RUNTIME / "user-build"
    shutil.rmtree(ctx, ignore_errors=True)
    shutil.copytree(SRC, ctx)
    docker("run", "--rm", "--platform", "linux/386", "-v", "%s:/usr/local/cross/gcc-3.3.4_glibc-2.3.2:ro" % TOOLCHAIN,
           "-v", "%s:/work" % ctx, BUILDER, "sh", "-c",
           "cd /work && G=/usr/local/cross/gcc-3.3.4_glibc-2.3.2/bin/arm-linux-gcc && "
           "$G -O2 -Wall -shared -fPIC -static-libgcc -o shim.so shim.c && "
           "$G -O2 -Wall -static-libgcc -o fbdemo fbdemo.c")
    docker("build", "-q", "-t", tag, "-t", IMAGE + ":latest", str(ctx))
    return tag


def latest_dump():
    dumps = sorted(p for p in (ROOT / "artifacts").glob("bootloader-dump-*") if (p / "layout.txt").exists())
    return dumps[-1] if dumps else None


class Emulator:
    """Drives one emulator container. Coordinates are screen pixels, 480x272."""

    def __init__(self, port=8630, name=NAME, telnet_port=2323):
        self.port, self.name, self.telnet_port = port, name, telnet_port
        self.url = "http://127.0.0.1:%d" % port

    # Lifecycle

    def start(self, usb=False, power=True, timeout=30):
        tag = build()
        self.stop()
        LOGS.mkdir(parents=True, exist_ok=True)
        mounts = {ROOT / "build": "/in/build", RUNTIME / "opentom": "/in/opentom",
                  ROOT / "artifacts/opentom-dist-snapshot": "/in/dist", ROOT / "firmware": "/in/firmware"}
        dump = latest_dump()
        if dump:
            mounts[dump] = "/in/dump"
        args = ["run", "-d", "--name", self.name, "--init",
                "--cap-add", "SYS_ADMIN", "--cap-add", "NET_ADMIN", "--cap-add", "SYS_NICE",
                "--security-opt", "apparmor=unconfined", "--tmpfs", "/run:exec,dev,suid,size=768m",
                "-p", "127.0.0.1:%d:8080" % self.port, "-p", "127.0.0.1:%d:23" % self.telnet_port,
                "-e", "HOMTOM_USB=%d" % usb, "-e", "HOMTOM_POWER=%d" % power, "-v", "%s:/logs" % LOGS]
        for src, dest in mounts.items():
            args += ["-v", "%s:%s:ro" % (src, dest)]
        docker(*args, tag)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                return self.state()
            except OSError:
                time.sleep(0.1)
        raise TimeoutError("emulator did not answer on " + self.url)

    def stop(self):
        docker("rm", "-f", self.name, check=False)

    def running(self):
        return docker("inspect", "-f", "{{.State.Running}}", self.name, check=False).stdout.strip() == "true"

    # HTTP API

    def _call(self, path, body=None, raw=False, timeout=15):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.url + path, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            payload = r.read()
            return (payload, r.headers) if raw else json.loads(payload)

    def state(self):
        return self._call("/api/state")

    def power(self, on=None):
        return self._call("/api/power", {} if on is None else {"on": on})

    def reset(self):
        return self._call("/api/reset", {})

    def usb(self, on):
        return self._call("/api/usb", {"on": on})

    def tap(self, x, y, ms=80):
        return self._call("/api/tap", {"x": x, "y": y, "ms": ms})["ok"]

    def touch(self, x, y, down, first=False):
        return self._call("/api/touch", {"x": x, "y": y, "down": down, "first": first})["ok"]

    def screenshot(self, path=None):
        png, _ = self._call("/api/screen.png", raw=True)
        if path:
            Path(path).write_bytes(png)
        return png

    def frame(self):
        """Current screen as a Frame (raw RGB565 from the framebuffer)."""
        data, headers = self._call("/api/fb", raw=True)
        return Frame(data, int(headers["X-Seq"]))

    def wait_for(self, predicate, timeout=30, interval=0.05):
        """Poll frames until predicate(frame) is true; returns the frame."""
        deadline = time.time() + timeout
        frame = self.frame()
        while not predicate(frame):
            if time.time() > deadline:
                raise TimeoutError("screen condition not met within %ss" % timeout)
            time.sleep(interval)
            frame = self.frame()
        return frame

    def wait_for_pixels(self, expected, tolerance=24, timeout=30):
        """expected: {(x, y): (r, g, b)}; every pixel within tolerance per channel."""
        return self.wait_for(lambda f: all(f.close(f.pixel(x, y), rgb, tolerance) for (x, y), rgb in expected.items()),
                             timeout)

    def wait_for_change(self, since, timeout=30, min_fraction=0.0):
        """Wait until the screen differs from frame `since` in more than min_fraction of pixels."""
        return self.wait_for(lambda f: f.diff(since) > min_fraction, timeout)

    # Guest access

    def exec(self, command, timeout=60, check=False):
        """Run a shell command inside the guest chroot with the device's busybox; returns CompletedProcess."""
        return docker("exec", self.name, "chroot", GUEST, "/bin/busybox", "env", "-i", *GUEST_ENV,
                      "/bin/sh", "-c", command, check=check, timeout=timeout)

    def guest_file(self, path):
        return docker("exec", self.name, "cat", GUEST + path, check=False).stdout

    def events(self):
        return (LOGS / "events.log").read_text() if (LOGS / "events.log").exists() else ""


class Frame:
    def __init__(self, data, seq=0):
        self.data, self.seq = data, seq

    def pixel(self, x, y):
        p, = struct.unpack_from("<H", self.data, (y * W + x) * 2)
        return ((p >> 11) * 255 // 31, (p >> 5 & 63) * 255 // 63, (p & 31) * 255 // 31)

    @staticmethod
    def close(a, b, tolerance):
        return all(abs(i - j) <= tolerance for i, j in zip(a, b))

    def diff(self, other):
        """Fraction of pixels that differ."""
        a, b = memoryview(self.data).cast("H"), memoryview(other.data).cast("H")
        return sum(1 for i, j in zip(a, b) if i != j) / (W * H)

    def fraction(self, rgb, tolerance=24, box=(0, 0, W, H)):
        x0, y0, x1, y1 = box
        hits = sum(1 for y in range(y0, y1, 2) for x in range(x0, x1, 2)
                   if self.close(self.pixel(x, y), rgb, tolerance))
        return hits / (((x1 - x0 + 1) // 2) * ((y1 - y0 + 1) // 2))

    def png(self):
        rows = b"".join(b"\0" + b"".join(bytes(self.pixel(x, y)) for x in range(W)) for y in range(H))
        chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8630)
    parser.add_argument("--name", default=NAME)
    sub = parser.add_subparsers(dest="cmd", required=True)
    up = sub.add_parser("up", help="build if needed, start, print the URL")
    up.add_argument("--usb", action="store_true", help="start with the USB cable plugged (storage mode)")
    up.add_argument("--telnet-port", type=int, default=2323)
    up.add_argument("--open", action="store_true", help="open the page in the browser")
    sub.add_parser("down")
    sub.add_parser("build").add_argument("--force", action="store_true")
    sub.add_parser("status")
    sub.add_parser("reset")
    sub.add_parser("power").add_argument("state", nargs="?", choices=["on", "off"])
    sub.add_parser("usb").add_argument("state", choices=["on", "off"])
    sub.add_parser("screenshot").add_argument("out")
    tap = sub.add_parser("tap")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    sub.add_parser("exec").add_argument("command")
    args = parser.parse_args()

    emu = Emulator(args.port, args.name, getattr(args, "telnet_port", 2323))
    if args.cmd == "up":
        t0 = time.time()
        state = emu.start(usb=args.usb)
        print("homtom emulator %s (%s), boot %d, ready in %.1fs" % (emu.url, state["mode"], state["boot_id"],
                                                                 time.time() - t0))
        print("logs: %s   telnet: 127.0.0.1:%d" % (LOGS, emu.telnet_port))
        if args.open:
            subprocess.run(["open", emu.url])
    elif args.cmd == "down":
        emu.stop()
    elif args.cmd == "build":
        print(build(args.force))
    elif args.cmd == "status":
        print(json.dumps(emu.state(), indent=2))
    elif args.cmd == "reset":
        print(json.dumps(emu.reset()))
    elif args.cmd == "power":
        print(json.dumps(emu.power(None if args.state is None else args.state == "on")))
    elif args.cmd == "usb":
        print(json.dumps(emu.usb(args.state == "on")))
    elif args.cmd == "screenshot":
        emu.screenshot(args.out)
        print(args.out)
    elif args.cmd == "tap":
        print(emu.tap(args.x, args.y))
    elif args.cmd == "exec":
        r = emu.exec(args.command)
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        sys.exit(r.returncode)


if __name__ == "__main__":
    main()

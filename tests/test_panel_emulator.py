"""End to end: the ARM panel under qemu-user (phase 0 emulator image) against the mock gateway.

Screenshots and idle measurements land in .runtime/emu/panel-test/.
"""
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import unittest
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = "homtom-emu-phase0"
BUILDER = "tomtom-revival-build:gcc334-host"
TOOLCHAIN = ROOT / "artifacts/source/toolchain/gcc-3.3.4_glibc-2.3.2"
BINARY = ROOT / "panel/build/homtom-panel"
OUT = ROOT / ".runtime/emu/panel-test"
W, H = 480, 272
BLUE = (0x18, 0xBC, 0xF2)
IDLE_SECONDS = int(os.environ.get("HOMTOM_IDLE_SECONDS", "30"))

# Inside each slot's icon disc, below the icon (see panel/src/layout.c and ui.c).
DISC = {0: (41, 83), 1: (198, 83), 2: (395, 83), 3: (41, 202), 4: (198, 202), 5: (355, 202)}
TILE_CENTRE = {1: (240, 90)}


def docker(*args, check=True, timeout=120):
    r = subprocess.run(["docker", *args], capture_output=True, text=True, timeout=timeout)
    if check and r.returncode:
        raise RuntimeError("docker %s: %s" % (args[0], r.stderr[-1500:]))
    return r


def have_image(name):
    try:
        return docker("image", "inspect", name, check=False, timeout=20).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


class RFB:
    """Minimal VNC client: raw 32-bit frames and pointer events."""

    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.take(12)
        self.s.sendall(b"RFB 003.008\n")
        n = self.take(1)[0]
        self.take(n)
        self.s.sendall(b"\x01")
        self.take(4)
        self.s.sendall(b"\x01")
        self.w, self.h = struct.unpack(">HH", self.take(4))
        self.take(16)
        self.take(struct.unpack(">I", self.take(4))[0])
        self.s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
        self.s.sendall(struct.pack(">BxHi", 2, 1, 0))

    def take(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def frame(self):
        self.s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, self.w, self.h))
        px = bytearray(self.w * self.h * 4)
        while True:
            t = self.take(1)[0]
            if t == 2:
                continue
            if t == 3:
                self.take(3 + struct.unpack(">I", self.take(7)[3:])[0])
                continue
            if t == 1:
                _, _, n = struct.unpack(">xHH", self.take(5))
                self.take(n * 6)
                continue
            self.take(1)
            for _ in range(struct.unpack(">H", self.take(2))[0]):
                x, y, rw, rh, enc = struct.unpack(">HHHHi", self.take(12))
                data = self.take(rw * rh * 4)
                for r in range(rh):
                    o = ((y + r) * self.w + x) * 4
                    px[o:o + rw * 4] = data[r * rw * 4:(r + 1) * rw * 4]
            return px

    def pointer(self, x, y, down):
        self.s.sendall(struct.pack(">BBHH", 5, 1 if down else 0, x, y))

    def tap(self, x, y):
        self.pointer(x, y, True)
        time.sleep(0.15)
        self.pointer(x, y, False)

    def swipe(self, x0, x1, y, steps=8):
        for i in range(steps + 1):
            self.pointer(x0 + (x1 - x0) * i // steps, y, True)
            time.sleep(0.04)
        self.pointer(x1, y, False)


def rgb(px, x, y):
    o = (y * W + x) * 4
    return px[o + 2], px[o + 1], px[o]


def near(a, b, tol=24):
    return all(abs(i - j) <= tol for i, j in zip(a, b))


def png(path, px):
    rows = b"".join(b"\0" + bytes(c for x in range(W) for c in rgb(px, x, y)) for y in range(H))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def build_panel():
    sources = list((ROOT / "panel/src").glob("*"))
    if BINARY.exists() and BINARY.stat().st_mtime >= max(p.stat().st_mtime for p in sources):
        return
    subprocess.run([str(ROOT / "panel/build.sh")], check=True, capture_output=True, timeout=300)


def docker_ready():
    return shutil.which("docker") and have_image(IMAGE) and (BINARY.exists() or (
        TOOLCHAIN.exists() and have_image(BUILDER)))


@unittest.skipUnless(docker_ready(), "needs Docker, the %s image and the panel build (or toolchain)" % IMAGE)
class PanelEmulatorTest(unittest.TestCase):
    def setUp(self):
        build_panel()
        self.name = "homtom-panel-test-%d" % os.getpid()
        self.stage = Path(tempfile.mkdtemp(prefix="homtom-panel-"))
        self.addCleanup(shutil.rmtree, self.stage, True)
        (self.stage / "bin").mkdir()
        (self.stage / "etc").mkdir()
        shutil.copy2(BINARY, self.stage / "bin/homtom-panel")
        # The emulator's touch bridge reports screen pixels, so calibration is the identity.
        (self.stage / "etc/pointercal").write_text("65536 0 0 0 65536 0 65536\n")
        docker("rm", "-f", self.name, check=False)
        self.addCleanup(docker, "rm", "-f", self.name, check=False)
        # run.sh copies /opentom to /mnt/sdcard/opentom, the panel's default pointercal location.
        docker("run", "-d", "--name", self.name, "--tmpfs", "/run:exec,size=128m",
               "-p", "127.0.0.1::5900", "-p", "127.0.0.1::8130",
               "-v", "%s:/opentom:ro" % self.stage, "-v", "%s:/gateway:ro" % (ROOT / "gateway"),
               IMAGE, "/mnt/sdcard/opentom/bin/homtom-panel", "-g", "127.0.0.1:8130")
        docker("exec", "-d", self.name, "python3", "/gateway/homtom_gateway.py", "--mock",
               "--bind", "0.0.0.0", "--port", "8130", "--allow", "*")
        ports = docker("port", self.name).stdout
        self.vnc = int(next(l for l in ports.splitlines() if l.startswith("5900")).rsplit(":", 1)[1])
        self.api = "http://127.0.0.1:%s" % next(l for l in ports.splitlines() if l.startswith("8130")).rsplit(":", 1)[1]
        OUT.mkdir(parents=True, exist_ok=True)

    def gateway(self, path, method="GET"):
        req = urllib.request.Request(self.api + path, data=b"" if method == "POST" else None, method=method)
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.read().decode()

    def connect(self, timeout=30):
        deadline = time.time() + timeout
        while True:
            try:
                return RFB(self.vnc)
            except (OSError, EOFError):
                if time.time() > deadline:
                    raise
                time.sleep(0.5)

    def wait_screen(self, vnc, cond, what, timeout=20):
        deadline = time.time() + timeout
        while True:
            px = vnc.frame()
            if cond(px):
                return px
            if time.time() > deadline:
                png(OUT / "failure.png", px)
                self.fail("screen never showed %s (see %s, logs: %s)" % (
                    what, OUT / "failure.png", docker("logs", self.name, check=False).stderr[-800:]))
            time.sleep(0.3)

    def wait_calls(self, cond, timeout=10):
        deadline = time.time() + timeout
        while True:
            calls = json.loads(self.gateway("/v1/debug/calls"))
            if cond(calls) or time.time() > deadline:
                return calls
            time.sleep(0.2)

    def panel_process(self):
        script = ("import os\nfor p in os.listdir('/proc'):\n if p.isdigit():\n"
                  "  try: c=open('/proc/%s/cmdline'%p,'rb').read()\n  except OSError: continue\n"
                  "  if b'homtom-panel' in c and b'python' not in c: print(p)")
        pids = docker("exec", self.name, "python3", "-c", script).stdout.split()
        self.assertEqual(len(pids), 1, pids)
        return pids[0]

    def sample(self, pid):
        stat = docker("exec", self.name, "cat", "/proc/%s/stat" % pid).stdout.rsplit(")", 1)[1].split()
        status = docker("exec", self.name, "cat", "/proc/%s/status" % pid).stdout
        rss = next(int(l.split()[1]) for l in status.splitlines() if l.startswith("VmRSS:"))
        return int(stat[11]) + int(stat[12]), rss

    def test_tap_reaches_the_mock_and_the_tile_redraws(self):
        vnc = self.connect()
        first = self.wait_screen(vnc, lambda px: near(rgb(px, *DISC[0]), BLUE) and not near(rgb(px, *DISC[1]), BLUE),
                                 "the first page with the living room light on")
        png(OUT / "01-panel.png", first)

        vnc.tap(*TILE_CENTRE[1])
        calls = self.wait_calls(bool)
        self.assertEqual([(c["domain"], c["service"], c["data"]) for c in calls],
                         [("switch", "toggle", {"entity_id": "switch.coffee_machine"})])
        toggled = self.wait_screen(vnc, lambda px: near(rgb(px, *DISC[1]), BLUE), "the coffee machine tile on")
        png(OUT / "02-after-tap.png", toggled)
        self.assertIn("switch.coffee_machine\tswitch\tt\t1\tOn", self.gateway("/v1/panel"))

        # The climate tile's right third steps the target up.
        vnc.tap(446, 67)
        self.assertTrue(self.wait_calls(lambda calls: any(c["service"] == "set_temperature" for c in calls)))
        self.assertIn("Heat · 22°", self.gateway("/v1/panel"))

        # A change made elsewhere arrives over the long poll.
        self.gateway("/v1/debug/state/light.kitchen?state=on", "POST")
        pushed = self.wait_screen(vnc, lambda px: near(rgb(px, *DISC[5]), BLUE), "the kitchen tile on after a push")
        png(OUT / "03-after-push.png", pushed)

        vnc.swipe(400, 80, 200)
        page2 = self.wait_screen(vnc, lambda px: not near(rgb(px, *DISC[0]), BLUE) and near(rgb(px, *DISC[1]), BLUE),
                                 "page two (hallway off, garden pump on)")
        png(OUT / "04-page-two.png", page2)
        vnc.swipe(80, 400, 200)
        self.wait_screen(vnc, lambda px: near(rgb(px, *DISC[0]), BLUE), "page one again")

        pid = self.panel_process()
        time.sleep(3)
        ticks0, _ = self.sample(pid)
        started = time.time()
        time.sleep(IDLE_SECONDS)
        ticks1, rss = self.sample(pid)
        elapsed = time.time() - started
        hz = int(docker("exec", self.name, "getconf", "CLK_TCK").stdout.strip() or 100)
        cpu = (ticks1 - ticks0) / hz / elapsed * 100
        result = dict(idle_seconds=round(elapsed, 1), cpu_percent=round(cpu, 2), qemu_rss_kib=rss,
                      binary_bytes=BINARY.stat().st_size)
        (OUT / "idle.json").write_text(json.dumps(result, indent=2) + "\n")
        print("\npanel idle under qemu-user:", json.dumps(result))
        self.assertLess(cpu, 2.0, "panel is not idle: %s" % result)

        # Holding the top-left corner exits with status 10, the launcher's cue to start OpenTom.
        vnc.pointer(30, 15, True)
        time.sleep(3)
        vnc.pointer(30, 15, False)
        code = docker("wait", self.name, timeout=20).stdout.strip()
        self.assertEqual(code, "10")


if __name__ == "__main__":
    unittest.main()

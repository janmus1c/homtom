"""Userspace emulator supervisor: boots homtom's /etc/rc under qemu-user in a chroot and serves the screen.

Runs as PID 1's child in the container. One board: power, reset, a USB cable and a 480x272 screen.
Inputs are mounted read-only under /in (see emulator/emu.py); logs go to /logs.
"""
import io
import json
import os
import shutil
import signal
import stat
import struct
import subprocess
import sys
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import numpy
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, "/in/firmware")
import ttimage  # noqa: E402

W, H = 480, 272
FB_BYTES = W * H * 2
IN = Path("/in")
LOGS = Path("/logs")
EMU = Path("/run/emu")
ROOT = EMU / "rootfs"
DISK = EMU / "disk"
IMAGES = EMU / "images"
INTERNAL = DISK / "mmcblk1"
SHIMS = {"/bin/mount": "mount", "/sbin/insmod": "insmod", "/sbin/rmmod": "rmmod", "/sbin/reboot": "reboot"}
GUEST_ENV = ["HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", "LD_PRELOAD=/.emu/shim.so"]
WEB = Path(__file__).with_name("index.html")


def log(msg):
    line = "%.3f %s\n" % (time.time(), msg)
    with open(LOGS / "supervisor.log", "a") as f:
        f.write(line)


def run(*args):
    return subprocess.run(args, capture_output=True, text=True)


def module_name(data):
    """Name from .gnu.linkonce.this_module (struct module: state, list_head, name[60])."""
    try:
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        sections = [struct.unpack_from("<IIIIII", data, shoff + i * shentsize) for i in range(shnum)]
        names = sections[shstrndx][4]
        for name, _, _, _, offset, size in sections:
            if data[names + name:].split(b"\0", 1)[0] == b".gnu.linkonce.this_module":
                return data[offset + 12:offset + 72].split(b"\0", 1)[0].decode()
    except (struct.error, IndexError, UnicodeDecodeError):
        pass
    return None


def device_start_sh(text):
    """OpenTom start.sh as tools/homtom-install-storage leaves it on the device."""
    text = text.replace("power_button -b bin/suspend bin/suspend &", "# homtom: suspend disabled to keep USB up")
    return text.replace('if [ -f "$DIST/bin/lab-connectivity" ]; then', "if false; then # homtom: lab helper retired")


class Board:
    def __init__(self):
        self.lock = threading.RLock()
        self.frame = threading.Condition()
        self.power = False
        self.usb = False
        self.mode = "off"
        self.boot_id = 0
        self.boot_started = 0.0
        self.seq = 0
        self.fb = b"\0" * FB_BYTES
        self.ts = None
        self.wdt_deadline = None
        self.events_pos = 0
        self.rc = None
        self.calib = None

    # Power and boot

    def power_on(self):
        with self.lock:
            log("power on")
            self.kill_guest()
            self.make_disks()
            self.power = True
            self.boot()

    def power_off(self):
        with self.lock:
            log("power off")
            self.kill_guest()
            self.power = False
            self.mode = "off"
            self.blank()

    def reset(self):
        with self.lock:
            if not self.power:
                return self.power_on()
            log("reset")
            self.kill_guest()
            self.boot()

    def set_usb(self, on):
        with self.lock:
            if on == self.usb:
                return
            self.usb = on
            log("usb %s" % ("plugged" if on else "unplugged"))
            self.event_log("usb %d" % on)
            if not on and self.mode == "storage":
                self.kill_guest()
                self.boot()

    def boot(self):
        try:
            self._boot()
        except Exception:
            self.mode = "failed"
            log("boot %d failed:\n%s" % (self.boot_id, traceback.format_exc()))

    def _boot(self):
        self.boot_id += 1
        self.boot_started = time.time()
        self.wdt_deadline = None
        self.events_pos = 0
        if self.usb:
            self.mode = "storage"
            log("boot %d: USB connected at reset, bootloader storage mode" % self.boot_id)
            self.storage_screen()
            return
        self.mode = "booting"
        t0 = time.time()
        self.prepare_rootfs()
        log("boot %d: rootfs ready in %.2fs" % (self.boot_id, time.time() - t0))
        console = open(LOGS / "console.log", "ab")
        console.write(b"\n==== boot %d %s ====\n" % (self.boot_id, time.strftime("%Y-%m-%dT%H:%M:%S").encode()))
        console.flush()
        self.rc = subprocess.Popen(["chroot", str(ROOT), "/bin/busybox", "env", "-i"] + GUEST_ENV + ["/init"],
                                   cwd=str(ROOT), stdin=subprocess.DEVNULL, stdout=console, stderr=console,
                                   start_new_session=True)
        console.close()
        self.mode = "running"

    def guest_pids(self):
        pids = []
        for p in Path("/proc").iterdir():
            if p.name.isdigit():
                try:
                    if os.readlink(p / "root") == str(ROOT):
                        pids.append(int(p.name))
                except OSError:
                    pass
        return pids

    def kill_guest(self):
        for _ in range(50):
            pids = self.guest_pids()
            if not pids:
                break
            for pid in pids:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            time.sleep(0.02)
        if self.rc:
            self.rc.wait()
            self.rc = None
        mounts = [line.split()[4] for line in open("/proc/self/mountinfo")]
        for target in reversed(mounts):
            if target.startswith(str(ROOT) + "/") or target == str(ROOT):
                run("umount", "-l", target)
        if self.ts is not None:
            os.close(self.ts)
            self.ts = None
        run("ip", "link", "del", "usb0")

    def make_disks(self):
        shutil.rmtree(DISK, ignore_errors=True)
        shutil.rmtree(IMAGES, ignore_errors=True)
        opentom = INTERNAL / "opentom"
        for name in ("bin", "lib", "etc"):
            shutil.copytree(IN / "opentom" / name, opentom / name, symlinks=True)
        if (IN / "dist/share").is_dir():
            shutil.copytree(IN / "dist/share", opentom / "share", symlinks=True)
        (opentom / "start.sh").write_text(device_start_sh((IN / "dist/start.sh").read_text()))
        (opentom / "logs").mkdir()
        shutil.copy(IN / "build/ttsystem", INTERNAL / "ttsystem")
        (DISK / "mmcblk0p1").mkdir(parents=True)
        IMAGES.mkdir(parents=True)
        dump = IN / "dump"
        for name in ("mtd0-before-flash.bin", "mtd0.bin", "mtd0-firstboot.bin"):
            if (dump / name).exists():
                shutil.copy(dump / name, IMAGES / "mtd0")
                break
        for name in ("mmcblk0", "mmcblk1"):
            if (dump / (name + "-head.bin")).exists():
                os.symlink(dump / (name + "-head.bin"), IMAGES / name)

    def layout(self):
        """/proc/partitions and /proc/mtd as the device reported them (homtom dump.sh layout.txt)."""
        lines = (IN / "dump/layout.txt").read_text().splitlines() if (IN / "dump/layout.txt").exists() else []
        end = next((i for i, l in enumerate(lines) if l.startswith("dev:")), 0)
        partitions = lines[:end]
        mtd = [l for l in lines if l.startswith("dev:") or l.startswith("mtd")]
        if not partitions:
            partitions = ["major minor  #blocks  name", "", "  31     0        512 mtdblock0",
                          " 250     8    2007040 mmcblk1"]
        if not mtd:
            mtd = ["dev:    size   erasesize  name", 'mtd0: 00080000 00010000 "phys_mapped_flash"']
        return "\n".join(partitions) + "\n", "\n".join(mtd) + "\n"

    def prepare_rootfs(self):
        shutil.rmtree(ROOT, ignore_errors=True)
        ROOT.mkdir(parents=True)
        image = (IN / "build/ttsystem").read_bytes()
        modnames = []
        for e in ttimage.initramfs_entries(image):
            path = ROOT / e["name"].lstrip("/")
            kind = stat.S_IFMT(e["mode"])
            perm = stat.S_IMODE(e["mode"])
            if path == ROOT:
                continue
            if kind == stat.S_IFDIR:
                path.mkdir(parents=True, exist_ok=True)
                path.chmod(perm)
            elif kind == stat.S_IFLNK:
                os.symlink(e["data"].rstrip(b"\0").decode(), path)
            elif kind in (stat.S_IFCHR, stat.S_IFBLK, stat.S_IFIFO):
                os.mknod(path, e["mode"], os.makedev(e["rmaj"], e["rmin"]))
            else:
                path.write_bytes(e["data"])
                path.chmod(perm)
                if e["name"].endswith(".ko"):
                    name = module_name(e["data"])
                    if name:
                        modnames.append("%s %s\n" % (e["name"], name))
        self.install_shims(modnames)

    def install_shims(self, modnames):
        emu = ROOT / ".emu"
        for sub in ("log", "disk", "images", "proc/barcelona"):
            (emu / sub).mkdir(parents=True, exist_ok=True)
        for sub in ("log", "disk", "images"):
            source = {"log": LOGS, "disk": DISK, "images": IMAGES}[sub]
            run("mount", "--bind", str(source), str(emu / sub))
        here = Path(__file__).parent
        shutil.copy(here / "shim.so", emu / "shim.so")
        shutil.copy(here / "guest/lib.sh", emu / "lib.sh")
        for target, name in SHIMS.items():
            dest = ROOT / target.lstrip("/")
            if dest.is_symlink() or dest.exists():
                dest.unlink()
            shutil.copy(here / "guest" / name, dest)
            dest.chmod(0o755)
        (emu / "modnames").write_text("".join(modnames))
        (emu / "modules").write_text("")
        (emu / "events").write_text("")
        partitions, mtd = self.layout()
        (emu / "proc/partitions").write_text(partitions)
        (emu / "proc/mtd").write_text(mtd)
        (emu / "proc/cmdline").write_text("root=/dev/mmcblk0p2\n")
        for line in (Path(__file__).parent / "barcelona.txt").read_text().splitlines():
            if line and not line.startswith("#"):
                name, _, value = line.partition(" ")
                (emu / "proc/barcelona" / name).write_text(value + "\n")

        dev = ROOT / "dev"
        for name in ("fb", "fb0", "input/event0", "watchdog", "mtd0", "mmcblk0", "mmcblk1", "ptmx"):
            p = dev / name
            if p.is_symlink() or p.exists():
                p.unlink()
        (dev / "homtom-fb").write_bytes(self.bootloader_frame())
        os.symlink("homtom-fb", dev / "fb")
        os.mkfifo(dev / "homtom-ts")
        os.symlink("../homtom-ts", dev / "input/event0")
        (dev / "homtom-wdt").write_bytes(b"")
        os.symlink("homtom-wdt", dev / "watchdog")
        os.symlink("pts/ptmx", dev / "ptmx")
        for name, major, minor in (("null", 1, 3), ("zero", 1, 5), ("random", 1, 8), ("urandom", 1, 9),
                                   ("tty", 5, 0), ("console", 5, 1)):
            if not (dev / name).exists():
                os.mknod(dev / name, 0o20666, os.makedev(major, minor))
        if (IMAGES / "mtd0").exists():
            os.symlink("/.emu/images/mtd0", dev / "mtd0")
        for name in ("mmcblk0", "mmcblk1"):
            if (IMAGES / name).exists():
                os.symlink("/.emu/images/" + name, dev / name)
        self.ts = os.open(dev / "homtom-ts", os.O_RDWR | os.O_NONBLOCK)
        self.fake_sys()

    def fake_sys(self):
        sys_ = ROOT / "sys"
        devices = sys_ / "devices/platform"
        for path, files in (("s3c-hsmmc/mmc1:0001", {"cid": "", "name": "MMC02G\n", "type": "MMC\n"}),
                            ("s3c-sdi/mmc0:59b4", {"cid": "", "scr": "0225000000000000\n", "name": "SQ08G\n",
                                                    "type": "SD\n"}),
                            ("s3c-hsudc", {}), ("s3c2410-ts", {})):
            (devices / path).mkdir(parents=True, exist_ok=True)
            for name, value in files.items():
                (devices / path / name).write_text(value)
        (sys_ / "bus/platform/devices").mkdir(parents=True)
        for name in ("s3c-hsmmc", "s3c-sdi", "s3c-hsudc", "s3c2410-ts"):
            os.symlink("../../../devices/platform/" + name, sys_ / "bus/platform/devices" / name)
        for blk, target in (("mmcblk1", "s3c-hsmmc/mmc1:0001"), ("mmcblk0", "s3c-sdi/mmc0:59b4")):
            (sys_ / "block" / blk).mkdir(parents=True)
            os.symlink("../../devices/platform/" + target, sys_ / "block" / blk / "device")
        backlight = sys_ / "class/backlight/s3c"
        backlight.mkdir(parents=True)
        (backlight / "brightness").write_text("100\n")
        (backlight / "max_brightness").write_text("100\n")

    # Screen

    def bootloader_frame(self):
        """The bootloader's splash stays in the LFB the kernel inherits (ATAG_VIDEOLFB)."""
        for path in (IN / "build/bootsplash/splashw.bmp", INTERNAL / "splashw.bmp"):
            if path.exists():
                try:
                    return to_rgb565(Image.open(path).convert("RGB").resize((W, H)))
                except OSError:
                    pass
        return b"\0" * FB_BYTES

    def blank(self):
        self.show(b"\0" * FB_BYTES)

    def show(self, data):
        with self.frame:
            if data != self.fb:
                self.fb = data
                self.seq += 1
                self.frame.notify_all()

    def storage_screen(self):
        img = Image.new("RGB", (W, H), (0x0E, 0x16, 0x1E))
        draw = ImageDraw.Draw(img)
        font = ImageFont.load_default(size=22)
        small = ImageFont.load_default(size=14)
        draw.text((W // 2, 110), "USB storage mode", fill=(0x18, 0xBC, 0xF2), font=font, anchor="mm")
        draw.text((W // 2, 150), "emulated bootloader - unplug USB to boot", fill=(200, 210, 220), font=small,
                  anchor="mm")
        self.show(to_rgb565(img))

    def watch_screen(self):
        fbfile = ROOT / "dev/homtom-fb"
        while True:
            time.sleep(0.03)
            if self.mode in ("running", "booting"):
                try:
                    with open(fbfile, "rb") as f:
                        data = f.read(FB_BYTES)
                except OSError:
                    continue
                if len(data) == FB_BYTES:
                    self.show(data)

    def png(self, data=None):
        buf = io.BytesIO()
        to_image(data or self.fb).save(buf, "PNG", compress_level=1)
        return buf.getvalue()

    # Touch

    def calibration(self):
        try:
            text = (INTERNAL / "opentom/etc/pointercal").read_text().replace("\0", " ")
            values = [int(v) for v in text.split()[:7]]
            if len(values) == 7 and values[6]:
                return values
        except (OSError, ValueError):
            pass
        return [65536, 0, 0, 0, 65536, 0, 65536]

    def raw(self, x, y):
        """Inverse of tslib's linear module, so the guest's own pointercal maps the tap back to (x, y)."""
        a = self.calibration()
        bx, by = x * a[6] - a[2], y * a[6] - a[5]
        det = a[0] * a[4] - a[1] * a[3]
        return round((bx * a[4] - a[1] * by) / det), round((a[0] * by - a[3] * bx) / det)

    def touch(self, x, y, down, first=False):
        if self.ts is None:
            return False
        now = time.time()
        sec, usec = int(now), int(now % 1 * 1e6)
        events = []
        if down:
            rx, ry = self.raw(x, y)
            events += [(3, 0, rx), (3, 1, ry), (3, 0x18, 200)]
            if first:
                events.append((1, 0x14A, 1))
        else:
            events += [(1, 0x14A, 0), (3, 0x18, 0)]
        events.append((0, 0, 0))
        try:
            os.write(self.ts, b"".join(struct.pack("<iiHHi", sec, usec, *e) for e in events))
        except BlockingIOError:
            return False
        return True

    def tap(self, x, y, ms=80):
        self.touch(x, y, True, first=True)
        for _ in range(max(1, ms // 20)):
            time.sleep(0.02)
            self.touch(x, y, True)
        return self.touch(x, y, False)

    # Guest events: module loads, reboot, watchdog

    def event_log(self, line):
        with open(LOGS / "events.log", "a") as f:
            f.write("%.3f boot=%d %s\n" % (time.time(), self.boot_id, line))

    def watch_events(self):
        while True:
            time.sleep(0.05)
            with self.lock:
                if self.mode != "running":
                    continue
                if self.rc and self.rc.poll() is not None:
                    log("boot %d: /init exited with %s" % (self.boot_id, self.rc.returncode))
                    self.rc = None
                try:
                    with open(ROOT / ".emu/events") as f:
                        f.seek(self.events_pos)
                        lines = f.read().splitlines()
                        self.events_pos = f.tell()
                except OSError:
                    continue
                for line in lines:
                    self.event_log(line)
                    self.handle(line.split())
                if self.wdt_deadline and time.time() > self.wdt_deadline:
                    log("boot %d: watchdog expired, reset" % self.boot_id)
                    self.event_log("watchdog reset")
                    self.reset()

    def handle(self, words):
        if not words:
            return
        if words[0] == "insmod" and words[1] == "g_ether":
            run("ip", "link", "add", "usb0", "type", "dummy")
        elif words[0] == "rmmod" and words[1] == "g_ether":
            run("ip", "link", "del", "usb0")
        elif words[0] == "reboot":
            threading.Thread(target=self.reset, daemon=True).start()
        elif words[0] in ("wdt-open", "wdt-timeout", "wdt-keepalive"):
            self.wdt_deadline = time.time() + int(words[1])

    def mirror_logs(self):
        while True:
            time.sleep(2)
            for sub in ("opentom/logs", "homtom/logs"):
                src = INTERNAL / sub
                if not src.is_dir():
                    continue
                dest = LOGS / "guest" / sub.split("/")[0]
                dest.mkdir(parents=True, exist_ok=True)
                for f in src.iterdir():
                    try:
                        if f.is_file() and (not (dest / f.name).exists() or
                                            f.stat().st_mtime > (dest / f.name).stat().st_mtime):
                            shutil.copy2(f, dest / f.name)
                    except OSError:
                        pass

    def state(self):
        return dict(power=self.power, usb=self.usb, mode=self.mode, boot_id=self.boot_id,
                    boot_started=self.boot_started, uptime=time.time() - self.boot_started if self.power else 0,
                    seq=self.seq, watchdog=self.wdt_deadline)


def to_image(data):
    p = numpy.frombuffer(data, "<u2").reshape(H, W).astype(numpy.uint32)
    rgb = numpy.stack([(p >> 11) * 255 // 31, (p >> 5 & 63) * 255 // 63, (p & 31) * 255 // 31], axis=-1)
    return Image.fromarray(rgb.astype(numpy.uint8), "RGB")


def to_rgb565(img):
    a = numpy.asarray(img.convert("RGB"), dtype=numpy.uint16)
    return ((a[..., 0] >> 3) << 11 | (a[..., 1] >> 2) << 5 | a[..., 2] >> 3).astype("<u2").tobytes()


board = Board()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, body, ctype="application/json", code=200, headers=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def query(self):
        from urllib.parse import parse_qs, urlparse
        return {k: v[-1] for k, v in parse_qs(urlparse(self.path).query).items()}

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/":
            return self.reply(WEB.read_bytes(), "text/html; charset=utf-8")
        if path == "/api/state":
            return self.reply(board.state())
        if path == "/api/screen.png":
            return self.reply(board.png(), "image/png", headers={"X-Seq": str(board.seq)})
        if path == "/api/fb":
            return self.reply(board.fb, "application/octet-stream", headers={"X-Seq": str(board.seq)})
        if path == "/api/frame":
            after = int(self.query().get("after", -1))
            with board.frame:
                board.frame.wait_for(lambda: board.seq != after, timeout=10)
                seq, data = board.seq, board.fb
            return self.reply(board.png(data), "image/png", headers={"X-Seq": str(seq)})
        self.reply({"error": "not found"}, code=404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(length) or b"{}")
        path = self.path.split("?", 1)[0]
        if path == "/api/power":
            on = body.get("on", not board.power)
            board.power_on() if on else board.power_off()
        elif path == "/api/reset":
            board.reset()
        elif path == "/api/usb":
            board.set_usb(bool(body.get("on", not board.usb)))
        elif path == "/api/touch":
            ok = board.touch(int(body["x"]), int(body["y"]), bool(body["down"]), bool(body.get("first")))
            return self.reply({"ok": ok})
        elif path == "/api/tap":
            ok = board.tap(int(body["x"]), int(body["y"]), int(body.get("ms", 80)))
            return self.reply({"ok": ok})
        else:
            return self.reply({"error": "not found"}, code=404)
        self.reply(board.state())


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    for name in ("console.log", "supervisor.log", "events.log"):
        (LOGS / name).write_text("")
    shutil.rmtree(LOGS / "guest", ignore_errors=True)
    board.usb = os.environ.get("HOMTOM_USB") == "1"
    for target in (board.watch_screen, board.watch_events, board.mirror_logs):
        threading.Thread(target=target, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", 8080), Handler)
    server.daemon_threads = True
    log("serving on :8080")
    if os.environ.get("HOMTOM_POWER", "1") == "1":
        threading.Thread(target=board.power_on, daemon=True).start()
    server.serve_forever()


if __name__ == "__main__":
    main()

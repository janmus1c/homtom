#!/usr/bin/env python3
"""Build a homtom ttsystem from the OpenTom base image, the HS-UDC modules and the overlay."""
import argparse
import gzip
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "firmware"))
import ttimage  # noqa: E402

VERSION = "0.1.0"
BASE = ROOT / "artifacts/backups/first-INTERNAL/ttsystem"
MODULES = ROOT / "artifacts/kernel-builds/hsudc-v9"
ALT_MODULE = ROOT / "artifacts/kernel-builds/hsudc-v8/s3c-udc-hs.ko"
GADGET = "/lib/modules/2.6.13/kernel/drivers/usb/gadget/"
MKTTIMAGE = ROOT / "tools/ttimage/mkttimage"
OVERLAY = ROOT / "firmware/overlay"

STOCK_USB = """# Starting USB Ether Gadget ...
insmod /lib/modules/$KVER/kernel/drivers/usb/gadget/s3c24xx_udc.ko
insmod /lib/modules/$KVER/kernel/drivers/usb/gadget/g_ether.ko
sleep 1
ifconfig usb0 192.168.1.10
# DHCPD Server
udhcpd -f /etc/udhcpd.conf &

# Starting telnet daemon ...
( cd $DIST ; /usr/sbin/telnetd -F & )
"""
HOMTOM_USB = """# homtom: USB Ethernet, Telnet and FTP
cat /etc/homtom/splash.fb >/dev/fb 2>/dev/null
cat /etc/homtom-release
sh /etc/homtom/network.sh
sh /etc/homtom/dump.sh
sh /etc/homtom/addons.sh
"""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def put(entries, name, data, mode=0o100644):
    names = {entry["name"].rstrip("/") for entry in entries}
    parent = name.rsplit("/", 1)[0]
    missing = []
    while parent and parent not in names and "/" + parent.lstrip("/") not in names:
        missing.append(parent)
        parent = parent.rsplit("/", 1)[0]
    for directory in reversed(missing):
        entries.append(dict(name=directory, mode=0o40755, data=b""))
    for entry in entries:
        if entry["name"].lstrip("/") == name.lstrip("/"):
            entry["data"], entry["mode"] = data, mode
            return
    entries.append(dict(name=name, mode=mode, data=data))


def build(out_dir):
    base = BASE.read_bytes()
    secs, entry = ttimage.sections(base)
    kernel = next(data for address, data in secs if address == 0x31700000)
    entries = ttimage.initramfs_entries(base)

    rc = next(e for e in entries if e["name"].lstrip("/") == "etc/rc")
    text = rc["data"].decode()
    if STOCK_USB not in text:
        raise SystemExit("etc/rc USB block not found; base image changed")
    text = text.replace(STOCK_USB, HOMTOM_USB)
    text = text.replace("echo Runnning \\(ttsystem\\) etc/rc ...\n",
                        "cat /etc/homtom/splash.fb >/dev/fb 2>/dev/null\necho homtom: running etc/rc\n", 1)
    rc["data"] = text.encode()

    put(entries, "/lib/modules/2.6.13/kernel/drivers/usb/gadget/s3c-udc-hs.ko",
        (MODULES / "s3c-udc-hs.ko").read_bytes())
    put(entries, GADGET + "g_ether.ko", (MODULES / "g_ether.ko").read_bytes())
    put(entries, GADGET + "s3c-udc-hs-alt.ko", ALT_MODULE.read_bytes())
    for path in sorted(OVERLAY.rglob("*")):
        if path.is_file():
            mode = 0o100755 if path.stat().st_mode & 0o111 else 0o100644
            put(entries, "/" + str(path.relative_to(OVERLAY)), path.read_bytes(), mode)

    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip() or "unknown"
    release = "homtom %s (build %s) for TomTom GO 630\n" % (VERSION, commit)
    put(entries, "/etc/homtom-release", release.encode())
    put(entries, "/etc/motd", ("\n  " + release + "  USB 192.168.1.10 · telnet · ftp\n\n").encode())

    out_dir.mkdir(parents=True, exist_ok=True)
    initramfs = gzip.compress(ttimage.write_cpio(entries), compresslevel=9, mtime=0)
    (out_dir / "initramfs.cpio.gz").write_bytes(initramfs)
    (out_dir / "zImage").write_bytes(kernel)
    image = subprocess.run([str(MKTTIMAGE), str(out_dir / "initramfs.cpio.gz"), str(out_dir / "zImage")],
                           capture_output=True, check=True).stdout
    if ttimage.initramfs_entries(image) is None:
        raise SystemExit("rebuilt image does not parse")
    if len(initramfs) >= 0x31700000 - 0x31000000:
        raise SystemExit("initramfs overlaps the kernel load address")
    (out_dir / "ttsystem").write_bytes(image)
    manifest = dict(name="homtom", version=VERSION, build=commit, ttsystem_sha256=sha256(image),
                    ttsystem_bytes=len(image), base_sha256=sha256(base),
                    kernel_sha256=sha256(kernel), initramfs_sha256=sha256(initramfs),
                    entry=hex(entry), files=len(entries))
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    print(json.dumps(build(parser.parse_args().out), indent=2))

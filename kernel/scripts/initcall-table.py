#!/usr/bin/env python3
"""Map initcall global index (as drawn by the homtom LCD counter) to a function name.

Usage: initcall-table.py [index ...]   (no args: print the whole table)
Reads .runtime/linux-6.1/vmlinux and artifacts/kernel-builds/homtom-6.1/System.map.
"""
import bisect
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VMLINUX = ROOT / ".runtime/linux-6.1/vmlinux"
SYSMAP = ROOT / "artifacts/kernel-builds/homtom-6.1/System.map"


def load_map():
    syms = []
    marks = {}
    for line in SYSMAP.read_text().splitlines():
        a, ty, n = line.split()[:3]
        a = int(a, 16)
        marks[n] = a
        if ty in "tTwW":
            syms.append((a, n))
    syms.sort()
    return syms, marks


def elf_read(path, vaddr, size):
    d = path.read_bytes()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    for i in range(shnum):
        _n, _t, _f, addr, off, sz = struct.unpack_from("<IIIIII", d, shoff + i * shentsize)
        if addr and addr <= vaddr < addr + sz:
            o = off + (vaddr - addr)
            return d[o:o + size]
    raise SystemExit(f"vaddr {vaddr:#x} not in any section")


def table():
    syms, marks = load_map()
    start, end = marks["__initcall_start"], marks["__initcall_end"]
    raw = elf_read(VMLINUX, start, end - start)
    addrs = [a for a, _ in syms]
    out = []
    for i in range(0, len(raw), 4):
        v = struct.unpack_from("<i", raw, i)[0]
        fn = (start + i + v) & 0xFFFFFFFF if abs(v) < 0x10000000 else v & 0xFFFFFFFF
        j = bisect.bisect_right(addrs, fn) - 1
        name = syms[j][1] if j >= 0 and fn - syms[j][0] < 0x400 else f"?{fn:#x}"
        out.append((fn, name))
    return out


if __name__ == "__main__":
    t = table()
    if sys.argv[1:]:
        for a in sys.argv[1:]:
            k = int(a, 0)
            print(k, t[k][1] if 0 <= k < len(t) else "(out of range)")
    else:
        for k, (fn, name) in enumerate(t):
            print(k, f"{fn:#010x}", name)

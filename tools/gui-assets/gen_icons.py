#!/usr/bin/env python3
"""Rasterise Material Design Icons (Pictogrammers, icons Apache 2.0) into packed alpha bitmaps for the gui.

Standard library only. Reads the SVG files of the @mdi/svg npm package (one <path d="..."> on a 24x24 viewBox),
fills them with the nonzero rule at 16 sub-scanlines per pixel with exact horizontal coverage, quantises the
coverage to 1, 2 or 4 bits and writes:

  --header FILE   C tables: one bitmap blob, a gui_icon table and an enum of ICON_<NAME>_<SIZE> indexes
  --sheet FILE    contact sheet PNG (every icon at every size, 3x zoom, on the dark card colour)
  --png-dir DIR   one white RGBA PNG per icon and size, alpha = quantised coverage (for mockups)

usage:
  npm pack @mdi/svg@7.4.47 && tar xzf mdi-svg-7.4.47.tgz      # once; gives package/svg/*.svg
  python3 -I tools/gui-assets/gen_icons.py --svg-dir package/svg --sizes 20,24,32 --bpp 2 \
      --header build/gui-assets/icon_data.h --sheet build/gui-assets/icons.png

Bitmap layout: row-major, rows padded to a whole byte, most significant bits first. A 2 bpp value v means
coverage v/3; the renderer blends the icon colour over the background with alpha16 = v * 16 / 3.
"""
import argparse
import math
import os
import re
import struct
import sys
import zlib

ICONS = [
    "home", "home-outline", "lightbulb", "lightbulb-outline", "lightbulb-off-outline", "ceiling-light", "desk-lamp",
    "floor-lamp", "led-strip-variant", "thermometer", "thermostat", "water-percent", "molecule-co2", "motion-sensor",
    "motion-sensor-off", "account", "account-outline", "radiator", "radiator-off", "fire", "air-conditioner",
    "snowflake", "power", "chevron-left", "chevron-right", "arrow-left", "minus", "plus", "check", "close", "cog",
    "crosshairs-gps", "bluetooth", "microphone", "volume-high", "volume-off", "brightness-6", "palette",
    "chair-rolling", "sofa", "bed", "silverware-fork-knife", "wifi", "wifi-off", "battery", "battery-charging",
    "usb", "harddisk", "restart", "sync", "alert-circle-outline", "cloud-off-outline", "information-outline",
    "white-balance-sunny",
]

NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


class Scanner:
    def __init__(self, d):
        self.s, self.i = d, 0

    def skip(self):
        while self.i < len(self.s) and self.s[self.i] in " \t\r\n,":
            self.i += 1

    def command(self):
        self.skip()
        if self.i < len(self.s) and self.s[self.i].isalpha():
            self.i += 1
            return self.s[self.i - 1]
        return None

    def more(self):
        self.skip()
        return self.i < len(self.s) and not self.s[self.i].isalpha()

    def number(self):
        self.skip()
        m = NUM.match(self.s, self.i)
        if not m:
            raise ValueError("number expected at %d: %r" % (self.i, self.s[self.i:self.i + 12]))
        self.i = m.end()
        return float(m.group())

    def flag(self):
        self.skip()
        c = self.s[self.i]
        if c not in "01":
            raise ValueError("arc flag expected at %d" % self.i)
        self.i += 1
        return c == "1"


def cubic(p0, p1, p2, p3, n):
    out = []
    for k in range(1, n + 1):
        t = k / n
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                    u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return out


def quad(p0, p1, p2, n):
    out = []
    for k in range(1, n + 1):
        t = k / n
        u = 1 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return out


def arc(p0, rx, ry, phi_deg, large, sweep, p1):
    """SVG 1.1 implementation notes F.6.5/F.6.6: endpoint to centre parameterisation, then flatten."""
    if p0 == p1:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [p1]
    phi = math.radians(phi_deg)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1, y1 = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cx1, cy1 = co * rx * y1 / ry, -co * ry * x1 / rx
    cx = cp * cx1 - sp * cy1 + (p0[0] + p1[0]) / 2
    cy = sp * cx1 + cp * cy1 + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    dt = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(4, int(math.ceil(abs(dt) / (math.pi / 16))))
    out = []
    for k in range(1, n + 1):
        t = t1 + dt * k / n
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        out.append((cp * ex - sp * ey + cx, sp * ex + cp * ey + cy))
    out[-1] = p1
    return out


def parse_path(d, n=12):
    """Return a list of closed polygons (lists of points) in viewBox units."""
    sc = Scanner(d)
    polys, cur = [], []
    x = y = sx = sy = 0.0
    last_c = last_q = None
    cmd = None
    while True:
        c = sc.command()
        if c is None:
            if not sc.more():
                break
            if cmd is None:
                raise ValueError("path starts without a command")
            c = {"M": "L", "m": "l"}.get(cmd, cmd)
        cmd = c
        rel = c.islower()
        C = c.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if C == "Z":
            if cur:
                polys.append(cur)
            cur = []
            x, y = sx, sy
            last_c = last_q = None
            continue
        if C == "M":
            if cur:
                polys.append(cur)
            x, y = sc.number() + ox, sc.number() + oy
            sx, sy = x, y
            cur = [(x, y)]
            last_c = last_q = None
            while sc.more():
                ox, oy = (x, y) if rel else (0.0, 0.0)
                x, y = sc.number() + ox, sc.number() + oy
                cur.append((x, y))
            continue
        while True:
            ox, oy = (x, y) if rel else (0.0, 0.0)
            if not cur:
                cur = [(x, y)]
            if C == "L":
                x, y = sc.number() + ox, sc.number() + oy
                cur.append((x, y))
                last_c = last_q = None
            elif C == "H":
                x = sc.number() + ox
                cur.append((x, y))
                last_c = last_q = None
            elif C == "V":
                y = sc.number() + oy
                cur.append((x, y))
                last_c = last_q = None
            elif C in "CS":
                if C == "C":
                    p1 = (sc.number() + ox, sc.number() + oy)
                else:
                    p1 = (2 * x - last_c[0], 2 * y - last_c[1]) if last_c else (x, y)
                p2 = (sc.number() + ox, sc.number() + oy)
                p3 = (sc.number() + ox, sc.number() + oy)
                cur += cubic((x, y), p1, p2, p3, n)
                last_c, last_q = p2, None
                x, y = p3
            elif C in "QT":
                if C == "Q":
                    p1 = (sc.number() + ox, sc.number() + oy)
                else:
                    p1 = (2 * x - last_q[0], 2 * y - last_q[1]) if last_q else (x, y)
                p2 = (sc.number() + ox, sc.number() + oy)
                cur += quad((x, y), p1, p2, n)
                last_q, last_c = p1, None
                x, y = p2
            elif C == "A":
                rx, ry, rot = sc.number(), sc.number(), sc.number()
                large, sweep = sc.flag(), sc.flag()
                p1 = (sc.number() + ox, sc.number() + oy)
                cur += arc((x, y), rx, ry, rot, large, sweep, p1)
                x, y = p1
                last_c = last_q = None
            else:
                raise ValueError("unsupported command " + c)
            if not sc.more():
                break
    if cur:
        polys.append(cur)
    return polys


def raster(polys, size, view=24.0, sub=16):
    """Nonzero fill. Returns size*size coverage floats 0..1."""
    k = size / view
    edges = []
    for p in polys:
        pts = [(px * k, py * k) for px, py in p]
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            if y0 == y1:
                continue
            w = 1 if y1 > y0 else -1
            if y0 > y1:
                x0, y0, x1, y1 = x1, y1, x0, y0
            edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0), w))
    cov = [0.0] * (size * size)
    for row in range(size):
        acc = cov
        base = row * size
        for s in range(sub):
            yy = row + (s + 0.5) / sub
            xs = []
            for y0, y1, x0, dxdy, w in edges:
                if y0 <= yy < y1:
                    xs.append((x0 + (yy - y0) * dxdy, w))
            if not xs:
                continue
            xs.sort()
            wind = 0
            for i in range(len(xs) - 1):
                wind += xs[i][1]
                if wind == 0:
                    continue
                a, b = max(0.0, xs[i][0]), min(float(size), xs[i + 1][0])
                if b <= a:
                    continue
                px = int(a)
                while px < size and px < b:
                    lo, hi = max(a, px), min(b, px + 1)
                    if hi > lo:
                        acc[base + px] += (hi - lo) / sub
                    px += 1
    return [min(1.0, c) for c in cov]


def quantise(cov, bpp):
    top = (1 << bpp) - 1
    return [int(c * top + 0.5) for c in cov]


def pack(levels, size, bpp):
    out = bytearray()
    per = 8 // bpp
    for r in range(size):
        row = levels[r * size:(r + 1) * size]
        for i in range(0, size, per):
            b = 0
            for j in range(per):
                v = row[i + j] if i + j < size else 0
                b |= v << (8 - bpp * (j + 1))
            out.append(b)
    return bytes(out)


def png(path, w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_path(svg_dir, name):
    with open(os.path.join(svg_dir, name + ".svg"), encoding="utf-8") as f:
        text = f.read()
    ds = re.findall(r'<path[^>]*\sd="([^"]+)"', text)
    if len(ds) != 1:
        raise ValueError("%s: expected one path, found %d" % (name, len(ds)))
    return ds[0]


def cname(name):
    return re.sub(r"[^A-Z0-9]", "_", name.upper())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--svg-dir", required=True, help="the svg/ folder of the @mdi/svg package")
    ap.add_argument("--icons", help="comma separated mdi names (default: the built-in list)")
    ap.add_argument("--sizes", default="20,24,32")
    ap.add_argument("--bpp", type=int, default=2, choices=(1, 2, 4))
    ap.add_argument("--header")
    ap.add_argument("--sheet")
    ap.add_argument("--png-dir")
    ap.add_argument("--table", help="C header indexed by icon name: gui_icon_name[], gui_icon_off[icon][size], "
                    "ICON_<NAME> enum; every icon at every --sizes size (the GUI's icon_data.h)")
    a = ap.parse_args()
    names = a.icons.split(",") if a.icons else ICONS
    sizes = [int(s) for s in a.sizes.split(",")]
    top = (1 << a.bpp) - 1
    blob = bytearray()
    table = []
    rendered = {}
    for name in names:
        polys = parse_path(read_path(a.svg_dir, name))
        for size in sizes:
            lv = quantise(raster(polys, size), a.bpp)
            rendered[(name, size)] = lv
            data = pack(lv, size, a.bpp)
            table.append((name, size, len(blob), len(data)))
            blob += data
            if a.png_dir:
                os.makedirs(a.png_dir, exist_ok=True)
                rgba = bytearray()
                for v in lv:
                    rgba += bytes((255, 255, 255, v * 255 // top))
                png(os.path.join(a.png_dir, "%s-%d.png" % (name, size)), size, size, bytes(rgba))
    if a.header:
        os.makedirs(os.path.dirname(os.path.abspath(a.header)), exist_ok=True)
        L = ["/* Generated by tools/gui-assets/gen_icons.py from Material Design Icons (Pictogrammers,",
             " * https://pictogrammers.com/library/mdi/), icons licensed under the Apache License 2.0.",
             " * %d bpp alpha, rows padded to a byte, most significant bits first. */" % a.bpp,
             "typedef struct { unsigned short off; unsigned char w, h; } gui_icon;",
             "#define GUI_ICON_BPP %d" % a.bpp, "enum {"]
        for i, (name, size, off, ln) in enumerate(table):
            L.append("\tICON_%s_%d = %d," % (cname(name), size, i))
        L.append("\tICON_COUNT = %d" % len(table))
        L.append("};")
        L.append("static const unsigned char gui_icon_bits[%d] = {" % len(blob))
        for i in range(0, len(blob), 24):
            L.append("\t" + ",".join(str(x) for x in blob[i:i + 24]) + ",")
        L.append("};")
        L.append("static const gui_icon gui_icons[%d] = {" % len(table))
        for name, size, off, ln in table:
            L.append("\t{%d,%d,%d},\t/* %s */" % (off, size, size, name))
        L.append("};")
        assert len(blob) < 65536, "offsets are u16"
        with open(a.header, "w", encoding="ascii") as f:
            f.write("\n".join(L) + "\n")
    if a.table:
        off = {(name, size): o for name, size, o, ln in table}
        L = ["/* Generated by tools/gui-assets/gen_icons.py from Material Design Icons @mdi/svg 7.4.47 (Pictogrammers,",
             " * https://pictogrammers.com/library/mdi/), icons licensed under the Apache License 2.0.",
             " * %d bpp alpha, rows padded to a byte, most significant bits first. Sizes: %s px. */"
             % (a.bpp, ", ".join(str(s) for s in sizes)),
             "#define GUI_ICON_N %d" % len(names), "enum {"]
        L += ["\tICON_%s," % cname(n) for n in names]
        L.append("};")
        L.append("#ifndef GUI_ICON_ENUM_ONLY")
        L.append("static const unsigned char gui_icon_size[%d] = { %s };" % (len(sizes), ", ".join(map(str, sizes))))
        L.append("static const char *const gui_icon_name[GUI_ICON_N] = {")
        L += ['\t"%s",' % n for n in names]
        L.append("};")
        L.append("static const unsigned short gui_icon_off[GUI_ICON_N][%d] = {" % len(sizes))
        L += ["\t{ %s }," % ", ".join(str(off[(n, s)]) for s in sizes) for n in names]
        L.append("};")
        L.append("static const unsigned char gui_icon_bits[%d] = {" % len(blob))
        for i in range(0, len(blob), 24):
            L.append("\t" + ",".join(str(x) for x in blob[i:i + 24]) + ",")
        L.append("};")
        L.append("#endif")
        assert len(blob) < 65536, "offsets are u16"
        with open(a.table, "w", encoding="ascii") as f:
            f.write("\n".join(L) + "\n")
    if a.sheet:
        z, pad = 3, 8
        cell = max(sizes) + pad
        cols = len(sizes)
        per_row = 6
        rows = (len(names) + per_row - 1) // per_row
        W, H = per_row * cols * cell * z, rows * cell * z
        bg, fg = (0x21, 0x21, 0x21), (0xE1, 0xE1, 0xE1)
        img = bytearray(bytes(bg + (255,)) * (W * H))
        for n_i, name in enumerate(names):
            for s_i, size in enumerate(sizes):
                lv = rendered[(name, size)]
                ox = ((n_i % per_row) * cols + s_i) * cell + pad // 2
                oy = (n_i // per_row) * cell + pad // 2
                for y in range(size):
                    for x in range(size):
                        al = lv[y * size + x] / top
                        if not al:
                            continue
                        col = bytes(int(bg[c] + (fg[c] - bg[c]) * al) for c in range(3)) + b"\xff"
                        for zy in range(z):
                            p = (((oy + y) * z + zy) * W + (ox + x) * z) * 4
                            img[p:p + 4 * z] = col * z
        os.makedirs(os.path.dirname(os.path.abspath(a.sheet)), exist_ok=True)
        png(a.sheet, W, H, bytes(img))
    sys.stderr.write("%d icons x %d sizes at %d bpp: %d bytes of bitmaps\n" % (len(names), len(sizes), a.bpp, len(blob)))


if __name__ == "__main__":
    main()

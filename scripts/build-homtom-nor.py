#!/usr/bin/env python3
"""Quantise the homtom RGBA renders to one shared palette and build the patched NOR image.

Reads artifacts/homtom-nor/rgba/id###_WxH.rgba (from brand/render-bootloader.swift) and the newest
artifacts/bootloader-dump-*/mtd0.bin. Writes palette.bin, mtd0-homtom.bin and contact-sheet.png
under artifacts/homtom-nor/. Offline only.
"""
import glob
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "firmware"))
import norimage  # noqa: E402

OUT = ROOT / "artifacts" / "homtom-nor"

ANCHORS = [0x0E161E, 0xF5F3EE, 0x18BCF2, 0xE5484D, 0x0E7091, 0x1B2A38, 0x2A3B4C, 0x5B6B7A]
INK, BONE, BLUE, RED, DEEP, SLATE, SLATE2, GREY = [((a >> 16) & 255, (a >> 8) & 255, a & 255) for a in ANCHORS]
RAMPS = [(INK, BONE, 28), (INK, BLUE, 20), (INK, RED, 16), (BONE, BLUE, 12), (BONE, RED, 14),
         (BONE, DEEP, 14), (BLUE, DEEP, 6), (INK, DEEP, 8), (INK, SLATE, 2), (SLATE, SLATE2, 2),
         (SLATE2, BONE, 8), (SLATE2, BLUE, 6), (GREY, BONE, 8), (INK, GREY, 6), (INK, SLATE2, 4),
         (SLATE, BONE, 4), (SLATE, BLUE, 4), (SLATE2, GREY, 3), (DEEP, SLATE2, 2)]


def to565(c):
    r, g, b = c
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def from565(v):
    return (((v >> 11) << 3) | (v >> 13), (((v >> 5) & 63) << 2) | ((v >> 9) & 3), ((v & 31) << 3) | ((v & 31) >> 2))


def d2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def load_images():
    imgs = {}
    for f in sorted(glob.glob(str(OUT / "rgba" / "id*_*x*.rgba"))):
        name = Path(f).stem
        ident, size = name[2:].split("_")
        w, h = map(int, size.split("x"))
        raw = Path(f).read_bytes()
        assert len(raw) == w * h * 4
        imgs[int(ident)] = (w, h, [(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 4)])
    return imgs


def build_palette(imgs):
    pal = []

    def add(c):
        v = to565(c)
        if v not in pal:
            pal.append(v)

    for a in ANCHORS:
        add(((a >> 16) & 255, (a >> 8) & 255, a & 255))
    fixed = len(pal)
    for a, b, n in RAMPS:
        for i in range(1, n + 1):
            t = i / (n + 1)
            add(tuple(round(a[k] + (b[k] - a[k]) * t) for k in range(3)))
    counts = {}
    for _, _, px in imgs.values():
        for c in px:
            counts[c] = counts.get(c, 0) + 1
    # Top up to 256 with the most frequent colours that the palette represents worst.
    while len(pal) < 256:
        cols = [from565(v) for v in pal]
        worst = max(counts, key=lambda c: counts[c] * min(d2(c, p) for p in cols))
        before = len(pal)
        add(worst)
        if len(pal) == before:
            break
    assert len(pal) <= 256, len(pal)
    while len(pal) < 256:
        pal.append(pal[-1])
    # Lloyd refinement of the non-anchor, non-ramp-end entries is skipped on purpose: ramps stay exact.
    return pal[:256], fixed


def quantise(imgs, pal):
    cols = [from565(v) for v in pal]
    cache = {}
    out = {}
    err = 0.0
    n = 0
    for ident, (w, h, px) in imgs.items():
        idx = bytearray()
        for c in px:
            j = cache.get(c)
            if j is None:
                j = min(range(256), key=lambda k: d2(c, cols[k]))
                cache[c] = j
            idx.append(j)
            err += d2(c, cols[j])
            n += 1
        out[ident] = (w, h, bytes(idx))
    return out, (err / n) ** 0.5


def contact_sheet(images, palette, path, bg):
    pad = 6
    width = 1000
    x = y = pad
    rowh = 0
    placed = []
    for im in images:
        w, h = im["width"], im["height"]
        if x + w + pad > width:
            x, y, rowh = pad, y + rowh + pad, 0
        placed.append((x, y, im))
        x += w + pad
        rowh = max(rowh, h)
    height = y + rowh + pad
    canvas = bytearray([bg]) * (width * height)
    for px, py, im in placed:
        w, h = im["width"], im["height"]
        for r in range(h):
            canvas[(py + r) * width + px:(py + r) * width + px + w] = im["pixels"][r * w:(r + 1) * w]
    norimage.write_indexed_png(path, width, height, bytes(canvas), palette)


def main():
    dump = Path(sorted(glob.glob(str(ROOT / "artifacts" / "bootloader-dump-*" / "mtd0.bin")))[-1])
    data = dump.read_bytes()
    orig = norimage.parse(data)
    imgs = load_images()
    for im in orig["images"]:
        assert im["id"] in imgs and imgs[im["id"]][:2] == (im["width"], im["height"]), im["id"]
    pal, fixed = build_palette(imgs)
    q, rms = quantise(imgs, pal)
    (OUT / "palette.bin").write_bytes(struct.pack("<256H", *pal))
    records = [(im["id"],) + q[im["id"]] for im in orig["images"]]
    region = norimage.build_region(pal, orig["pre"], records)
    patched = bytearray(data)
    patched[norimage.PALETTE_OFFSET:norimage.PALETTE_OFFSET + len(region)] = region
    (OUT / "mtd0-homtom.bin").write_bytes(patched)
    end = norimage.PALETTE_OFFSET + len(region)
    new = norimage.parse(bytes(patched))
    palette_rgb = new["palette"]
    contact_sheet(new["images"], palette_rgb, OUT / "contact-sheet.png", 6)
    print("dump", dump.name)
    print("palette entries %d, anchors %d, rms quantisation error %.2f" % (len(set(pal)), fixed, rms))
    print("region 0x28000-0x%x (%d bytes, stock 0x%x)" % (end, len(region), orig["end"] + 8))
    return 0


if __name__ == "__main__":
    sys.exit(main())

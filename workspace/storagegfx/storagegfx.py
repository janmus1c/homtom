"""Build a homtom storage-mode image table and render every group-16 scene, stock and new.

Usage: python3 storagegfx.py NOR BOOTLOADER_OUT ART_DIR OUT_DIR

NOR             current NOR dump (stock image table at 0x28000)
BOOTLOADER_OUT  gunzipped bootloader (scene table at image offset 0x28d94)
ART_DIR         id###_WxH.rgba files from render-art.swift
OUT_DIR         receives table.bin (palette + pre byte + records + end marker, the layout of NOR 0x28000),
                frames/*.rgb (480x272 RGB888) and frames.json for the contact-sheet step.
"""
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware"))
import norimage  # noqa: E402

SCENE_BASE, SCENE_SIZE, SCENES = 0x28D94, 0x140, 140
GROUP = 16
W, H = 480, 272
LABEL = [(230, 337, 8), (263, 443, 8)]

STATES = [
    (0x00, "idle", "Storage mode, idle (no host access for 5000 ticks)", "V"),
    (0x41, "busy-int", "Host is reading/writing INTERNAL; alternates with 0x42 (badge blinks)", "V"),
    (0x42, "busy-none", "Busy frame without drive badge (the off phase of the blink)", "V"),
    (0x81, "busy-sd", "Host is reading/writing the SD card; alternates with 0x82", "V"),
    (0x82, "busy-none-sd", "Busy frame without badge, SD blink off phase", "V"),
    (0xC1, "busy-both", "Host accesses both drives; alternates with 0xC2", "V"),
    (0xC2, "busy-none-both", "Busy frame without badges, both-drive blink off phase", "V"),
    (0x45, "start-int", "Starting from INTERNAL: ttsystem is being loaded from drive 0", "V"),
    (0x85, "start-sd", "Starting from the SD card: ttsystem is being loaded from drive 1", "V"),
    (0x44, "no-system", "No usable system: card slides out, then the card icon is struck through; loops until media or a USB host appears", "V call, H meaning"),
    (0xC4, "no-system-both", "As 0x44 plus error mark and SD badge (INTERNAL flagged unusable)", "V call, H meaning"),
    (0x43, "card-in", "Struck card turns plain, then the card slides in; shown while the SD card is checked", "V call, H meaning"),
    (0x84, "error-sd", "Error mark with blinking SD badge; no caller of the scene selector produces 0x84", "unreachable (V)"),
]


def scene_table(bl):
    out = {}
    for s in range(SCENES):
        off = SCENE_BASE + s * SCENE_SIZE
        group, mode = struct.unpack_from("<II", bl, off)
        steps = []
        for k in range(26):
            i, x, y, delay = struct.unpack_from("<Ihhi", bl, off + 8 + k * 12)
            if i in (0xFFFFFFFF, 0xFFFFFFFE):
                steps.append(("END" if i == 0xFFFFFFFF else "LOOP",))
                break
            steps.append((i, x, y, delay))
        if group == GROUP:
            out[mode] = (s, steps)
    return out


def blit(canvas, img, pal, x, y):
    w, h, px = img
    for row in range(h):
        yy = y + row
        if not 0 <= yy < H:
            continue
        base = (yy * W + x) * 3
        line = bytearray()
        for p in px[row * w:(row + 1) * w]:
            line += pal[p]
        n = min(w, W - x)
        canvas[base:base + n * 3] = line[:n * 3]


def frames(steps, images, pal, label=True):
    """Simulate 0x30b06964: draw entries; every entry with a delay, or the end, shows a frame with the label."""
    canvas = bytearray(W * H * 3)
    out = []
    for st in steps:
        if st[0] in ("END", "LOOP"):
            break
        ident, x, y, delay = st
        if ident in images:
            blit(canvas, images[ident], pal, x, y)
        if delay:
            snap = bytearray(canvas)
            if label:
                for lid, lx, ly in LABEL:
                    if lid in images:
                        blit(snap, images[lid], pal, lx, ly)
            out.append((bytes(snap), delay))
    if not out or steps[-1][0] == "END":
        snap = bytearray(canvas)
        if label:
            for lid, lx, ly in LABEL:
                if lid in images:
                    blit(snap, images[lid], pal, lx, ly)
        if not out or out[-1][0] != bytes(snap):
            out.append((bytes(snap), 0))
    return out


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def expand565(v):
    return (((v >> 11) << 3) | (v >> 13), (((v >> 5) & 63) << 2) | ((v >> 9) & 3), ((v & 31) << 3) | ((v & 31) >> 2))


def build_palette(arts):
    """Index 0 stays black (the R1 logo and screen clears rely on it). The rest: the most used RGB565
    colours, then greedy merging so every pixel maps to its nearest entry."""
    counts = {}
    for _, _, rgba in arts.values():
        for i in range(0, len(rgba), 4):
            v = rgb565(rgba[i], rgba[i + 1], rgba[i + 2])
            counts[v] = counts.get(v, 0) + 1
    ranked = sorted(counts, key=lambda v: -counts[v])
    chosen = [0x0000]
    cols = [expand565(0)]

    def dist(a, b):
        return (a[0] - b[0]) ** 2 * 3 + (a[1] - b[1]) ** 2 * 4 + (a[2] - b[2]) ** 2 * 2

    for v in ranked:
        if len(chosen) == 256:
            break
        c = expand565(v)
        if v in chosen:
            continue
        if min(dist(c, k) for k in cols) < 60:
            continue
        chosen.append(v)
        cols.append(c)
    while len(chosen) < 256:
        chosen.append(0x0000)
        cols.append((0, 0, 0))
    cache = {}

    def index(v):
        if v not in cache:
            c = expand565(v)
            cache[v] = min(range(len(cols)), key=lambda i: dist(c, cols[i]) + (0 if i or v == 0 else 10 ** 9))
        return cache[v]

    return chosen, index


def write_png(path, w, h, rgb):
    import zlib

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(nor_path, bl_path, art_dir, out_dir):
    nor = Path(nor_path).read_bytes()
    bl = Path(bl_path).read_bytes()
    out = Path(out_dir)
    (out / "frames").mkdir(parents=True, exist_ok=True)
    stock = norimage.parse(nor)
    stock_pal = [bytes(c) for c in stock["palette"]]
    stock_imgs = {i["id"]: (i["width"], i["height"], i["pixels"]) for i in stock["images"]}

    arts = {}
    for f in sorted(Path(art_dir).glob("id*_*x*.rgba")):
        ident = int(f.name[2:5])
        w, h = map(int, f.stem.split("_")[1].split("x"))
        data = f.read_bytes()
        assert len(data) == w * h * 4, f
        sw, sh, _ = stock_imgs[ident]
        assert (w, h) == (sw, sh), "size differs from stock for id %d" % ident
        arts[ident] = (w, h, data)

    pal565, index = build_palette(arts)
    new_imgs = dict(stock_imgs)
    for ident, (w, h, rgba) in arts.items():
        px = bytes(index(rgb565(rgba[i], rgba[i + 1], rgba[i + 2])) for i in range(0, len(rgba), 4))
        new_imgs[ident] = (w, h, px)
    new_pal = [bytes(expand565(v)) for v in pal565]

    order = [i["id"] for i in stock["images"]]
    region = norimage.build_region(pal565, stock["pre"], [(i, *new_imgs[i]) for i in order])
    (out / "table.bin").write_bytes(region)
    check = norimage.parse(b"\xff" * norimage.PALETTE_OFFSET + region + b"\xff" * 8)
    assert [i["id"] for i in check["images"]] == order
    for i in check["images"]:
        assert i["pixels"] == new_imgs[i["id"]][2]

    scenes = scene_table(bl)
    manifest = []
    for mode, slug, meaning, grade in STATES:
        sidx, steps = scenes[mode]
        ids = [s[0] for s in steps if s[0] not in ("END", "LOOP")]
        fo = frames(steps, stock_imgs, stock_pal)
        fn = frames(steps, new_imgs, new_pal)
        names = []
        for k, ((a, d), (b, _)) in enumerate(zip(fo, fn)):
            na = "%02x-%s-%d-orig.rgb" % (mode, slug, k)
            nb = "%02x-%s-%d-new.rgb" % (mode, slug, k)
            (out / "frames" / na).write_bytes(a)
            (out / "frames" / nb).write_bytes(b)
            names.append(dict(orig=na, new=nb, delay=d))
        loop = steps[-1][0] == "LOOP"
        bare = frames([(218, 0, 0, 0), ("END",)], stock_imgs, stock_pal)[0][0]
        key = max(range(len(fo)), key=lambda k: (sum(1 for i in range(0, len(bare), 3) if fo[k][0][i:i + 3] != bare[i:i + 3]), k))
        manifest.append(dict(key=key, mode="0x%02X" % mode, slug=slug, scene=sidx, meaning=meaning, grade=grade,
                             ids=ids, loop=loop, frames=names))
        write_png(out / ("%02x-%s-new.png" % (mode, slug)), W, H, fn[key][0])
    (out / "frames.json").write_text(json.dumps(manifest, indent=1))
    (out / "images").mkdir(exist_ok=True)
    imgs = []
    for ident in sorted(arts):
        for tag, src, pal in (("orig", stock_imgs, stock_pal), ("new", new_imgs, new_pal)):
            w, h, px = src[ident]
            (out / "images" / ("id%03d-%s.rgb" % (ident, tag))).write_bytes(b"".join(pal[p] for p in px))
        imgs.append(dict(id=ident, w=w, h=h, rle=len(norimage.encode_rle(new_imgs[ident][2])),
                         stock_rle=len(norimage.encode_rle(stock_imgs[ident][2]))))
    (out / "images.json").write_text(json.dumps(imgs, indent=1))
    used = len(set(pal565))
    print("table %d bytes (stock %d), palette entries used %d" % (len(region), stock["end"] + 8 - 0x28000, used))
    for ident in sorted(arts):
        w, h, px = new_imgs[ident]
        print("id %d %dx%d rle %d bytes" % (ident, w, h, len(norimage.encode_rle(px))))


if __name__ == "__main__":
    main(*sys.argv[1:5])

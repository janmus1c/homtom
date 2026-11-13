"""Decode and encode the image table in the GO 630 NOR dump (mtd0, bootloader 5.5120).

Layout (confirmed from a read-only dump of the NOR):
  0x28000  256 x RGB565 little-endian palette (512 bytes), shared by all images
  0x28200  one byte (0x10 in the dump), kept verbatim
  0x28201  chain of records, each 16-byte big-endian header + RLE pixel data:
             u32 len   = 8 + len(data)
             u32 id
             u32 width
             u32 height
           pixel data is 8 bits per pixel, palette indexed, row-major with no row
           alignment. Tokens: n in 1..0x7f = run of n copies of the next byte;
           n in 0x80..0xff = literal of 256-n following bytes.
  end      8 zero bytes (len == 0), then 0xFF to 0x40000 where the ngffs area starts.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

PALETTE_OFFSET = 0x28000
RECORDS_OFFSET = 0x28201
REGION_END = 0x40000


def decode_rle(blob, count):
    out = bytearray()
    i = 0
    while len(out) < count:
        n = blob[i]
        if n == 0:
            raise ValueError("zero token at %d" % i)
        if n < 0x80:
            out += bytes([blob[i + 1]]) * n
            i += 2
        else:
            k = 256 - n
            out += blob[i + 1:i + 1 + k]
            i += 1 + k
    if len(out) != count or i != len(blob):
        raise ValueError("RLE data does not match pixel count")
    return bytes(out)


def encode_rle(pixels, literal_cap=128):
    """Reproduces the original encoder byte for byte on all 24 stock images."""
    out = bytearray()
    n = len(pixels)

    def run(p):
        r = 1
        while p + r < n and pixels[p + r] == pixels[p] and r < 127:
            r += 1
        return r

    p = 0
    while p < n:
        r = run(p)
        if r >= 2:
            out += bytes([r, pixels[p]])
            p += r
            continue
        start = p
        p += 1
        while p < n and p - start < literal_cap and run(p) < 3:
            p += 1
        out += bytes([256 - (p - start)]) + bytes(pixels[start:p])
    return bytes(out)


def parse(data):
    """Return dict(palette=[(r,g,b)*256], pre=byte, images=[...], trailer=bytes)."""
    pal565 = struct.unpack_from("<256H", data, PALETTE_OFFSET)
    palette = [(((v >> 11) << 3) | (v >> 13), (((v >> 5) & 63) << 2) | ((v >> 9) & 3),
                ((v & 31) << 3) | ((v & 31) >> 2)) for v in pal565]
    images = []
    off = RECORDS_OFFSET
    while True:
        length, ident, width, height = struct.unpack_from(">4I", data, off)
        if length == 0:
            break
        end = off + 8 + length
        blob = bytes(data[off + 16:end])
        images.append(dict(offset=off, id=ident, width=width, height=height, blob=blob,
                           pixels=decode_rle(blob, width * height)))
        off = end
    return dict(palette=palette, palette565=pal565, pre=data[RECORDS_OFFSET - 1],
                images=images, end=off, trailer=bytes(data[off:off + 8]))


def encode_record(ident, width, height, pixels):
    blob = encode_rle(pixels)
    return struct.pack(">4I", 8 + len(blob), ident, width, height) + blob


def build_region(palette565, pre, images, trailer=b"\0" * 8):
    """Bytes for 0x28000 up to the end marker. images: iterable of (id, w, h, pixels)."""
    out = bytearray(struct.pack("<256H", *palette565))
    out.append(pre)
    for ident, w, h, px in images:
        out += encode_record(ident, w, h, px)
    out += trailer
    if PALETTE_OFFSET + len(out) > REGION_END:
        raise ValueError("image region would run into the ngffs area at 0x%x" % REGION_END)
    return bytes(out)


def write_indexed_png(path, width, height, pixels, palette):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    raw = b"".join(b"\0" + pixels[y * width:(y + 1) * width] for y in range(height))
    plte = b"".join(bytes(c) for c in palette)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
                           + chunk(b"PLTE", plte) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def extract(nor_path, out_dir):
    data = Path(nor_path).read_bytes()
    info = parse(data)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for n, img in enumerate(info["images"]):
        name = "%02d_id%03d_%dx%d_0x%x.png" % (n, img["id"], img["width"], img["height"], img["offset"])
        write_indexed_png(out / name, img["width"], img["height"], img["pixels"], info["palette"])
        print(name)
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract", help="write every image as an indexed PNG")
    e.add_argument("nor")
    e.add_argument("outdir")
    v = sub.add_parser("verify", help="check that re-encoding reproduces every record")
    v.add_argument("nor")
    a = ap.parse_args(argv)
    if a.cmd == "extract":
        extract(a.nor, a.outdir)
    else:
        info = parse(Path(a.nor).read_bytes())
        bad = [i["id"] for i in info["images"] if encode_rle(i["pixels"]) != i["blob"]]
        print("%d images, %d mismatches %s" % (len(info["images"]), len(bad), bad))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

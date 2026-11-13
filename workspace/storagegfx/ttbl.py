"""TTBL ttsystem packer and loader-faithful verifier for the storage-graphics records.

verify IMAGE BOOTLOADER_OUT
    Replays what the 5.5120 loader does (0x30b0bb80, dis:12048-12206): magic, structural pass over the
    records, the optional trailer rule after the terminator, then per record MD5 of the payload compared
    with the Blowfish-decrypted signature. Blowfish uses the P-array and S-boxes read from the bootloader
    image (0x33dd4, 0x33e1c) and the key the loader copies from image offset 0xbdb4, so the check does
    not share code with the signer. It also checks every load range against the bootloader RAM map.

stage BASE TABLE_BIN OUT [SPLASH_NAME]
    Append data records to BASE (an existing ttsystem): the image table at TABLE_ADDR, the palette at
    the bootloader's RAM palette copy, the 4-byte table pointer, and optionally an 11-byte 8.3 name
    written over "SPLASHW BMP" at 0x30b05f78, so the splash lookup fails and id 0 is drawn from the
    redirected table instead (0x30b00148-0x30b00150). Signs with ttsign.
"""
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TABLE_ADDR = 0x30F00000
POINTER_ADDR = 0x30008430
PALETTE_RAM = 0x30009400
RECORDS_IN_TABLE = 0x201

# Bootloader RAM use (dis line numbers in docs/STORAGE_GFX.md). Ranges are [start, end).
RESERVED = [
    (0x30000000, 0x30008000, "below .data, use unknown"),
    (0x30008000, 0x30008A28, ".data"),
    (0x30008A28, 0x3016B76C, ".bss (zeroed at reset)"),
    (0x3016B76C, 0x303F5800, "SVC stack grows down from 0x303f5800"),
    (0x303F5800, 0x30400000, "mode stacks, MMU table 0x303f8000, IRQ vectors 0x303fff00"),
    (0x30400000, 0x30600000, "frame buffers (uncached sections)"),
    (0x30600000, 0x30680000, "RLE decode buffer (0x300082dc)"),
    (0x30680000, 0x30700000, "buffer pointed to by 0x30008130"),
    (0x30700000, 0x30880000, "uncached DMA/USB buffers"),
    (0x30880000, 0x30B00000, "heap (0x30b0ac6c)"),
    (0x30B00000, 0x30B35DCC, "bootloader image"),
]
SPLASH_NAME = 0x30B05F78  # "SPLASHW BMP" (8.3, 11 bytes) in the bootloader's RAM image, read as data by 0x30b05d4c
ALLOWED_INSIDE = [(POINTER_ADDR, POINTER_ADDR + 4), (PALETTE_RAM, PALETTE_RAM + 512), (SPLASH_NAME, SPLASH_NAME + 11)]


class Blowfish:
    def __init__(self, p, s, key):
        self.p = list(p)
        self.s = [list(s[i * 256:(i + 1) * 256]) for i in range(4)]
        j = 0
        for i in range(18):
            d = 0
            for _ in range(4):
                d = (d << 8) | key[j]
                j = (j + 1) % len(key)
            self.p[i] ^= d
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self.encrypt(l, r)
            self.p[i], self.p[i + 1] = l, r
        for box in self.s:
            for i in range(0, 256, 2):
                l, r = self.encrypt(l, r)
                box[i], box[i + 1] = l, r

    def f(self, x):
        s = self.s
        return ((((s[0][x >> 24] + s[1][(x >> 16) & 255]) & 0xFFFFFFFF) ^ s[2][(x >> 8) & 255]) + s[3][x & 255]) & 0xFFFFFFFF

    def encrypt(self, l, r):
        for i in range(16):
            l ^= self.p[i]
            r ^= self.f(l)
            l, r = r, l
        l, r = r, l
        return l ^ self.p[17], r ^ self.p[16]

    def decrypt(self, l, r):
        for i in range(17, 1, -1):
            l ^= self.p[i]
            r ^= self.f(l)
            l, r = r, l
        l, r = r, l
        return l ^ self.p[0], r ^ self.p[1]


def bootloader_cipher(bl):
    p = struct.unpack_from("<18I", bl, 0x33DD4)
    s = struct.unpack_from("<1024I", bl, 0x33E1C)
    return Blowfish(p, s, bl[0xBDB4:0xBDC4])


def parse(img):
    if img[:4] != b"TTBL":
        raise ValueError("magic")
    recs, off = [], 4
    while True:
        if off + 8 > len(img):
            raise ValueError("truncated before terminator")
        length, addr = struct.unpack_from("<II", img, off)
        if length == 0:
            entry, param = struct.unpack_from("<II", img, off + 4)
            return recs, entry, param, img[off + 12:]
        end = off + 8 + length + 16
        if end > len(img):
            raise ValueError("record at %d runs past the file" % off)
        recs.append(dict(offset=off, addr=addr, data=img[off + 8:off + 8 + length], sig=img[off + 8 + length:end]))
        off = end


def verify(img, bl):
    problems = []
    recs, entry, param, trailer = parse(img)
    if len(trailer) >= 4:
        w = struct.unpack_from("<I", trailer)[0]
        if w == 0 and len(trailer) >= 8 and struct.unpack_from("<I", trailer, 4)[0] != 1:
            problems.append("trailer after terminator would be rejected by the loader")
    bf = bootloader_cipher(bl)
    print("TTBL, %d records, entry 0x%08x, param 0x%08x, trailer %d bytes" % (len(recs), entry, param, len(trailer)))
    spans = []
    for r in recs:
        a0, a1 = r["addr"], r["addr"] + len(r["data"])
        l0, r0, l1, r1 = struct.unpack("<4I", r["sig"])
        dec = struct.pack("<4I", *bf.decrypt(l0, r0), *bf.decrypt(l1, r1))
        md5 = hashlib.md5(r["data"]).digest()
        ok = dec == md5
        notes = []
        for s, e, what in RESERVED:
            if a0 < e and a1 > s and not any(a0 >= x and a1 <= y for x, y in ALLOWED_INSIDE):
                notes.append("overlaps " + what)
        for s, e in spans:
            if a0 < e and a1 > s:
                notes.append("overlaps another record")
        spans.append((a0, a1))
        print("  0x%08x-0x%08x %8d bytes  signature %s%s" % (a0, a1, len(r["data"]), "OK" if ok else "BAD",
                                                             ("  [" + "; ".join(notes) + "]") if notes else ""))
        if not ok:
            problems.append("signature 0x%08x" % a0)
        problems += ["0x%08x %s" % (a0, n) for n in notes]
    if not any(r["addr"] <= entry < r["addr"] + len(r["data"]) for r in recs):
        problems.append("entry point not inside a record")
    return recs, problems


def sign(data):
    return subprocess.run([str(HERE / "ttsign")], input=data, capture_output=True, check=True).stdout


def pack(recs, entry, param):
    out = bytearray(b"TTBL")
    for addr, data in recs:
        out += struct.pack("<II", len(data), addr) + data + sign(data)
    out += struct.pack("<III", 0, entry, param)
    return bytes(out)


def stage(base, table, palette, splash_name=None):
    recs, entry, param, trailer = parse(base)
    if trailer:
        raise ValueError("base image has a trailer; not handled")
    pointer = struct.pack("<I", TABLE_ADDR + RECORDS_IN_TABLE)
    extra = [(TABLE_ADDR, table), (PALETTE_RAM, palette), (POINTER_ADDR, pointer)]
    if splash_name is not None:
        assert len(splash_name) == 11
        extra.append((SPLASH_NAME, splash_name))
    return pack([(r["addr"], r["data"]) for r in recs] + extra, entry, param)


def main(argv):
    if argv[0] == "verify":
        img, bl = Path(argv[1]).read_bytes(), Path(argv[2]).read_bytes()
        _, problems = verify(img, bl)
        print("RESULT:", "well-formed and signed" if not problems else "PROBLEMS: " + "; ".join(problems))
        return 1 if problems else 0
    if argv[0] == "stage":
        base = Path(argv[1]).read_bytes()
        table = Path(argv[2]).read_bytes()
        out = Path(argv[3])
        name = argv[4].encode() if len(argv) > 4 else None
        img = stage(base, table, table[:512], name)
        out.write_bytes(img)
        digest = hashlib.sha256(img).hexdigest()
        Path(str(out) + ".sha256").write_text("%s  %s\n" % (digest, out.name))
        print(out, len(img), digest)
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

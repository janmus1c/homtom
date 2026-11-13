#!/usr/bin/env python3
"""Turn the homtom RGB565 splash into the bootloader's 480x272 24-bit BMP screens."""
import struct
import sys
from pathlib import Path

W, H = 480, 272
ROOT = Path(__file__).resolve().parent


def load_fb(path):
    data = Path(path).read_bytes()
    pixels = []
    for i in range(0, len(data), 2):
        v = data[i] | data[i + 1] << 8
        r, g, b = v >> 11 & 31, v >> 5 & 63, v & 31
        pixels.append([r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2])
    return pixels


def battery(pixels, colour=(0xF2, 0x5C, 0x3B)):
    x0, y0, w, h = 222, 222, 32, 14
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w + 3):
            edge = x in (x0, x0 + 1, x0 + w - 2, x0 + w - 1) or y in (y0, y0 + 1, y0 + h - 2, y0 + h - 1)
            cap = x >= x0 + w and y0 + 4 <= y < y0 + h - 4
            level = x0 + 4 <= x < x0 + 10 and y0 + 4 <= y < y0 + h - 4
            if (edge and x < x0 + w) or cap or level:
                pixels[y * W + x] = list(colour)


def bmp(pixels):
    rows = b"".join(bytes(c for px in pixels[y * W:(y + 1) * W] for c in reversed(px)) for y in range(H - 1, -1, -1))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(rows), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(rows), 2835, 2835, 0, 0)
    return header + info + rows


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else ROOT / "overlay/etc/homtom/splash.fb"
    out = ROOT.parent / "build/bootsplash"
    out.mkdir(parents=True, exist_ok=True)
    base = load_fb(src)
    (out / "splashw.bmp").write_bytes(bmp(base))
    (out / "antitheftw.bmp").write_bytes(bmp(base))
    low = [list(p) for p in base]
    battery(low)
    (out / "lowbatteryw.bmp").write_bytes(bmp(low))
    for name in ("splashw.bmp", "antitheftw.bmp", "lowbatteryw.bmp"):
        print(out / name, (out / name).stat().st_size)

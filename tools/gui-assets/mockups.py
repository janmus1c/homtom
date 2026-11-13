#!/usr/bin/env python3
"""Render the 480x272 UI mockups of docs/UI_DESIGN.md to PNG.

Each page is described below with the same pixel geometry the C renderer will use (rectangles, text baselines,
icon positions). The script writes one absolutely positioned HTML page per mockup with Roboto and the Poppins
wordmark embedded, and the icons as the quantised 2 bpp bitmaps from gen_icons.py (so icon pixels match the
device tables). Headless Chrome takes a 480x272 screenshot; the script then decodes the PNG (stdlib), quantises
every pixel to RGB565 (ordered 4x4 Bayer dither only inside gradient widgets) and writes the result.

usage:
  python3 -I tools/gui-assets/mockups.py --roboto Roboto-VF.ttf --svg-dir package/svg [--out docs/ui-mockups]

Roboto: google/fonts ofl/roboto (SIL OFL 1.1). Icons: @mdi/svg 7.4.47 (Apache 2.0). Poppins: brand/fonts.
"""
import argparse
import base64
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import gen_icons  # noqa: E402

W, H = 480, 272
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
ROBOTO_ASC, ROBOTO_DESC = 1900 / 2048, 500 / 2048
POPPINS_ASC, POPPINS_DESC = 1.05, 0.35

DARK = dict(
    bg="#101010", card="#212021", border="#313031", chip_off="#292829", text="#DEDFDE", text2="#9C9E9C",
    disabled="#6B6D6B", track="#292829", dot_off="#4A494A", primary="#009AC6", mark="#18BAEF",
    amber="#FFC308", heat="#FF6D21", steel="#42719C", green="#4AAE52", error="#DE4539", warning="#FFA600",
    chip_amber="#4A4118", chip_heat="#4A3021", chip_steel="#293039", chip_green="#293C29", chip_primary="#183842",
    light_fill="#393021", switch_off="#4A494A", handle="#FFFFFF",
)
LIGHT = dict(
    bg="#F7F7F7", card="#FFFFFF", border="#DEDFDE", chip_off="#EFEFEF", text="#101410", text2="#5A5D5A",
    disabled="#BDBEBD", track="#EFEFEF", dot_off="#BDBEBD", primary="#009AC6", mark="#009AC6",
    amber="#FFC308", heat="#FF6D21", steel="#42719C", green="#4AAE52", error="#DE4539", warning="#FFA600",
    chip_amber="#FFF3CE", chip_heat="#FFE3D6", chip_steel="#DEE3EF", chip_green="#DEEFDE", chip_primary="#D6EFF7",
    light_fill="#FFF7DE", switch_off="#BDBEBD", handle="#FFFFFF",
)


class Page:
    def __init__(self, t, icons):
        self.t, self.icons, self.el, self.dither = t, icons, [], []

    def c(self, k):
        return self.t.get(k, k)

    def rect(self, x, y, w, h, fill, r=0, border=None, extra=""):
        b = "border:1px solid %s;" % self.c(border) if border else ""
        self.el.append('<div style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;background:%s;border-radius:%s;%s%s"></div>'
                       % (x, y, w, h, self.c(fill), "%dpx" % r if isinstance(r, int) else r, b, extra))

    def raw(self, html):
        self.el.append(html)

    def text(self, x, base, s, size=14, weight=400, color="text", align="l", w=0, font="roboto", track=0):
        asc, desc = (ROBOTO_ASC, ROBOTO_DESC) if font == "roboto" else (POPPINS_ASC, POPPINS_DESC)
        a, d = round(asc * size), round(desc * size)
        fam = "R" if font == "roboto" else "P"
        style = "top:%dpx;font:%d %dpx/%dpx %s;color:%s;letter-spacing:%spx;" % (
            base - a, weight, size, a + d, fam, self.c(color), track)
        if align == "l":
            style += "left:%dpx;" % x
        elif align == "r":
            style += "left:%dpx;width:%dpx;text-align:right;" % (x - w, w)
        else:
            style += "left:%dpx;width:%dpx;text-align:center;" % (x, w)
        self.el.append('<div class="t" style="%s">%s</div>' % (style, s))

    def icon(self, name, size, x, y, color="text"):
        self.el.append('<div style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;background:%s;'
                       '-webkit-mask:url(%s) no-repeat"></div>' % (x, y, size, size, self.c(color), self.icons(name, size)))

    def chip(self, name, cx, cy, fg, bg, d=36, isz=24):
        self.rect(cx - d // 2, cy - d // 2, d, d, bg, r="50%")
        self.icon(name, isz, cx - isz // 2, cy - isz // 2, fg)

    def mark(self, x, y, size, color):
        self.el.append('<svg style="left:%dpx;top:%dpx" width="%d" height="%d" viewBox="20 20 60 60"><g fill="none" '
                       'stroke="%s" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"><path d="M27 48 50 '
                       '28l23 20v24H27Z"/><path d="M50 47 56 62 50 58.5 44 62Z" fill="%s" stroke-width="3"/></g></svg>'
                       % (x, y, size, size, self.c(color), self.c(color)))

    def html(self, fonts):
        return ("<!doctype html><meta charset=utf-8><style>%s html,body{margin:0;width:%dpx;height:%dpx;overflow:hidden;"
                "background:%s;-webkit-font-smoothing:antialiased}body>*{position:absolute;box-sizing:border-box}"
                ".t{white-space:pre;font-feature-settings:'tnum'}</style><body>%s</body>"
                % (fonts, W, H, self.c("bg"), "".join(self.el)))


# ---------- shared parts ----------

def header(p, title, right="14:32", back=False, offline=None):
    if back:
        p.icon("arrow-left", 24, 12, 4, "text")
        p.text(48, 22, title, 16, 500)
    else:
        p.mark(12, 7, 18, "mark")
        p.text(35, 21, "homtom", 14, 600, "text2", font="poppins", track=-0.63)
        p.rect(98, 9, 1, 14, "border")
        p.text(108, 22, title, 16, 500)
    if offline:
        p.icon("cloud-off-outline", 20, 364, 6, "warning")
        p.text(468, 21, offline, 12, 500, "warning", align="r", w=80)
    else:
        p.text(468, 21, right, 14, 500, "text2", align="r", w=60)


def dots(p, n, active, cy=260):
    x0 = W // 2 - (n - 1) * 6
    for i in range(n):
        p.rect(x0 + i * 12 - 3, cy - 3, 6, 6, "text" if i == active else "dot_off", r="50%")


def card(p, x, y, w, h):
    p.rect(x, y, w, h, "card", 12, "border")


def light_tile(p, x, y, w, h, name, state, pct=None, unavailable=False):
    if pct:
        fill_w = max(12, (w - 2) * pct // 100)
        p.raw('<div style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;border-radius:12px;overflow:hidden;'
              'background:%s;border:1px solid %s"><div style="position:absolute;left:0;top:0;bottom:0;width:%dpx;'
              'background:%s"></div></div>' % (x, y, w, h, p.c("card"), p.c("border"), fill_w, p.c("light_fill")))
    else:
        card(p, x, y, w, h)
    cy = y + h // 2
    if unavailable:
        p.chip("lightbulb-outline", x + 30, cy, "disabled", "chip_off")
    elif pct:
        p.chip("lightbulb", x + 30, cy, "amber", "chip_amber")
    else:
        p.chip("lightbulb-outline", x + 30, cy, "text2", "chip_off")
    p.text(x + 58, cy - 3, name, 14, 500, "disabled" if unavailable else "text")
    p.text(x + 58, cy + 14, state, 12, 400, "disabled" if unavailable else "text")


# ---------- pages ----------

def page_office(p, offline=False):
    header(p, "Office", offline="Offline 4 min" if offline else None)
    if offline:
        p.t = dict(p.t, amber=p.t["text2"], chip_amber=p.t["chip_off"], heat=p.t["text2"], chip_heat=p.t["chip_off"],
                   light_fill=p.t["card"], steel=p.t["text2"])
    light_tile(p, 8, 40, 146, 76, "Ambient", "72 %", pct=72)
    light_tile(p, 162, 40, 146, 76, "Desk lamp", "40 %", pct=40)
    light_tile(p, 8, 124, 146, 76, "Ceiling", "Off")
    light_tile(p, 162, 124, 146, 76, "LED strip", "Unavailable", unavailable=True)
    # sensor strip: one card, three readings
    card(p, 8, 208, 300, 40)
    cells = [("motion-sensor", "amber" if not offline else "text2", "Detected"),
             ("molecule-co2", "steel", "612 ppm"), ("thermometer", "steel", "22.4 °C")]
    for i, (ic, col, val) in enumerate(cells):
        cx = 8 + i * 100
        if i:
            p.rect(cx, 218, 1, 20, "border")
        p.icon(ic, 20, cx + 12, 218, col)
        p.text(cx + 38, 233, val, 14, 500, "text2" if offline else "text")
    # thermostat
    card(p, 316, 40, 156, 208)
    p.chip("radiator", 346, 76, "heat", "chip_heat")
    p.text(374, 73, "Thermostat", 14, 500)
    p.text(374, 90, "Heating" if not offline else "Last: heating", 12, 400)
    p.text(328, 128, "Target", 12, 400, "text2")
    p.text(327, 160, "21.5", 28, 400, "text2" if offline else "text")
    p.text(383, 160, "°C", 14, 400, "text2")
    p.text(328, 180, "Now 22.4 °C", 12, 400, "text2")
    for bx, ic in ((328, "minus"), (398, "plus")):
        p.rect(bx, 196, 62, 40, "chip_off", 10)
        p.icon(ic, 24, bx + 19, 204, "disabled" if offline else "text")
    dots(p, 6, 1)


def page_home(p):
    header(p, "Home")
    badges = [("lightbulb", "amber", "chip_amber", "3 lights on", 112),
              ("fire", "heat", "chip_heat", "Heating", 96), ("account", "green", "chip_green", "Home", 76)]
    x = 8
    for ic, col, bg, label, w in badges:
        p.rect(x, 40, w, 32, "card", 16, "border")
        p.chip(ic, x + 16, 56, col, bg, d=24, isz=20)
        p.text(x + 34, 61, label, 12, 500)
        x += w + 8
    card(p, 8, 80, 464, 168)
    rows = [("chair-rolling", "Office", 2, "22.4 °C"), ("sofa", "Living room", 1, "21.8 °C"),
            ("bed", "Bedroom", 0, "19.6 °C"), ("silverware-fork-knife", "Kitchen", 0, None)]
    for i, (ic, name, on, temp) in enumerate(rows):
        y = 81 + i * 42
        if i:
            p.rect(52, y - 1, 420 - 1, 1, "border")
        p.icon(ic, 24, 20, y + 9, "text2")
        p.text(56, y + 26, name, 14, 500)
        if on:
            p.icon("lightbulb", 20, 304, y + 11, "amber")
            p.text(326, y + 26, "%d on" % on, 12, 400)
        if temp:
            p.text(436, y + 26, temp, 14, 400, align="r", w=70)
        else:
            p.text(436, y + 26, "No reading", 12, 400, "text2", align="r", w=90)
        p.icon("chevron-right", 20, 444, y + 11, "text2")
    dots(p, 6, 0)


def page_light(p):
    header(p, "Ambient", back=True, right="Office")
    p.text(12, 70, "72", 28, 400)
    p.text(46, 70, "%", 16, 400, "text2")
    p.text(70, 70, "Warm white, 3000 K", 12, 400, "text2")
    p.rect(416, 40, 56, 40, "chip_amber", 10)
    p.icon("power", 24, 432, 48, "amber")
    # brightness slider (HA control slider: 40 px, radius 8, track 20 % disabled colour, white 4 px handle)
    p.icon("brightness-6", 20, 12, 98, "text2")
    sx, sw = 44, 428
    p.rect(sx, 88, sw, 40, "track", 8)
    fw = sw * 72 // 100
    p.rect(sx, 88, fw, 40, "amber", 8)
    p.rect(sx + fw - 9, 93, 4, 30, "handle", 2)
    # colour temperature, 2000..6500 K, gradient dithered
    p.icon("white-balance-sunny", 20, 12, 146, "text2")
    grad = "linear-gradient(90deg,#FF8B14,#FFA957 15%,#FFC489 33%,#FFDDB4 55%,#FFEDDC 75%,#F3F2FF)"
    p.rect(sx, 136, sw, 40, grad, 8)
    p.dither.append((sx, 136, sw, 40))
    hx = sx + 4 + (sw - 8) * (3000 - 2000) // 4500
    p.rect(hx - 3, 140, 6, 32, "#101010", 3)
    p.rect(hx - 2, 141, 4, 30, "handle", 2)
    # favourite colours
    p.icon("palette", 20, 12, 194, "text2")
    sw_cols = ["#FFC489", "#F3F2FF", "#F74539", "#FF9A00", "#4AAE52", "#2196F7", "#9C6DC6"]
    for i, c in enumerate(sw_cols):
        x = 44 + i * 48
        if i == 0:
            p.rect(x - 4, 180, 48, 48, "transparent", 12, border=None, extra="box-shadow:inset 0 0 0 2px %s" % p.c("text"))
        p.rect(x, 184, 40, 40, c, 8)
    p.rect(380, 184, 92, 40, "chip_off", 10)
    p.text(380, 209, "More", 14, 500, "primary", align="c", w=92)
    p.text(12, 258, "Changed 4 min ago by the wall switch", 12, 400, "text2")


def switch(p, x, y, on):
    p.rect(x, y, 44, 24, "primary" if on else "switch_off", 12)
    p.rect(x + (23 if on else 3), y + 3, 18, 18, "handle" if on else "text2", "50%")


def page_tools(p):
    header(p, "Tools")
    left = [("crosshairs-gps", "GPS receiver", "Fix, 7 satellites", "switch"),
            ("sync", "GPS assist", "Sent today 12:00", "Send"),
            ("bluetooth", "Pairing", "Bluetooth closed", "Open")]
    right = [("microphone", "Mic test", "Last −38 dB, OK", "Test"),
             ("volume-high", "Test tone", "1 kHz, 60 %", "Play"),
             ("usb", "USB storage", "Restarts device", "Restart")]
    for cx, rows in ((8, left), (244, right)):
        card(p, cx, 40, 228, 208)
        for i, (ic, title, status, action) in enumerate(rows):
            y = 41 + i * 69
            if i:
                p.rect(cx + 44, y - 1, 228 - 45, 1, "border")
            p.icon(ic, 24, cx + 12, y + 22, "text2")
            p.text(cx + 48, y + 30, title, 14, 500)
            p.text(cx + 48, y + 48, status, 12, 400, "text2")
            if action == "switch":
                switch(p, cx + 228 - 12 - 44, y + 22, True)
            else:
                bw = 72 if action == "Restart" else 56
                p.rect(cx + 228 - 10 - bw, y + 15, bw, 38, "chip_off", 10)
                p.text(cx + 228 - 10 - bw, y + 39, action, 14, 500, "error" if action == "Restart" else "primary",
                       align="c", w=bw)
    dots(p, 6, 5)


PAGES = [
    ("office", page_office, DARK, {}),
    ("office-light-theme", page_office, LIGHT, {}),
    ("office-offline", page_office, DARK, {"offline": True}),
    ("light-detail", page_light, DARK, {}),
    ("home", page_home, DARK, {}),
    ("tools", page_tools, DARK, {}),
]


# ---------- PNG I/O and RGB565 ----------

def png_read(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    i, idat = 8, b""
    while i < len(d):
        ln, t = struct.unpack(">I4s", d[i:i + 8])
        body = d[i + 8:i + 8 + ln]
        if t == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body)
            assert bd == 8 and ct in (2, 6) and il == 0, (bd, ct, il)
        elif t == b"IDAT":
            idat += body
        i += 12 + ln
    bpp = 4 if ct == 6 else 3
    raw, stride = zlib.decompress(idat), w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            out += line[x * bpp:x * bpp + 3]
    return w, h, out


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def to565(w, h, rgb, dither):
    out = bytearray(len(rgb))
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            dz = any(dx <= x < dx + dw and dy <= y < dy + dh for dx, dy, dw, dh in dither)
            t = (BAYER[y & 3][x & 3] + 0.5) / 16 if dz else 0.5
            for c, bits in ((0, 5), (1, 6), (2, 5)):
                m = (1 << bits) - 1
                v = min(m, int(rgb[i + c] * m / 255 + t)) if dz else round(rgb[i + c] * m / 255)
                out[i + c] = (v << (8 - bits)) | (v >> (2 * bits - 8))
    return out


def png_write(path, w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roboto", required=True, help="Roboto variable TTF (google/fonts ofl/roboto)")
    ap.add_argument("--svg-dir", required=True, help="svg/ folder of @mdi/svg")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "ui-mockups"))
    ap.add_argument("--keep-html", help="also write the HTML pages to this folder")
    ap.add_argument("--chrome", default=CHROME)
    a = ap.parse_args()

    def b64(path):
        return base64.b64encode(open(path, "rb").read()).decode()

    fonts = ("@font-face{font-family:R;src:url(data:font/ttf;base64,%s)}"
             "@font-face{font-family:P;font-weight:600;src:url(data:font/ttf;base64,%s)}"
             % (b64(a.roboto), b64(os.path.join(ROOT, "brand", "fonts", "Poppins-SemiBold.ttf"))))
    cache = {}

    def icons(name, size):
        if (name, size) not in cache:
            lv = gen_icons.quantise(gen_icons.raster(gen_icons.parse_path(gen_icons.read_path(a.svg_dir, name)), size), 2)
            rgba = b"".join(bytes((255, 255, 255, v * 85)) for v in lv)
            with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
                tmp = f.name
            gen_icons.png(tmp, size, size, rgba)
            cache[(name, size)] = "data:image/png;base64," + b64(tmp)
            os.unlink(tmp)
        return cache[(name, size)]

    os.makedirs(a.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        for name, fn, theme, kw in PAGES:
            p = Page(dict(theme), icons)
            fn(p, **kw)
            hp = os.path.join(td, name + ".html")
            with open(hp, "w", encoding="utf-8") as f:
                f.write(p.html(fonts))
            if a.keep_html:
                os.makedirs(a.keep_html, exist_ok=True)
                with open(os.path.join(a.keep_html, name + ".html"), "w", encoding="utf-8") as f:
                    f.write(p.html(fonts))
            shot = os.path.join(td, name + ".png")
            subprocess.run([a.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
                            "--force-device-scale-factor=1", "--window-size=%d,%d" % (W, H),
                            "--user-data-dir=" + os.path.join(td, "profile"), "--screenshot=" + shot, "file://" + hp],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            w, h, rgb = png_read(shot)
            assert (w, h) == (W, H), (w, h)
            png_write(os.path.join(a.out, name + ".png"), w, h, to565(w, h, rgb, p.dither))
            sys.stderr.write("%s.png\n" % name)


if __name__ == "__main__":
    main()

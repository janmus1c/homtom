"""Host-only reference algorithm for a future NOR writer, run against the en29lv400bb model.

This is not a device tool: it talks to an object with read(word), write(word, data) and wait(us), and has no
/dev/mem path. A device writer would port these functions to C line for line (docs/NOR_PLAN.md section 9).

Plans:
- r1_plan(image): the no-erase black logo patch. For every word of record 0 (id 0, 0x28201-0x2919A) that
  holds a non-zero pixel value byte, the target word keeps the RLE count and header bytes and sets the value
  bytes to 0. Every target is a bit-subset of the stock word, so it is clear-only.
- n4_plan(image): erase SA10 (byte 0x70000) and program its header back. The header spans bytes
  0x70000-0x70029 (0x2A bytes; docs/NOR_PLAN.md said 0x29, which would drop the last byte, 0x00).
"""

import hashlib

RECORD0 = 0x28201
R1_LO, R1_HI = 0x28211, 0x2919A  # byte allowlist (inclusive): record 0 pixel data
STOCK_SHA = "d48b41abc85cfd6f860cdb1f3321987caf66fd70e799856d6429f97aa128ead5"


class WriterError(Exception):
    pass


def walk_record(img, off):
    """Return (header, end, count_bytes, value_bytes) for the RLE record at byte offset off."""
    ln = int.from_bytes(img[off:off + 4], "big")
    rid, w, h = (int.from_bytes(img[off + 4 + 4 * i:off + 8 + 4 * i], "big") for i in range(3))
    p, px, counts, values = off + 16, 0, [], []
    while px < w * h:
        n = img[p]
        counts.append(p)
        p += 1
        if n < 0x80:
            if n == 0:
                raise ValueError("zero run at %#x" % (p - 1))
            values.append(p)
            p += 1
            px += n
        else:
            k = 256 - n
            values.extend(range(p, p + k))
            p += k
            px += k
    if px != w * h or p != off + 8 + ln:
        raise ValueError("record does not end at its length")
    return (ln, rid, w, h), p, counts, values


def r1_plan(img):
    _, end, _, values = walk_record(img, RECORD0)
    target = bytearray(img)
    for a in values:
        target[a] = 0
    plan = []
    for word in sorted({a // 2 for a in values if img[a]}):
        new = target[2 * word] | target[2 * word + 1] << 8
        plan.append((word, new))
    return plan, bytes(target)


def n4_plan(img):
    base = 0x70000
    target = bytearray(img)
    target[base:base + 0x10000] = b"\xff" * 0x10000
    target[base:base + 0x2A] = img[base:base + 0x2A]
    words = [(w, target[2 * w] | target[2 * w + 1] << 8) for w in range(base // 2, (base + 0x2A) // 2)]
    return [w for w in words if w[1] != 0xFFFF], bytes(target)


def unlock(bus):
    bus.write(0x555, 0xAA)
    bus.write(0x2AA, 0x55)


def reset(bus):
    bus.write(0x000, 0xF0)


def wait_ready(bus, word, timeout_us, poll_us=0.0):
    """Toggle-bit algorithm: DQ6 stops toggling when done; on DQ5 re-read twice, still toggling = failure."""
    start = bus.now
    while True:
        a = bus.read(word)
        b = bus.read(word)
        if (a ^ b) & 0x40 == 0:
            return "ok"
        if b & 0x20:
            a = bus.read(word)
            b = bus.read(word)
            if (a ^ b) & 0x40 == 0:
                return "ok"
            reset(bus)
            return "dq5"
        if bus.now - start > timeout_us:
            reset(bus)
            return "timeout"
        if poll_us:
            bus.wait(poll_us)


class Writer:
    """Allowlisted program and erase. Every refusal happens before the first bus write of an operation."""

    def __init__(self, bus, allow_words, allow_sectors=(), program_timeout_us=1000.0, erase_timeout_us=20e6):
        self.bus = bus
        self.allow_words = set(allow_words)
        self.allow_sectors = set(allow_sectors)
        self.program_timeout_us, self.erase_timeout_us = program_timeout_us, erase_timeout_us
        self.log = []

    def program_word(self, word, data):
        if word not in self.allow_words:
            raise WriterError("word %#x outside allowlist" % word)
        old = self.bus.read(word)
        if old == data:
            self.log.append(("skip", word))
            return
        if data & ~old & 0xFFFF:
            raise WriterError("word %#x: %04x -> %04x needs a 0->1 change" % (word, old, data))
        unlock(self.bus)
        self.bus.write(0x555, 0xA0)
        self.bus.write(word, data)
        r = wait_ready(self.bus, word, self.program_timeout_us)
        got = self.bus.read(word)
        self.log.append((r, word, got))
        if r != "ok" or got != data:
            raise WriterError("word %#x: %s, read %04x, want %04x" % (word, r, got, data))

    def erase_sector(self, sector, base_word):
        if sector not in self.allow_sectors:
            raise WriterError("sector %d outside allowlist" % sector)
        unlock(self.bus)
        self.bus.write(0x555, 0x80)
        unlock(self.bus)
        self.bus.write(base_word, 0x30)
        r = wait_ready(self.bus, base_word, self.erase_timeout_us, poll_us=1000.0)
        self.log.append((r, "erase", sector))
        if r != "ok":
            raise WriterError("erase sector %d: %s" % (sector, r))


def read_image(bus, words=0x40000):
    out = bytearray()
    for w in range(words):
        v = bus.read(w)
        out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def run_r1(chip, img):
    """Full R1 procedure: pre-image gate, plan, program, whole-chip verify. Resumable after interruption."""
    plan, target = r1_plan(img)
    pre = chip.image()
    for a in range(len(pre)):
        if pre[a] != img[a] and not (R1_LO <= a <= R1_HI and pre[a] & ~img[a] & 0xFF == 0):
            raise WriterError("pre-image differs outside the R1 allowlist at %#x" % a)
    w = Writer(chip, {word for word, _ in plan})
    for word, data in plan:
        w.program_word(word, data)
    if chip.image() != target:
        raise WriterError("post-image mismatch")
    return w, target

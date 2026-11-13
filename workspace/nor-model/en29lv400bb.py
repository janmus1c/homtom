"""Host software model of the EON EN29LV400BB in x16 mode (AMD/Spansion command set, bottom boot).

Addresses are 16-bit word addresses (byte address / 2). The chip has 0x40000 words; higher address bits
alias, as measured on the GO 630 (physical 0x80000 reads like 0x00000).

Modelled behaviour, with sources:
- [M] measured on the device by norid/norid2 (2026-11-07): autoselect word 0 = 0x007F, word 0x100 = 0x001C,
  word 1 = 0x22BA, sector base + 2 = protect status (0x0001 for SA0, 0x0000 elsewhere), word 3 = 0x0000,
  CFI query (98@55) is ignored and the array stays in read mode, F0 returns to read mode.
- [V] EON EN29LV400 datasheet Rev. C (2004/03/18) and EN29LV400A Rev. I (2016/05/23), see docs/NOR_PLAN.md
  section 9.5: command table (AA@555, 55@2AA, then 90/A0/80 at 555; sector erase 30 at SA; F0 at any address);
  incorrect addresses, data or sequences reset the device to read mode; program only clears bits and a 0->1
  attempt ends in DQ5 = 1 that needs F0; reset is ignored once programming or erasure has begun; a program to
  a protected sector toggles DQ6 for about 2 us and an erase of only protected sectors for about 100 us, then
  the chip returns to read mode with the data unchanged (no DQ5); DQ7 reads the complement of the programmed
  bit and 0 during erase; DQ6 toggles on every read while busy; the device does not support multiple sector
  erase, so DQ3 reads 1 straight after the first 30 (no 50 us window); DQ2 toggles when reading a sector that
  is being erased; word program 8 us typical, 300 us max; sector erase 0.5 s typical, 10 s max.
- [H] not stated by the datasheet: which address bits the unlock cycles decode (modelled as A10-A0) and that
  the upper data byte of a command is ignored; what an invalid sequence does while in autoselect mode
  (modelled as staying in autoselect); erase suspend (B0) and unlock bypass (20, dropped from the A datasheet
  in its Rev. C) are not modelled because no writer uses them.

Time is virtual: every bus access advances the clock by `access_us`, and `wait(us)` advances it further.
Fault hooks: `power_loss()`, `stuck_busy` (operation never completes), `autoselect_bank1` (word 0x100 value).
"""

import random

WORDS = 0x40000
SECTORS = [  # (first word, word count): 16K, 8K, 8K, 32K, 7 x 64K bytes
    (0x00000, 0x2000), (0x02000, 0x1000), (0x03000, 0x1000), (0x04000, 0x4000),
    (0x08000, 0x8000), (0x10000, 0x8000), (0x18000, 0x8000), (0x20000, 0x8000),
    (0x28000, 0x8000), (0x30000, 0x8000), (0x38000, 0x8000),
]

READ, CYC1, CYC2, AUTOSEL, PROG_SETUP, ERASE_CYC3, ERASE_CYC4, ERASE_CYC5, BUSY_PROG, BUSY_ERASE, ERASE_WINDOW, FAILED = range(12)


def sector_of(word):
    word %= WORDS
    for i, (base, n) in enumerate(SECTORS):
        if base <= word < base + n:
            return i
    raise AssertionError(word)


class EN29LV400BB:
    def __init__(self, image=None, protected=(0,), access_us=1.0, program_us=8.0, program_max_us=300.0,
                 erase_us=500000.0, erase_max_us=10000000.0, autoselect_bank1=0x001C, seed=1):
        if image is None:
            self.cells = [0xFFFF] * WORDS
        else:
            assert len(image) == WORDS * 2
            self.cells = [image[2 * i] | image[2 * i + 1] << 8 for i in range(WORDS)]
        self.protected = set(protected)
        self.access_us, self.program_us, self.program_max_us = access_us, program_us, program_max_us
        self.erase_us, self.erase_max_us = erase_us, erase_max_us
        self.autoselect_bank1 = autoselect_bank1
        self.rng = random.Random(seed)
        self.state = READ
        self.after_unlock = READ  # state to return to after an autoselect-mode unlock
        self.now = 0.0
        self.busy_until = 0.0
        self.toggle = 0
        self.prog_word = self.prog_data = None
        self.erase_sectors = []
        self.stuck_busy = False
        self.writes = []  # (time, word, data, state) log of every bus write
        self.commands_seen = set()

    # --- bus ---------------------------------------------------------------------------------------
    def image(self):
        return b"".join(bytes((c & 0xFF, c >> 8)) for c in self.cells)

    def wait(self, us):
        self.now += us
        self._advance()

    def _advance(self):
        if self.state in (BUSY_PROG, BUSY_ERASE) and not self.stuck_busy and self.now >= self.busy_until:
            self._finish()

    def read(self, word):
        self.now += self.access_us
        self._advance()
        word %= WORDS
        if self.state in (BUSY_PROG, BUSY_ERASE, FAILED):
            return self._status(word)
        if self.state == AUTOSEL or (self.state in (CYC1, CYC2) and self.after_unlock == AUTOSEL):
            return self._autoselect(word)
        return self.cells[word]

    def write(self, word, data):
        self.now += self.access_us
        self._advance()
        word %= WORDS
        self.writes.append((self.now, word, data, self.state))
        low = word & 0x7FF
        d = data & 0xFF
        st = self.state
        if st in (BUSY_PROG, BUSY_ERASE):
            return  # reset and every other command are ignored while an embedded operation runs
        if st == FAILED:
            if d == 0xF0:
                self.state = READ
            return
        if st == PROG_SETUP:
            self._start_program(word, data)
            return
        if d == 0xF0:
            self.state = READ
            self.after_unlock = READ
            return
        if st in (READ, AUTOSEL) and low == 0x555 and d == 0xAA:
            self.after_unlock = st
            self.state = CYC1
            return
        if st == CYC1 and low == 0x2AA and d == 0x55:
            self.state = CYC2
            return
        if st == CYC2 and low == 0x555:
            self.commands_seen.add(d)
            if d == 0x90:
                self.state = AUTOSEL
                return
            if d == 0xA0:
                self.state = PROG_SETUP
                return
            if d == 0x80:
                self.state = ERASE_CYC3
                return
        if st == ERASE_CYC3 and low == 0x555 and d == 0xAA:
            self.state = ERASE_CYC4
            return
        if st == ERASE_CYC4 and low == 0x2AA and d == 0x55:
            self.state = ERASE_CYC5
            return
        if st == ERASE_CYC5:
            if low == 0x555 and d == 0x10:
                self.erase_sectors = [i for i in range(len(SECTORS))]
                self._start_erase(chip=True)
                return
            if d == 0x30:
                self.erase_sectors = [sector_of(word)]  # [V] one sector per command; erase starts at once
                self._start_erase()
                return
        # 98 (CFI query) is ignored [M]; anything else that does not continue a sequence returns to the mode
        # the chip was in before the unlock (read or autoselect)
        if st == READ and low == 0x055 and d == 0x98:
            return
        self.state = self.after_unlock if st in (CYC1, CYC2) else (AUTOSEL if st == AUTOSEL else READ)
        if self.state == READ:
            self.after_unlock = READ

    # --- internals ---------------------------------------------------------------------------------
    def _autoselect(self, word):
        off = word & 0xFF
        if off == 0:
            return 0x007F if not (word & 0x100) else self.autoselect_bank1
        if off == 1:
            return 0x22BA
        if off == 2:
            return 1 if sector_of(word) in self.protected else 0
        return 0x0000

    def _start_program(self, word, data):
        self.prog_word, self.prog_data = word, data & 0xFFFF
        self.toggle = 0
        if sector_of(word) in self.protected:
            self.state, self.busy_until, self.prog_fail = BUSY_PROG, self.now + 2.0, False  # [V] about 2 us
            self.prog_effect = False
            return
        self.prog_effect = True
        self.prog_fail = bool(data & ~self.cells[word] & 0xFFFF)
        self.state = BUSY_PROG
        self.busy_until = self.now + (self.program_max_us if self.prog_fail else self.program_us)

    def _start_erase(self, chip=False):
        targets = [s for s in self.erase_sectors if s not in self.protected]
        self.erase_targets = targets
        self.state = BUSY_ERASE
        self.toggle = 0
        self.busy_until = self.now + (self.erase_us * max(1, len(targets)) if targets else 100.0)
        self.erase_fail = False

    def _finish(self):
        if self.state == BUSY_PROG:
            if self.prog_effect:
                self.cells[self.prog_word] &= self.prog_data
            if self.prog_effect and self.prog_fail:
                self.state = FAILED
                return
        else:
            for s in self.erase_targets:
                base, n = SECTORS[s]
                for w in range(base, base + n):
                    self.cells[w] = 0xFFFF
        self.state = READ
        self.after_unlock = READ

    def _status(self, word):
        self.toggle ^= 0x40
        if self.state in (BUSY_PROG, FAILED):
            dq7 = (~self.prog_data) & 0x80
            dq5 = 0x20 if self.state == FAILED else 0
            v = dq7 | self.toggle | dq5
        else:
            dq3 = 0x08
            dq2 = 0x04 if (sector_of(word) in self.erase_sectors
                           and (self.toggle & 0x40)) else 0
            v = self.toggle | dq3 | dq2
        return v | (v << 8)

    # --- faults ------------------------------------------------------------------------------------
    def power_loss(self):
        """Cut power now. An interrupted program clears a random subset of the bits it was clearing; an
        interrupted erase leaves every word of each target sector in a random state. Power-up is read mode."""
        if self.state == BUSY_PROG and self.prog_effect:
            clearing = self.cells[self.prog_word] & ~self.prog_data & 0xFFFF
            mask = self.rng.getrandbits(16) & clearing
            self.cells[self.prog_word] &= ~mask & 0xFFFF
        elif self.state == BUSY_ERASE:
            for s in self.erase_targets:
                base, n = SECTORS[s]
                for w in range(base, base + n):
                    self.cells[w] = self.rng.getrandbits(16)
        self.state = READ
        self.after_unlock = READ
        self.stuck_busy = False

"""Tests for the EN29LV400BB model and the reference writer. Run: python3 -m unittest -v (in this directory)."""

import os
import random
import unittest

from en29lv400bb import EN29LV400BB, SECTORS, WORDS, READ, sector_of
import writer_ref as wr

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STOCK = os.path.join(ROOT, "artifacts", "nor-analysis", "n1-n2-20261107", "nor-n1-linux61.bin")
GZIP_END = 0x247DD  # first byte after the compressed bootloader tail


def stock():
    with open(STOCK, "rb") as f:
        img = f.read()
    assert wr.sha(img) == wr.STOCK_SHA
    return img


def diff_bytes(a, b):
    return [i for i in range(len(a)) if a[i] != b[i]]


class Geometry(unittest.TestCase):
    def test_sector_map(self):
        sizes = [n * 2 for _, n in SECTORS]
        self.assertEqual(sizes, [0x4000, 0x2000, 0x2000, 0x8000] + [0x10000] * 7)
        self.assertEqual(sum(sizes), 0x80000)
        for (b, n), (b2, _) in zip(SECTORS, SECTORS[1:]):
            self.assertEqual(b + n, b2)
        self.assertEqual(sector_of(0x28216 // 2), 5)
        self.assertEqual(sector_of(0x70000 // 2), 10)
        self.assertEqual(sector_of(WORDS), 0)  # alias


class Identify(unittest.TestCase):
    """The measured N2 sequence must reproduce the device's answers and leave the array untouched."""

    def test_autoselect_and_reset(self):
        img = stock()
        c = EN29LV400BB(img)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x90)
        self.assertEqual([c.read(w) for w in range(4)], [0x007F, 0x22BA, 0x0001, 0x0000])
        bank = [c.read(w) for w in (0x100, 0x101, 0x102, 0x103, 0x200)]
        self.assertEqual(bank, [0x001C, 0x22BA, 0x0001, 0x0000, 0x007F])  # N2b, measured
        prot = [c.read(b + 2) for b, _ in SECTORS]
        self.assertEqual(prot, [1] + [0] * 10)
        c.write(0, 0xF0)
        self.assertEqual(c.image(), img)
        self.assertEqual(c.read(0), img[0] | img[1] << 8)

    def test_cfi_query_ignored(self):
        img = stock()
        c = EN29LV400BB(img)
        c.write(0x055, 0x98)
        self.assertEqual(c.read(0x10), img[0x20] | img[0x21] << 8)  # measured: 0x8004, the array
        self.assertEqual(c.read(0x10), 0x8004)

    def test_wrong_unlock_returns_to_read(self):
        img = stock()
        c = EN29LV400BB(img)
        c.write(0x555, 0xAA); c.write(0x2AB, 0x55); c.write(0x555, 0xA0); c.write(0x14108, 0x0000)
        self.assertEqual(c.state, READ)
        self.assertEqual(c.image(), img)

    def test_upper_address_and_data_bits_dont_care(self):
        c = EN29LV400BB()
        c.write(0x3F555, 0xAAAA); c.write(0x002AA, 0x5555); c.write(0x00555, 0x9090)
        self.assertEqual(c.read(0), 0x007F)


class Program(unittest.TestCase):
    def test_program_clears_only(self):
        c = EN29LV400BB()
        w = wr.Writer(c, {0x20000})
        w.program_word(0x20000, 0x1234)
        self.assertEqual(c.read(0x20000), 0x1234)
        w.program_word(0x20000, 0x1030)
        self.assertEqual(c.read(0x20000), 0x1030)

    def test_zero_to_one_fails_with_dq5_and_needs_reset(self):
        c = EN29LV400BB()
        c.cells[0x20000] = 0x00FF
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0xA0); c.write(0x20000, 0xFF0F)
        self.assertEqual(wr.wait_ready(c, 0x20000, 1000.0), "dq5")
        self.assertEqual(c.state, READ)  # wait_ready issued F0
        self.assertEqual(c.read(0x20000), 0x000F)  # the 0 bits that were requested did clear

    def test_writer_refuses_zero_to_one_before_bus_write(self):
        c = EN29LV400BB()
        c.cells[0x20000] = 0x00FF
        w = wr.Writer(c, {0x20000})
        n = len(c.writes)
        with self.assertRaises(wr.WriterError):
            w.program_word(0x20000, 0x01FF)
        self.assertEqual(len(c.writes), n)

    def test_protected_sector_unchanged(self):
        c = EN29LV400BB()
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0xA0); c.write(0x00010, 0x0000)
        self.assertEqual(wr.wait_ready(c, 0x10, 1000.0), "ok")
        self.assertEqual(c.read(0x10), 0xFFFF)

    def test_allowlist_refuses_before_bus_write(self):
        c = EN29LV400BB()
        w = wr.Writer(c, {0x20000})
        with self.assertRaises(wr.WriterError):
            w.program_word(0x10000, 0)
        self.assertEqual(c.writes, [])

    def test_reset_ignored_while_busy(self):
        c = EN29LV400BB(program_us=50.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0xA0); c.write(0x20000, 0x0000)
        c.write(0, 0xF0)
        self.assertNotEqual(c.read(5) ^ c.read(5), 0)  # still toggling
        self.assertEqual(wr.wait_ready(c, 0x20000, 1000.0), "ok")
        self.assertEqual(c.read(0x20000), 0)

    def test_dq7_complement_during_program(self):
        c = EN29LV400BB(program_us=50.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0xA0); c.write(0x20000, 0x0000)
        self.assertEqual(c.read(0x20000) & 0x80, 0x80)

    def test_stuck_busy_times_out(self):
        c = EN29LV400BB()
        c.stuck_busy = True
        w = wr.Writer(c, {0x20000})
        with self.assertRaises(wr.WriterError) as e:
            w.program_word(0x20000, 0)
        self.assertIn("timeout", str(e.exception))


class Erase(unittest.TestCase):
    def test_sector_erase_sa10_only(self):
        img = stock()
        c = EN29LV400BB(img, erase_us=5000.0)
        w = wr.Writer(c, set(), allow_sectors={10})
        w.erase_sector(10, 0x38000)
        out = c.image()
        self.assertEqual(out[0x70000:], b"\xff" * 0x10000)
        self.assertEqual(out[:0x70000], img[:0x70000])

    def test_dq3_dq2_during_erase(self):
        img = stock()
        c = EN29LV400BB(img, erase_us=5000.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x80)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x38000, 0x30)
        self.assertEqual(c.read(0x38000) & 0x88, 0x08)  # [V] no multi-sector window: DQ7 0, DQ3 1 at once
        a, b = c.read(0x38000), c.read(0x38000)
        self.assertEqual(a & 0x08, 0x08)
        self.assertNotEqual((a ^ b) & 0x04, 0)  # DQ2 toggles in the erasing sector
        a, b = c.read(0x20000), c.read(0x20000)
        self.assertEqual((a ^ b) & 0x04, 0)

    def test_erase_protected_sector_is_noop(self):
        img = stock()
        c = EN29LV400BB(img, erase_us=5000.0)
        w = wr.Writer(c, set(), allow_sectors={0})
        w.erase_sector(0, 0)
        self.assertEqual(c.image(), img)

    def test_chip_erase_skips_protected(self):
        img = stock()
        c = EN29LV400BB(img, erase_us=100.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x80)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x10)
        self.assertEqual(wr.wait_ready(c, 0, 1e6, poll_us=10.0), "ok")
        out = c.image()
        self.assertEqual(out[:0x4000], img[:0x4000])
        self.assertEqual(out[0x4000:], b"\xff" * (0x80000 - 0x4000))

    def test_erase_sector_refused_outside_allowlist(self):
        c = EN29LV400BB()
        w = wr.Writer(c, set(), allow_sectors={10})
        with self.assertRaises(wr.WriterError):
            w.erase_sector(5, 0x10000)
        self.assertEqual(c.writes, [])

    def test_n4_erase_then_header_and_mid_erase_power_loss(self):
        img = stock()
        plan, target = wr.n4_plan(img)
        self.assertEqual(diff_bytes(img, target), [])  # SA10 holds only the 0x2A-byte header plus 0xFF
        self.assertEqual(len(plan), 21)
        c = EN29LV400BB(img, erase_us=5000.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x80)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x38000, 0x30)
        c.wait(2000)
        c.power_loss()
        self.assertNotEqual(c.image()[0x70000:], img[0x70000:])
        self.assertEqual(c.image()[:0x70000], img[:0x70000])
        w = wr.Writer(c, {wd for wd, _ in plan}, allow_sectors={10})
        w.erase_sector(10, 0x38000)
        for wd, d in plan:
            w.program_word(wd, d)
        self.assertEqual(c.image(), img)

    def r3_ops(self, homtom):
        """SA6 erase and program, then SA5 erase, gzip tail (0x20000-0x247DC) first, then the rest of SA5."""
        ops = []
        for sector, base in ((6, 0x18000), (5, 0x10000)):
            ops.append(("erase", sector, base))
            words = [w for w in range(base, base + 0x8000) if homtom[2 * w:2 * w + 2] != b"\xff\xff"]
            words.sort(key=lambda w: (w >= 0x247DE // 2, w))
            ops += [("prog", w, homtom[2 * w] | homtom[2 * w + 1] << 8) for w in words]
        return ops

    def test_r3_sequence_and_brick_window(self):
        img = stock()
        with open(os.path.join(ROOT, "artifacts", "homtom-nor", "mtd0-homtom.bin"), "rb") as f:
            homtom = f.read()
        ops = self.r3_ops(homtom)
        allow = {o[1] for o in ops if o[0] == "prog"}
        c = EN29LV400BB(img, erase_us=2000.0)
        w = wr.Writer(c, allow, allow_sectors={5, 6})
        for o in ops:
            w.erase_sector(o[1], o[2]) if o[0] == "erase" else w.program_word(o[1], o[2])
        self.assertEqual(c.image(), homtom)
        # a power cut during the SA5 erase leaves SA0-SA4 intact but destroys the gzip tail at 0x20000
        c = EN29LV400BB(img, erase_us=2000.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x80)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x10000, 0x30)
        c.wait(1000)
        c.power_loss()
        out = c.image()
        self.assertEqual(out[:0x20000], img[:0x20000])
        self.assertNotEqual(out[0x20000:GZIP_END], img[0x20000:GZIP_END])


class Datasheet(unittest.TestCase):
    """[V] points from the EN29LV400 / EN29LV400A datasheets (docs/NOR_PLAN.md 9.5)."""

    def test_protected_program_toggles_about_2us_no_dq5(self):
        c = EN29LV400BB(access_us=0.5)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0xA0); c.write(0x800, 0x00F0)
        a, b = c.read(0x800), c.read(0x800)
        self.assertNotEqual((a ^ b) & 0x40, 0)
        self.assertEqual(b & 0x20, 0)
        c.wait(2.0)
        self.assertEqual(c.read(0x800), 0xFFFF)

    def test_sector_erase_starts_at_once_and_ignores_reset(self):
        img = stock()
        c = EN29LV400BB(img, erase_us=5000.0)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x555, 0x80)
        c.write(0x555, 0xAA); c.write(0x2AA, 0x55); c.write(0x38000, 0x30)
        c.write(0x30000, 0x30)  # no multi-sector erase: a second 30 is ignored
        c.write(0, 0xF0)  # reset is ignored once erasure has begun
        self.assertEqual(wr.wait_ready(c, 0x38000, 1e6, poll_us=100.0), "ok")
        out = c.image()
        self.assertEqual(out[:0x70000], img[:0x70000])  # SA9 (0x60000) kept its header
        self.assertEqual(out[0x70000:], b"\xff" * 0x10000)

    def test_timing_defaults(self):
        c = EN29LV400BB()
        self.assertEqual((c.program_us, c.program_max_us, c.erase_us, c.erase_max_us), (8.0, 300.0, 5e5, 1e7))


class R1(unittest.TestCase):
    def test_plan_shape(self):
        img = stock()
        hdr, end, counts, values = wr.walk_record(img, wr.RECORD0)
        self.assertEqual(hdr, (3986, 0, 237, 51))
        self.assertEqual(end, wr.R1_HI + 1)
        self.assertEqual(len(counts), 1377)
        self.assertEqual(len(values), 2601)
        self.assertEqual(sum(1 for a in values if img[a]), 2257)
        plan, target = wr.r1_plan(img)
        self.assertEqual(len(plan), 1669)
        changed = diff_bytes(img, target)
        self.assertEqual(len(changed), 2257)
        self.assertTrue(all(wr.R1_LO <= a <= wr.R1_HI for a in changed))
        for word, new in plan:
            old = img[2 * word] | img[2 * word + 1] << 8
            self.assertEqual(new & ~old & 0xFFFF, 0)  # clear-only
        self.assertEqual(img[0x28000:0x28002], b"\x00\x00")  # palette index 0 is black

    def test_r1_result_decodes_black_and_chain_intact(self):
        img = stock()
        c = EN29LV400BB(img)
        w, target = wr.run_r1(c, img)
        out = wr.read_image(c)
        self.assertEqual(out, target)
        _, end, _, values = wr.walk_record(out, wr.RECORD0)
        self.assertTrue(all(out[a] == 0 for a in values))
        p = wr.RECORD0
        ids = []
        while int.from_bytes(out[p:p + 4], "big"):
            hdr, end, _, _ = wr.walk_record(out, p)
            ids.append(hdr[1])
            p = end
        self.assertEqual(len(ids), 24)
        self.assertEqual(out[:wr.R1_LO], img[:wr.R1_LO])
        self.assertEqual(out[wr.R1_HI + 1:], img[wr.R1_HI + 1:])
        self.assertTrue(all(r[0] == "ok" for r in w.log))
        bus_writes = {wd for _, wd, _, _ in c.writes}
        self.assertTrue(bus_writes <= {0x555, 0x2AA, 0x000} | {wd for wd, _ in wr.r1_plan(img)[0]})
        self.assertNotIn(0x80, c.commands_seen)
        print("\n  R1 model run: %d program ops, %.1f ms virtual bus time, %d bus writes"
              % (len(w.log), c.now / 1000, len(c.writes)))

    def test_r1_power_loss_anywhere_is_safe_and_resumable(self):
        img = stock()
        plan, target = wr.r1_plan(img)
        rng = random.Random(7)
        cuts = sorted(rng.sample(range(1, 1669 * 6), 200))
        for cut in cuts:
            c = EN29LV400BB(img, seed=cut)
            n = 0
            orig_write = c.write

            def counting_write(word, data, c=c):
                nonlocal n
                n += 1
                if n == cut:
                    orig_write(word, data)
                    c.power_loss()
                    raise KeyboardInterrupt
                orig_write(word, data)
            c.write = counting_write
            w = wr.Writer(c, {wd for wd, _ in plan})
            try:
                for wd, d in plan:
                    w.program_word(wd, d)
            except KeyboardInterrupt:
                pass
            mid = c.image()
            bad = [a for a in diff_bytes(img, mid) if not (wr.R1_LO <= a <= wr.R1_HI)]
            self.assertEqual(bad, [], "cut %d changed bytes outside the allowlist" % cut)
            _, _, counts, values = wr.walk_record(mid, wr.RECORD0)  # chain and lengths still decode
            self.assertTrue(all(mid[a] == img[a] for a in counts))
            c.write = orig_write
            wr.run_r1(c, img)
            self.assertEqual(c.image(), target)

    def test_r1_rerun_is_noop(self):
        img = stock()
        c = EN29LV400BB(img)
        wr.run_r1(c, img)
        before = len(c.writes)
        w, _ = wr.run_r1(c, img)
        self.assertEqual(len(c.writes), before)
        self.assertTrue(all(r[0] == "skip" for r in w.log))

    def test_r1_refuses_foreign_image(self):
        img = bytearray(stock())
        img[0x8000] ^= 0x01
        c = EN29LV400BB(bytes(img))
        with self.assertRaises(wr.WriterError):
            wr.run_r1(c, stock())
        self.assertEqual(c.writes, [])

    def test_r1_with_protected_sa5_fails_closed(self):
        img = stock()
        c = EN29LV400BB(img, protected=(0, 5))
        with self.assertRaises(wr.WriterError):
            wr.run_r1(c, img)
        self.assertEqual(c.image(), img)
        self.assertEqual(c.state, READ)


if __name__ == "__main__":
    unittest.main()

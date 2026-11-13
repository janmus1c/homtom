import random
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "firmware"))
import norimage  # noqa: E402

DUMPS = sorted((ROOT / "artifacts").glob("bootloader-dump-*/mtd0.bin"))
DUMP = DUMPS[-1] if DUMPS else None


class RleTest(unittest.TestCase):
    def test_random_pixels_round_trip(self):
        rng = random.Random(1)
        for _ in range(200):
            px = bytes(rng.choice([0, 0, 0, 1, 2, 3, 200]) for _ in range(rng.randint(1, 600)))
            self.assertEqual(norimage.decode_rle(norimage.encode_rle(px), len(px)), px)

    def test_long_run_is_split_at_127(self):
        self.assertEqual(norimage.encode_rle(b"\x07" * 130), bytes([127, 7, 3, 7]))


@unittest.skipUnless(DUMP, "NOR dump not present")
class NorDumpTest(unittest.TestCase):
    def setUp(self):
        self.data = DUMP.read_bytes()
        self.info = norimage.parse(self.data)

    def test_expected_image_table(self):
        self.assertEqual(len(self.info["images"]), 24)
        self.assertEqual((self.info["images"][1]["width"], self.info["images"][1]["height"]), (480, 272))

    def test_every_image_re_encodes_byte_identically(self):
        for img in self.info["images"]:
            self.assertEqual(norimage.encode_rle(img["pixels"]), img["blob"], img["id"])

    def test_whole_region_rebuilds_identically(self):
        region = norimage.build_region(self.info["palette565"], self.info["pre"],
                                       [(i["id"], i["width"], i["height"], i["pixels"]) for i in self.info["images"]],
                                       self.info["trailer"])
        start = norimage.PALETTE_OFFSET
        self.assertEqual(self.data[start:start + len(region)], region)

    def test_cli_extracts_pngs(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(norimage.main(["extract", str(DUMP), tmp]), 0)
            self.assertEqual(len(list(Path(tmp).glob("*.png"))), 24)


if __name__ == "__main__":
    unittest.main()

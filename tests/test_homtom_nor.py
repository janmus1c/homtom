import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "firmware"))
import norimage  # noqa: E402

DUMPS = sorted((ROOT / "artifacts").glob("bootloader-dump-*/mtd0.bin"))
DUMP = DUMPS[-1] if DUMPS else None
PATCHED = ROOT / "artifacts" / "homtom-nor" / "mtd0-homtom.bin"
PALETTE = ROOT / "artifacts" / "homtom-nor" / "palette.bin"


@unittest.skipUnless(DUMP and PATCHED.exists(), "NOR dump or patched image not present")
class HomtomNorTest(unittest.TestCase):
    def setUp(self):
        self.orig_data = DUMP.read_bytes()
        self.new_data = PATCHED.read_bytes()
        self.orig = norimage.parse(self.orig_data)
        self.new = norimage.parse(self.new_data)

    def test_outside_image_region_identical(self):
        self.assertEqual(len(self.new_data), len(self.orig_data))
        end = self.new["end"] + 8
        self.assertEqual(self.new_data[:norimage.PALETTE_OFFSET], self.orig_data[:norimage.PALETTE_OFFSET])
        self.assertEqual(self.new_data[end:], self.orig_data[end:])
        self.assertEqual(self.new_data[0x28200], self.orig_data[0x28200])

    def test_terminator_and_region_end(self):
        end = self.new["end"] + 8
        self.assertEqual(self.new_data[self.new["end"]:end], b"\0" * 8)
        self.assertLessEqual(end, norimage.REGION_END)
        self.assertLessEqual(end, 0x40000)
        self.assertEqual(self.new["images"][0]["offset"], norimage.RECORDS_OFFSET)

    def test_ids_and_sizes_match_original(self):
        key = lambda im: (im["id"], im["width"], im["height"])
        self.assertEqual(len(self.new["images"]), 24)
        self.assertEqual([key(i) for i in self.new["images"]], [key(i) for i in self.orig["images"]])

    def test_decoding_round_trips(self):
        for im in self.new["images"]:
            self.assertEqual(len(im["pixels"]), im["width"] * im["height"])
            self.assertEqual(norimage.encode_rle(im["pixels"]), im["blob"])
        region = norimage.build_region(self.new["palette565"], self.new["pre"],
                                       [(i["id"], i["width"], i["height"], i["pixels"]) for i in self.new["images"]])
        self.assertEqual(self.new_data[norimage.PALETTE_OFFSET:norimage.PALETTE_OFFSET + len(region)], region)

    def test_palette_file_matches(self):
        pal = PALETTE.read_bytes()
        self.assertEqual(len(pal), 512)
        self.assertEqual(self.new_data[0x28000:0x28200], pal)


if __name__ == "__main__":
    unittest.main()

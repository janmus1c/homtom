import gzip
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "firmware"))
import ttimage  # noqa: E402

BASE = ROOT / "artifacts/backups/first-INTERNAL/ttsystem"


@unittest.skipUnless(BASE.exists(), "base ttsystem backup not present")
class TTImageTest(unittest.TestCase):
    def test_cpio_round_trip_keeps_every_entry(self):
        entries = ttimage.initramfs_entries(BASE.read_bytes())
        again = ttimage.read_cpio(ttimage.write_cpio(entries))
        self.assertEqual([(e["name"], e["mode"], e["data"], e["rmaj"], e["rmin"]) for e in entries],
                         [(e["name"], e["mode"], e["data"], e["rmaj"], e["rmin"]) for e in again])

    def test_build_replaces_usb_block_and_adds_modules(self):
        sys.path.insert(0, str(ROOT / "firmware"))
        import build
        with tempfile.TemporaryDirectory() as tmp:
            manifest = build.build(Path(tmp))
            image = (Path(tmp) / "ttsystem").read_bytes()
        names = {e["name"]: e for e in ttimage.initramfs_entries(image)}
        rc = names["/etc/rc"]["data"].decode()
        self.assertIn("sh /etc/homtom/network.sh", rc)
        self.assertNotIn("s3c24xx_udc.ko\ninsmod", rc)
        self.assertIn("/lib/modules/2.6.13/kernel/drivers/usb/gadget/s3c-udc-hs.ko", names)
        self.assertEqual(manifest["entry"], "0x31700000")
        for name in names:
            parent = name.rsplit("/", 1)[0]
            if parent:
                self.assertIn(parent, names, "no directory entry before " + name)
                self.assertLess(list(names).index(parent), list(names).index(name))
        secs, _ = ttimage.sections(image)
        self.assertEqual([a for a, _ in secs], [0x31000000, 0x31700000])


if __name__ == "__main__":
    unittest.main()

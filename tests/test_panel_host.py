import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANEL = ROOT / "panel"
SOURCES = ["tests/test_panel.c", "src/ui.c", "src/gfx.c", "src/proto.c", "src/layout.c", "src/http.c", "src/assets.c"]
CC = shutil.which("clang") or shutil.which("cc")


@unittest.skipUnless(CC, "no host C compiler")
class PanelHostTest(unittest.TestCase):
    """The panel's parser, layout, touch and renderer, compiled for the Mac as strict C89."""

    def test_c_unit_checks_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "test_panel"
            flags = ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                     "-Wno-overlength-strings", "-g"]
            if "clang" in CC:
                flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            build = subprocess.run([CC, *flags, "-o", str(exe), *SOURCES], cwd=PANEL, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(" 0 failures", run.stdout)


if __name__ == "__main__":
    unittest.main()

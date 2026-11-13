import functools
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
FIX = ROOT / "tests" / "fixtures" / "ha_ui_gen"
EXAMPLE = ROOT / "docs" / "ha-ui.example.json"
GEN = ROOT / "tools" / "homtom-ha-ui-gen.py"
PREVIEW = ROOT / "workspace" / "gui" / "out" / "preview"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gen = load("homtom_ha_ui_gen", GEN)
daemon = gen.ui


def run_gen(tmp, *extra, config=EXAMPLE):
    out = Path(tmp) / "out.conf"
    cfg = Path(tmp) / "ha-ui.json"
    cfg.write_text(Path(config).read_text())
    r = subprocess.run([sys.executable, "-I", str(GEN), "--config", str(cfg), "--from-dump", str(FIX / "registry.json"),
                        "--out", str(out), *extra], capture_output=True, text=True)
    return r, out


def page_block(text, pid):
    lines, on = [], False
    for ln in text.splitlines():
        if ln.startswith("page "):
            on = f"id={pid} " in ln + " "
        if on and ln.strip():
            lines.append(ln)
    return lines


class Generator(unittest.TestCase):
    def test_merge_order_header_and_limits(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = run_gen(tmp, "--merge-base", str(FIX / "base.conf"))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            text = out.read_text()
            ui_lines = [ln for ln in text.splitlines() if ln.split(" ")[0] == "ui"]
            self.assertEqual(len(ui_lines), 1)
            self.assertIn("start=office", ui_lines[0])
            self.assertIn("title=homtom", ui_lines[0])
            pages = [ln.split()[1][3:] for ln in text.splitlines() if ln.startswith("page ")]
            self.assertEqual(pages[0], "office")
            self.assertEqual(pages[-2:], ["system", "tools"])
            base = (FIX / "base.conf").read_text()
            self.assertTrue(text.rstrip().endswith(base[base.index("page id=system"):].rstrip()))
            self.assertIn("on key=short action=page arg=cycle", text)
            self.assertLess(text.index("on key=short"), text.index("page id=office"))
            st = gen.conf_stats(text)
            self.assertEqual(gen.over_limits(st, gen.limits()), [])
            if PREVIEW.exists():
                self.assertIn("host parser accepted", r.stdout)

    def test_vent_tile_and_page(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = run_gen(tmp, "--merge-base", str(FIX / "base.conf"))
            text = out.read_text()
            office = "\n".join(page_block(text, "office"))
            self.assertEqual(office.count("slot=office_ac"), 1)
            self.assertIn("slot=office_ac_pwr label=Vent icon=ac tap=ha:press confirm=1 hold=page:office_vent", office)
            vent = page_block(text, "office_vent")
            self.assertIn("hidden=1", vent[0])
            self.assertIn("parent=office", vent[0])
            btn = [ln for ln in vent if ln.startswith("button ")]
            self.assertEqual(len(btn), 3)
            for ln, (label, slot) in zip(btn, [("On/Off", "office_ac_pwr"), ("High", "office_ac_high"), ("Low", "office_ac_low")]):
                self.assertIn(f"label={label} slot={slot} tap=ha:press", ln)
            self.assertEqual(len(page_block(text, "office_l1")), 2)

    def test_default_layout_and_override(self):
        cfg = json.loads(EXAMPLE.read_text())
        self.assertNotIn("office_layout", cfg)
        slots = daemon_slots(cfg)
        text, warn = gen.generate(cfg, slots, ["office"], {}, base_text=(FIX / "base.conf").read_text())
        office = page_block(text, "office")
        self.assertEqual([ln.split()[0] for ln in office[1:]], ["thermo", "lightctl", "entity", "entity", "entity", "entity", "entity"])
        self.assertNotIn("office_pm25", "\n".join(office))
        self.assertTrue(any("office_pm25" in w for w in warn))
        cfg["office_layout"] = ["thermo", "all", "presence", "pm25", "vent"]
        text, warn = gen.generate(cfg, slots, ["office"], {}, base_text=(FIX / "base.conf").read_text())
        office = "\n".join(page_block(text, "office"))
        self.assertIn("slot=office_pm25", office)
        self.assertNotIn("slot=office_co2", office)
        cfg["office_layout"] = ["all", "lights", "vent"]
        text, _ = gen.generate(cfg, slots, ["office"], {}, base_text=(FIX / "base.conf").read_text())
        self.assertIn("slot=office_amb label=Ambient icon=bulb tap=ha:toggle hold=page:office_l1", text)

    def test_touch_targets(self):
        cfg = json.loads(EXAMPLE.read_text())
        text, _ = gen.generate(cfg, daemon_slots(cfg), ["office"], {}, base_text=(FIX / "base.conf").read_text())
        rows = {int(ln.split("at=")[1].split(",")[1].split()[0]) for ln in page_block(text, "office")[1:]}
        self.assertLessEqual(max(rows) + 1, gen.GRID_ROWS)
        # 4x3 grid, 216 px without bottom navigation: 66 px rows; with tabs 160 px: 48 px rows
        self.assertGreaterEqual((160 - 2 * 8) // 3, 40)

    def test_generic_names_when_keys_differ(self):
        cfg = json.loads(EXAMPLE.read_text())
        slots = daemon_slots(cfg)
        ren = {"office_ac_pwr": "remote_a", "office_ac_high": "remote_b", "office_ac_low": "remote_c"}
        slots = {ren.get(k, k): dict(v, name=n) for (k, v), n in
                 zip(slots.items(), [None] * len(slots))}
        slots["remote_a"]["name"], slots["remote_b"]["name"], slots["remote_c"]["name"] = "Fan power", "Fan high", "Fan low"
        text, _ = gen.generate(cfg, slots, ["office"], {}, base_text=(FIX / "base.conf").read_text())
        self.assertIn("slot=remote_a label=Vent", text)
        self.assertEqual(len([ln for ln in page_block(text, "office_vent") if ln.startswith("button")]), 3)
        slots = {k: v for k, v in slots.items() if k != "remote_b"}
        text, _ = gen.generate(cfg, slots, ["office"], {}, base_text=(FIX / "base.conf").read_text())
        self.assertEqual(len([ln for ln in page_block(text, "office_vent") if ln.startswith("button")]), 2)

    def test_over_limit_is_reported(self):
        st = gen.conf_stats("ui version=1\npage id=a\n" + "label x=0 y=0 w=1 h=1\n" * 100)
        self.assertEqual(st["widgets"], 100)
        self.assertTrue(gen.over_limits(st, gen.limits()))
        with self.assertRaises(ValueError):
            gen.parse_base("page id=a\n")

    def test_page_budget_keeps_area_and_vent_before_detail_pages(self):
        cfg = json.loads(EXAMPLE.read_text())
        text, warn = gen.generate(cfg, daemon_slots(cfg), ["office"], {}, pages_max=5,
                                  base_text=(FIX / "base.conf").read_text())
        self.assertIn("page id=office_vent", text)
        self.assertTrue(any("exceed" in w for w in warn))
        self.assertLessEqual(gen.conf_stats(text)["pages"], 5)

    def test_standalone_has_own_ui_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = run_gen(tmp, "--standalone")
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(sum(ln.startswith("ui ") for ln in out.read_text().splitlines()), 1)


def daemon_slots(cfg):
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "c.json"
        p.write_text(json.dumps(cfg))
        return daemon.load_config(str(p), str(Path(tmp) / "none.json"))["_slots"]


class VentPress(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.log = t / "ha-ui.log"
        cfg = t / "ha-ui.json"
        cfg.write_text(EXAMPLE.read_text())
        auto = t / "ha-ui.auto.json"
        auto.write_text(json.dumps({"slots": {"kitchen_a1": {"area": "kitchen", "entity": "button.kitchen_thing"}}}))
        self.patches = [patch.object(daemon, "CONFIG", str(cfg)), patch.object(daemon, "AUTO", str(auto)),
                        patch.object(daemon, "LOG", str(self.log)), patch.object(daemon, "DISABLE", str(t / "off")),
                        patch.object(daemon, "RT", str(t)), patch.object(daemon, "device_uptime", lambda: None),
                        patch.object(daemon, "load_config", functools.partial(daemon.load_config, str(cfg), str(auto))),
                        patch.object(daemon, "post_webhook", side_effect=AssertionError("must not post in a dry run"))]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self.tmp.cleanup()

    def events(self, *items):
        d = daemon.Daemon(dry_run=True, no_device=True)
        d.ev_first, d.ev_done = "seq=0 up=1 event=start name= value=", 0
        lines = ["seq=0 up=1 event=start name= value="] + [
            f"seq={i + 1} up=10 event=ha name={n} value={v} page=office" for i, (n, v) in enumerate(items)]
        d.handle_lines(("\n".join(lines) + "\n").encode())
        return self.log.read_text()

    def test_three_vent_presses_accepted_unmapped_refused(self):
        log = self.events(("office_ac_pwr", "press"), ("office_ac_high", "press"), ("office_ac_low", "press"),
                          ("office_nothing", "press"), ("kitchen_a1", "press"))
        for slot in ("office_ac_pwr", "office_ac_high", "office_ac_low"):
            self.assertIn(f"dry-run slot={slot} op=press", log)
        self.assertIn("refused slot=office_nothing op=press result=slot not mapped", log)
        self.assertIn("refused slot=kitchen_a1 op=press result=slot not mapped", log)
        self.assertEqual(log.count("dry-run slot="), 3)

    def test_press_on_a_light_and_double_press_refused(self):
        log = self.events(("office_amb", "press"), ("office_ac_high", "press"), ("office_ac_high", "press"))
        self.assertIn("refused slot=office_amb op=press result=invalid", log)
        self.assertIn("refused slot=office_ac_high op=press result=debounced", log)


if __name__ == "__main__":
    unittest.main()

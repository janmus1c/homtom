#!/usr/bin/env -S python3 -I
"""Generate the homtom ui.conf for Home Assistant areas (docs/HA_UI_BRIDGE.md, format docs/UI_CONFIG.md).

usage: homtom-ha-ui-gen.py [--from-dump FILE] [--merge-base PATH | --standalone] [--config FILE] [--out FILE] [--upload [--force]]
       homtom-ha-ui-gen.py --discover AREA [--from-dump FILE]     print proposed slot entries for one area
       homtom-ha-ui-gen.py --print-template [AREA ...]            print the registry template (render it in HA to make a dump)

Inputs: the user's map .runtime/ha-ui.json and the area/entity registry, read through the same path as the daemon
(POST /api/template with .runtime/ha_token) or from a JSON dump. Page order: the value of input_text.homtom_ui_areas
(comma-separated area ids) when it is readable, else "areas" in the config. One page per area, in that order, plus
hidden light detail pages (group=AREA_lights). Areas without slots in the config get display-only slots, written to
.runtime/ha-ui.auto.json; the daemon pushes their states but refuses their actions.
Merge (default): the output is the area pages first (top level, swipe order), then the pages of the base file
userland/ui.conf.default (--merge-base PATH) unchanged. The base `ui` line is the only header; start= becomes the first
area page. --standalone writes the old area-only file with its own `ui` line.
Output: .runtime/ui.conf.generated. --upload writes INTERNAL:/homtom/ui.conf (ui.conf.new, renamed in the same FTP
session) after the host parser (workspace/gui/out/preview) accepted the file; --force uploads without that check."""
import importlib.util, json, os, re, shlex, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RT = os.path.join(ROOT, ".runtime")
OUT = os.path.join(RT, "ui.conf.generated")
PREVIEW = os.path.join(ROOT, "workspace", "gui", "out", "preview")
DEV_CONF, DEV_CONF_TMP = "/mnt/internal/homtom/ui.conf", "/mnt/internal/homtom/ui.conf.new"
GRID_COLS, GRID_ROWS = 4, 3
BASE = os.path.join(ROOT, "userland", "ui.conf.default")
CFG_H = os.path.join(ROOT, "workspace", "gui", "cfg.h")
CFG_C = os.path.join(ROOT, "workspace", "gui", "cfg.c")
DEFAULT_LAYOUT = ["thermo", "all", "presence", "co2", "temp", "hum", "vent"]
ROLE_RE = [("presence", r"presen|occup|motion"), ("co2", r"co2|carbon"), ("pm25", r"pm"), ("hum", r"hum"),
           ("temp", r"temp")]
VENT_RE = [("vent_onoff", r"pwr|power|on.?off"), ("vent_high", r"high"), ("vent_low", r"low")]
ROLE_ICON = {"light": "bulb", "presence": "person", "co2": "co2", "temp": "thermometer", "hum": "drop", "pm25": "cloud",
             "vent": "ac"}
VENT_LABEL = {"vent_onoff": "On/Off", "vent_high": "High", "vent_low": "Low"}
NON_WIDGET = {"ui", "page", "var", "on"}


def limits():
    """Parser limits from workspace/gui/cfg.h (fallback: the documented values)."""
    out = {"CFG_FILE_MAX": 8192, "CFG_LINE_MAX": 200, "CFG_PAGES": 12, "CFG_NAV": 6, "CFG_WIDGETS": 64}
    try:
        with open(CFG_H) as f:
            for k, v in re.findall(r"^#define (CFG_\w+)\s+(\d+)", f.read(), re.M):
                if k in out:
                    out[k] = int(v)
    except OSError:
        pass
    return out


def icon_names():
    """Icon names the renderer's parser accepts (workspace/gui/cfg.c icon_name[])."""
    try:
        with open(CFG_C) as f:
            m = re.search(r"icon_name\[\]\s*=\s*\{(.*?)\}", f.read(), re.S)
        return set(re.findall(r'"(\w+)"', m.group(1)))
    except (OSError, AttributeError):
        return set(ROLE_ICON.values())
AREA_RE = re.compile(r"^[a-z0-9_]{1,30}$")
AUTO_SENSOR_CLASSES = ["carbon_dioxide", "temperature", "humidity", "pm25"]
AUTO_BINARY_CLASSES = ["occupancy", "presence", "motion"]

spec = importlib.util.spec_from_file_location("homtom_ha_ui", os.path.join(ROOT, "tools", "homtom-ha-ui.py"))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def registry_template(areas, helper):
    row = ("{%- set s = states[e] -%}{%- set ns = namespace(a={}) -%}{%- if s -%}"
           "{%- for k in keys if k in s.attributes -%}{%- set ns.a = dict(ns.a, **{k: s.attributes[k]}) -%}"
           "{%- endfor -%}{%- endif -%}{{ {'e': e, 's': s.state if s else 'unavailable', "
           "'lc': as_timestamp(s.last_changed) | round(1) if s else none, 'a': ns.a} | to_json }}")
    return ("{%- set keys = " + json.dumps(ui.ATTR_KEYS) + " -%}{%- set areas = " + json.dumps(areas) + " -%}"
            "{\"helper\": {{ (states('" + helper + "') if '" + helper + "' else '') | to_json }}, \"areas\": ["
            "{%- for ar in areas -%}{%- set ents = (area_entities(ar) + (area_devices(ar) | map('device_entities') "
            "| sum(start=[]))) | unique | select('match', '^(light|climate|binary_sensor|sensor|button)[.]') | sort -%}"
            "{\"id\": {{ ar | to_json }}, \"name\": {{ (area_name(ar) or ar) | to_json }}, \"entities\": ["
            "{%- for e in ents -%}" + row + "{{ ',' if not loop.last }}{%- endfor -%}]}{{ ',' if not loop.last }}"
            "{%- endfor -%}]}")


def read_registry(cfg, dump, extra_areas=()):
    helper = cfg.get("areas_helper") or ""
    if helper and not ui.ENTITY_RE.match(helper):
        raise ValueError("areas_helper must be an entity id")
    if dump:
        with open(dump) as f:
            d = json.load(f)
        return d.get("registry", d)
    if "HA_URL" not in os.environ:
        sys.exit("HA_URL must be set (or use --from-dump)")
    areas = [a["id"] for a in cfg.get("areas", [])] + list(extra_areas)
    first = json.loads(ui.ha_template(registry_template(areas, helper)))
    wanted = [a for a in (first.get("helper") or "").split(",") if AREA_RE.match(a)]
    missing = [a for a in wanted if a not in areas]
    if missing:
        first = json.loads(ui.ha_template(registry_template(areas + missing, helper)))
    return first


def area_order(cfg, reg):
    known = [a["id"] for a in cfg.get("areas", [])]
    wanted = [a for a in (reg.get("helper") or "").split(",") if AREA_RE.match(a)]
    return wanted or known


def auto_slots(area, reg_area, used):
    """Display-only slots for an area that has none in the config: lights, climate, presence and air sensors."""
    out, n = {}, 0
    for row in reg_area.get("entities", []):
        if not ui.ENTITY_RE.match(str(row.get("e", ""))) or row.get("s") == "unavailable":
            continue
        e, a = row["e"], row.get("a") or {}
        d = e.split(".")[0]
        dc = a.get("device_class")
        if not (d in ("light", "climate") or (d == "binary_sensor" and dc in AUTO_BINARY_CLASSES)
                or (d == "sensor" and dc in AUTO_SENSOR_CLASSES)):
            continue
        n += 1
        sid = f"{area[:10]}_a{n}"
        while sid in used:
            n += 1
            sid = f"{area[:10]}_a{n}"
        used.add(sid)
        out[sid] = {"area": area, "entity": e, "name": ui.ascii_text(a.get("friendly_name") or e, 24)}
    return out


def q(s):
    s = ui.ascii_text(s, 24).replace('"', "")
    return f'"{s}"' if (" " in s or not s) else s


class Grid:
    def __init__(self):
        self.used = set()

    def place(self, w):
        for r in range(GRID_ROWS):
            for c in range(GRID_COLS - w + 1):
                if all((c + i, r) not in self.used for i in range(w)):
                    self.used.update((c + i, r) for i in range(w))
                    return c, r
        return None


def role_of(sid, s):
    """Semantic role of a slot: explicit "role" in the map, else guessed from the slot id and name."""
    if s.get("role"):
        return s["role"]
    text = f"{sid} {s.get('name') or ''}".lower()
    kind = s["kind"]
    if kind == "climate":
        return "thermo"
    if kind == "light":
        return "all" if s.get("main") else "light"
    if kind == "button":
        return next((r for r, rx in VENT_RE if re.search(rx, text)), "button")
    return next((r for r, rx in ROLE_RE if re.search(rx, text)), "sensor")


def icon_for(role, slot, names):
    icon = slot.get("icon") or ROLE_ICON.get(role)
    return icon if icon in names else None


def widget_line(sid, slot, kind, c, r, w, detail=None, icon=None, tap=None, hold=None, label=None):
    at = f"at={c},{r}" + (f" span={w},1" if w > 1 else "")
    label = q(label or slot.get("name") or sid)
    if kind == "lightctl":
        ops = slot.get("ops") or set()
        caps = ""
        if "set_ct" in ops:
            caps += f" ct_min={int(slot.get('ct_min', 2000))} ct_max={int(slot.get('ct_max', 6500))}"
        caps += " color=1" if "set_hue_sat" in ops else " color=0"
        if "set_bri" not in ops:
            caps += " dim=0"
        return f"lightctl {at} slot={sid} label={label}{caps}" + (f" detail={detail}" if detail else "")
    if kind == "thermo":
        modes = ",".join(slot.get("modes") or ["off", "heat"])
        return (f"thermo {at} slot={sid} label={label} min={max(10, slot.get('min', 10))} "
                f"max={min(26, slot.get('max', 26))} step=0.5 modes={modes}")
    if kind == "button":
        return f"button {at} slot={sid} label={label} tap={tap}" + (" confirm=1" if slot.get("confirm") else "")
    line = f"entity {at} slot={sid} label={label}" + (f" icon={icon}" if icon else "")
    if tap:
        line += f" tap={tap}" + (" confirm=1" if slot.get("confirm") and tap == "ha:press" else "")
    elif slot["kind"] == "button" and slot.get("actions"):
        line += " tap=press" + (" confirm=1" if slot.get("confirm") else "")
    elif slot["kind"] == "light" and slot.get("actions"):
        line += " tap=toggle"
    if hold:
        line += f" hold={hold}"
    if slot.get("good_below"):
        line += f" good_below={slot['good_below']}"
    return line


def vent_page(area, pid, vent, names):
    """Hidden page with three large buttons, one per IR slot; returns lines."""
    lines = [f"page id={pid} title=Vent hidden=1 parent={area[:15]}"]
    present = [r for r in ("vent_onoff", "vent_high", "vent_low") if r in vent]
    wd = (464 - 8 * (len(present) - 1)) // len(present)
    for i, r in enumerate(present):
        sid, s = vent[r]
        conf = " confirm=1" if s.get("confirm") else ""
        lines.append(f"button x={8 + i * (wd + 8)} y=72 w={wd} h=120 label={q(VENT_LABEL[r])} slot={sid} tap=ha:press{conf}")
    return lines


def layout_items(area, cfg, mine, others, detail_ids, vent, names, warn):
    """Ordered (slot id, slot, widget kind, span, extra keys) for one area page."""
    by_role, used, items = {}, set(), []
    for sid, s in mine:
        by_role.setdefault(role_of(sid, s), []).append((sid, s))
    main = next(iter(by_role.get("all", [])), None) or (next(((a, b) for a, b in mine if b["kind"] == "light" and b.get("actions")), None))
    layout = cfg.get(f"{area}_layout") or DEFAULT_LAYOUT
    by_id = dict(mine)

    def add(sid, s, kind, span, **kw):
        if sid not in used:
            used.add(sid)
            items.append((sid, s, kind, span, kw))

    def entity(sid, s, role):
        add(sid, s, "entity", 1, icon=icon_for(role, s, names))

    for tok in layout:
        if tok == "thermo":
            for sid, s in by_role.get("thermo", []):
                if s.get("actions"):
                    add(sid, s, "thermo", 2)
        elif tok == "all" and main:
            add(main[0], main[1], "lightctl", 2, detail=detail_ids[0] if others else None)
        elif tok == "lights":
            for (sid, s), dp in zip(others, detail_ids):
                add(sid, s, "entity", 1, icon=icon_for("light", s, names), tap="ha:toggle", hold=f"page:{dp}")
        elif tok == "vent" and vent:
            first = next(r for r in ("vent_onoff", "vent_high", "vent_low") if r in vent)
            sid, s = vent[first]
            add(sid, s, "entity", 2, icon=icon_for("vent", s, names), label="Vent",
                tap="ha:press" if first == "vent_onoff" else None, hold=f"page:{area[:10]}_vent")
        elif tok == "rest":
            for sid, s in mine:
                if sid not in used and role_of(sid, s) not in ("vent_onoff", "vent_high", "vent_low"):
                    r = role_of(sid, s)
                    add(sid, s, "entity", 1, icon=icon_for(r, s, names))
        elif tok in by_id:
            entity(tok, by_id[tok], role_of(tok, by_id[tok]))
        elif tok in by_role and tok not in ("thermo", "all"):
            for sid, s in by_role[tok]:
                entity(sid, s, tok)
        elif tok not in ("all", "vent"):
            warn.append(f"area {area}: layout entry {tok!r} matches no slot")
    left = [sid for sid, s in mine if sid not in used and role_of(sid, s) not in ("vent_onoff", "vent_high", "vent_low")
            and not (s["kind"] == "light" and s.get("actions"))]
    if left:
        warn.append(f"area {area}: not on the page (add to {area}_layout to show): {', '.join(left)}")
    return items


def legacy_items(mine, main, others, detail_ids, names):
    """Areas without mapped slots (display-only): everything in reading order."""
    items = []
    if main:
        items.append((main[0], main[1], "lightctl", 2, {"detail": detail_ids[0] if others else None}))
    for sid, s in mine:
        if main and sid == main[0]:
            continue
        if s["kind"] == "climate" and s.get("actions"):
            items.append((sid, s, "thermo", 2, {}))
    for kinds in (("light", "climate"), ("binary", "sensor"), ("button",)):
        for sid, s in mine:
            if s["kind"] in kinds and (kinds != ("light", "climate") or not s.get("actions")) and not (main and sid == main[0]):
                items.append((sid, s, "entity", 1, {"icon": icon_for(role_of(sid, s), s, names)}))
    return items


def generate_pages(cfg, slots, order, reg_names, pages_max, names=None):
    """Return (list of page line-lists, warnings). Area pages first, each followed by its hidden pages."""
    names = icon_names() if names is None else names
    warn, pages = [], []
    titles = {a["id"]: a.get("title") for a in cfg.get("areas", [])}
    for area in order:
        mine = [(sid, s) for sid, s in slots.items() if s.get("area") == area]
        if not mine:
            warn.append(f"area {area}: no slots (unknown area or nothing usable), page skipped")
            continue
        pid = area[:15]
        lights = [(sid, s) for sid, s in mine if s["kind"] == "light" and s.get("actions")]
        main = next((x for x in lights if x[1].get("main")), lights[0] if lights else None)
        others = [x for x in lights if x is not main]
        detail_ids = [f"{area[:9]}_l{i + 1}" for i in range(len(others))]
        vent = {}
        for sid, s in mine:
            r = role_of(sid, s)
            if s["kind"] == "button" and s.get("actions") and r in VENT_LABEL and r not in vent:
                vent[r] = (sid, s)
        configured = any(s.get("actions") for _, s in mine)
        if configured:
            items = layout_items(area, cfg, mine, others, detail_ids, vent, names, warn)
        else:
            items = legacy_items(mine, main, others, detail_ids, names)
        g, body, overflow = Grid(), [], []
        for sid, s, kind, span, kw in items:
            pos = g.place(span)
            if pos is None:
                overflow.append(sid)
                continue
            body.append(widget_line(sid, s, kind, pos[0], pos[1], span, **kw))
        if overflow:
            warn.append(f"area {area}: no room on the page for {', '.join(overflow)}")
        area_title = q(titles.get(area) or reg_names.get(area) or area)
        pages.append([f"page id={pid} title={area_title} grid={GRID_COLS}x{GRID_ROWS}"] + body)
        if vent and any(i[0] == vent[next(r for r in ("vent_onoff", "vent_high", "vent_low") if r in vent)][0] for i in items):
            pages.append(vent_page(area, f"{area[:10]}_vent", vent, names))
        for (sid, s), dpid in zip(others, detail_ids):
            pages.append([f"page id={dpid} title={q(s.get('name') or sid)} grid=1x1 hidden=1 group={area[:9]}_lights "
                          f"parent={pid}", widget_line(sid, s, "lightctl", 0, 0, 1)])
    if not pages:
        raise ValueError("no area produced a page")
    if len(pages) > pages_max:
        keep = [p for p in pages if " hidden=1" not in p[0]]
        extra = [p for p in pages if " hidden=1" in p[0]]
        # vent pages outrank light detail pages
        extra.sort(key=lambda p: " title=Vent " not in p[0])
        extra = extra[:max(0, pages_max - len(keep))]
        warn.append(f"{len(pages)} pages exceed the room for generated pages ({pages_max}): kept "
                    f"{min(len(keep) + len(extra), pages_max)} (area pages first, then vent, then light detail pages)")
        names_ = {id(p) for p in keep + extra}
        pages = [p for p in pages if id(p) in names_][:pages_max]
    return pages, warn


def parse_base(text):
    """Split a ui.conf into (ui options in order, preamble lines, page lines from the first page on)."""
    opts, pre, rest, in_pages = None, [], [], False
    for ln in text.splitlines():
        w = ln.split(None, 1)
        if not in_pages and w and w[0] == "page":
            in_pages = True
        if in_pages:
            rest.append(ln)
        elif w and w[0] == "ui":
            if opts is not None:
                raise ValueError("base file has a second ui line")
            opts = {}
            for tok in shlex.split(ln)[1:]:
                k, _, v = tok.partition("=")
                opts[k] = v
        else:
            pre.append(ln)
    if opts is None:
        raise ValueError("base file has no ui line")
    return opts, pre, rest


def ui_line(opts):
    return "ui " + " ".join(f'{k}="{v}"' if (" " in v or not v) else f"{k}={v}" for k, v in opts.items())


def conf_stats(text):
    pages = hidden = widgets = 0
    for ln in text.splitlines():
        w = ln.split()
        if not w or w[0].startswith("#"):
            continue
        if w[0] == "page":
            pages += 1
            hidden += "hidden=1" in w
        elif w[0] not in NON_WIDGET:
            widgets += 1
    return {"bytes": len(text.encode()), "pages": pages, "visible": pages - hidden, "hidden": hidden, "widgets": widgets,
            "longest_line": max((len(x) for x in text.splitlines()), default=0)}


def over_limits(st, lim):
    bad = []
    for k, lk, label in (("bytes", "CFG_FILE_MAX", "bytes"), ("pages", "CFG_PAGES", "pages"), ("visible", "CFG_NAV", "visible pages"),
                         ("widgets", "CFG_WIDGETS", "widgets"), ("longest_line", "CFG_LINE_MAX", "characters in the longest line")):
        if st[k] > lim[lk]:
            bad.append(f"{st[k]} {label} exceed the parser limit {lim[lk]}")
    return bad


def generate(cfg, slots, order, reg_names, pages_max=None, base_text=None, lim=None, names=None):
    """Return (ui.conf text, warnings). With base_text: area pages first, then the base pages unchanged."""
    lim = lim or limits()
    pages_max = lim["CFG_PAGES"] if pages_max is None else pages_max
    base_opts = pre = rest = None
    room = pages_max
    if base_text is not None:
        base_opts, pre, rest = parse_base(base_text)
        bst = conf_stats("\n".join(rest))
        room = pages_max - bst["pages"]
        if room < 1:
            raise ValueError(f"the base file already has {bst['pages']} pages; no room for area pages (limit {pages_max})")
    pages, warn = generate_pages(cfg, slots, order, reg_names, room, names)
    if base_text is not None:
        nav_room = lim["CFG_NAV"] - bst["visible"]
        vis = [p for p in pages if " hidden=1" not in p[0]]
        if len(vis) > nav_room:
            drop = {id(p) for p in vis[max(0, nav_room):]}
            warn.append(f"{len(vis)} area pages exceed the {lim['CFG_NAV']} visible pages the parser allows with the base: "
                        f"dropped the last {len(vis) - max(0, nav_room)}")
            pages = [p for p in pages if id(p) not in drop]
    start = pages[0][0].split()[1].split("=")[1] if pages else None
    head = ["# generated by tools/homtom-ha-ui-gen.py; edit .runtime/ha-ui.json or input_text.homtom_ui_areas, not this file",
            "# the area pages come first (swipe order); the pages after them are the base file, unchanged",
            "# widgets entity, lightctl and thermo and the keys slot/detail/tap/hold/hidden/group/parent follow the HA UI contract",
            "# (docs/HA_UI_BRIDGE.md); a parser without them rejects this file and keeps its last good config"]
    if base_text is None:
        title = ui.ascii_text(cfg.get("ui_title", "homtom"), 15).replace(" ", "_") or "homtom"
        head.append(f"ui version=1 title={title} start={start} nav={cfg.get('nav', 'both')}")
        pre = []
        rest = []
    else:
        opts = dict(base_opts)
        opts["start"] = start
        if cfg.get("nav"):
            opts["nav"] = cfg["nav"]
        if cfg.get("ui_title"):
            opts["title"] = ui.ascii_text(cfg["ui_title"], 15).replace(" ", "_")
        head.append(ui_line(opts))
    while pre and not pre[-1].strip():
        pre.pop()
    lines = head + pre
    for p in pages:
        lines.append("")
        lines += p
    if rest:
        lines.append("")
        lines += rest
    text = "\n".join(lines).rstrip("\n") + "\n"
    return text, warn


def check_with_parser(path):
    """Run the host preview parser on the file; returns (ok, message). ok=None when the parser is not built."""
    if not os.path.exists(PREVIEW):
        return None, "host parser not built (workspace/gui/build.sh preview)"
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run([PREVIEW, "-c", path, "-o", os.path.join(d, "p.png")], capture_output=True, text=True, timeout=30)
    msg = (r.stdout + r.stderr)
    m = re.search(r"line \d+:[^\n]*", msg)
    if r.returncode != 0 or m:
        return False, m.group(0) if m else f"preview exit {r.returncode}"
    return True, "host parser accepted the file"


def pop_opt(argv, name):
    if name in argv:
        i = argv.index(name)
        if i + 1 >= len(argv):
            sys.exit(f"{name} needs a value")
        v = argv[i + 1]
        del argv[i:i + 2]
        return v
    return None


def main(argv):
    global OUT
    argv = list(argv)
    dump = pop_opt(argv, "--from-dump")
    config_path = pop_opt(argv, "--config")
    OUT = pop_opt(argv, "--out") or OUT
    merge_base = pop_opt(argv, "--merge-base")
    standalone = "--standalone" in argv
    if standalone and merge_base:
        sys.exit("--standalone and --merge-base exclude each other")
    if config_path:
        auto_path = os.path.splitext(config_path)[0] + ".auto.json"
        cfg = ui.load_config(config_path, auto_path)
    else:
        auto_path = ui.AUTO
        cfg = ui.load_config()
    if argv[:1] == ["--print-template"]:
        areas = [a for a in argv[1:] if AREA_RE.match(a)] or [a["id"] for a in cfg.get("areas", [])]
        print(registry_template(areas, cfg.get("areas_helper") or ""))
        return 0
    if argv[:1] == ["--discover"]:
        area = argv[1] if len(argv) > 1 and AREA_RE.match(argv[1]) else sys.exit("--discover AREA")
        reg = read_registry(cfg, dump, [area])
        ra = next((a for a in reg.get("areas", []) if a["id"] == area), None)
        print(json.dumps(auto_slots(area, ra or {}, set(cfg["_slots"])), indent=2))
        return 0
    reg = read_registry(cfg, dump)
    order = area_order(cfg, reg)
    configured = {sid: s for sid, s in cfg["_slots"].items() if s.get("actions")}
    used, auto = set(configured), {}
    reg_by_id = {a["id"]: a for a in reg.get("areas", [])}
    for area in order:
        if not any(s.get("area") == area for s in configured.values()) and area in reg_by_id:
            auto.update(auto_slots(area, reg_by_id[area], used))
    tmp = auto_path + ".new"
    with open(tmp, "w") as f:
        json.dump({"slots": auto}, f, indent=1)
    os.replace(tmp, auto_path)
    slots = dict(configured)
    for sid, s in auto.items():
        slots[sid] = dict(s, kind=ui.kind_of(s["entity"]), actions=False, ops=set())
    lim = limits()
    base_text, base_path = None, None
    if not standalone:
        base_path = merge_base or BASE
        with open(base_path) as f:
            base_text = f.read()
    pages_max = int(cfg.get("pages_max", lim["CFG_PAGES"]))
    text, warn = generate(cfg, slots, order, {a["id"]: a.get("name") for a in reg.get("areas", [])},
                          min(pages_max, lim["CFG_PAGES"]), base_text, lim)
    with open(OUT + ".new", "w") as f:
        f.write(text)
    os.replace(OUT + ".new", OUT)
    for w in warn:
        print("warning:", w)
    st = conf_stats(text)
    bad = over_limits(st, lim)
    ok, msg = check_with_parser(OUT)
    shown = os.path.relpath(OUT, ROOT) if OUT.startswith(ROOT) else OUT
    print(f"wrote {shown}: {st['bytes']}/{lim['CFG_FILE_MAX']} bytes, {st['pages']}/{lim['CFG_PAGES']} pages "
          f"({st['visible']}/{lim['CFG_NAV']} visible, {st['hidden']} hidden), {st['widgets']}/{lim['CFG_WIDGETS']} widgets; "
          f"areas {','.join(order)}, {len(auto)} display-only slots; "
          + (f"merged with {os.path.relpath(base_path, ROOT) if base_path.startswith(ROOT) else base_path}; " if base_path else "standalone; ")
          + msg)
    for b in bad:
        print("error:", b)
    if "--upload" in argv:
        if bad:
            print("not uploaded: the file exceeds the parser limits")
            return 3
        if ok is not True and "--force" not in argv:
            print("not uploaded: the host parser did not accept the file (use --force to upload anyway)")
            return 2
        how = ui.ftp_put_atomic(OUT, DEV_CONF_TMP, DEV_CONF)
        ui.log(f"ui.conf uploaded ({len(text)} bytes, rename via {how})")
        print("uploaded to INTERNAL:/homtom/ui.conf")
    return 3 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

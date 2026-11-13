#!/usr/bin/env -S python3 -I
"""Home Assistant <-> homtom touch UI daemon (docs/HA_UI_BRIDGE.md).

usage: homtom-ha-ui.py [--dry-run] [--once] [--no-device]     run the loop (states every ~2 s, events every ~1 s)
       homtom-ha-ui.py --print-states                        read HA once and print the /tmp/ha.states text
       homtom-ha-ui.py --action SLOT OP[:ARG] [--dry-run]    run one action through the same checks as a UI event
       homtom-ha-ui.py --print-template                      print the states template (payload for the push path)

States: the mapped entities are read from Home Assistant and pushed to the device as /tmp/ha.states (key=value,
<= 4 KB, <= 64 keys, lines <= 80 characters), uploaded to /tmp/ha.states.new over FTP and renamed in the same session.
Actions: /tmp/ui.events is fetched over FTP; new `event=ha` lines become webhook posts to automation.homtom_ui_actions,
which runs only an allow-list of actions for the mapped entities.

Slot map: .runtime/ha-ui.json (owner data, not in git; example docs/ha-ui.example.json). Display-only slots written by
homtom-ha-ui-gen.py: .runtime/ha-ui.auto.json. Actions are refused for any slot that is not in ha-ui.json.
Read path ("read" in the config): "rest" (long-lived token in .runtime/ha_token, one POST /api/template per cycle),
"push" (Home Assistant posts the same JSON to this daemon; no HA token on the Mac) or "file" (a JSON dump, development).
Secrets: HA_URL from the environment; webhook id in .runtime/ha_ui_webhook; token in .runtime/ha_token; push secret in
.runtime/ha_ui_push_secret. None of them is logged. Kill switch: .runtime/ha-ui.disable refuses all actions.
Log: .runtime/ha-ui.log."""
import calendar, http.server, json, os, re, subprocess, sys, tempfile, threading, time, unicodedata, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RT = os.path.join(ROOT, ".runtime")
CONFIG = os.path.join(RT, "ha-ui.json")
AUTO = os.path.join(RT, "ha-ui.auto.json")
LOG = os.path.join(RT, "ha-ui.log")
DISABLE = os.path.join(RT, "ha-ui.disable")
FTP = os.environ.get("HOMTOM_FTP", "ftp://192.168.1.10")
DEV_STATUS = os.environ.get("HOMTOM_URL", "http://192.168.1.10:8080/")
SH = os.path.join(ROOT, "tools", "homtom-sh")
UA = "homtom-ui/1"

STATES_PATH, STATES_TMP = "/tmp/ha.states", "/tmp/ha.states.new"
EVENTS_PATH, EVENTS_TAKEN = "/tmp/ui.events", "/tmp/ui.events.taken"
MAX_BYTES, MAX_KEYS, MAX_LINE = 4096, 64, 80
SLOT_RE = re.compile(r"^[a-z0-9_]{1,15}$")
ENTITY_RE = re.compile(r"^[a-z_]+\.[a-z0-9_]+$")
EVENT_RE = re.compile(r"^seq=(\d+) up=(\d+) event=(\S+) name=(\S*) value=(\S*)(?: page=(\S*))?$")
OPS = {"light": {"toggle", "set_bri", "set_ct", "set_hue_sat"}, "button": {"press"}, "climate": {"set_target", "mode"}}
ATTR_KEYS = ["friendly_name", "unit_of_measurement", "device_class", "brightness", "color_mode", "color_temp_kelvin",
             "hs_color", "supported_color_modes", "min_color_temp_kelvin", "max_color_temp_kelvin", "current_temperature",
             "temperature", "hvac_modes", "hvac_action", "min_temp", "max_temp", "target_temp_step", "preset_mode"]
DEBOUNCE_S = 0.4
STALE_S = 15
RATE_PER_S, RATE_BURST, RATE_PER_MIN = 3.0, 6, 60
EVENTS_ROTATE_BYTES = 12 * 1024
PUSH_EVERY_S = 20


def secret_values():
    out = [os.environ.get("HA_URL", "")]
    for name in ("ha_ui_webhook", "ha_token", "ha_ui_push_secret"):
        try:
            out.append(open(os.path.join(RT, name)).read().strip())
        except OSError:
            pass
    return [s for s in out if len(s) >= 6]


def scrub(text):
    text = str(text)
    for s in secret_values():
        text = text.replace(s, "<redacted>")
    return text


def log(*parts):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + scrub(" ".join(str(p) for p in parts))
    print(line, flush=True)
    try:
        os.makedirs(RT, exist_ok=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")
    except OSError:
        pass


def read_secret(name):
    with open(os.path.join(RT, name)) as f:
        v = f.read().strip()
    if not v:
        raise ValueError(f".runtime/{name} is empty")
    return v


def ascii_text(v, limit=40):
    s = str(v).replace("µ", "u").replace("μ", "u").replace("°", "")
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    s = "".join(c for c in s if 32 <= ord(c) < 127).strip()
    return s[:limit]


# ---------------- configuration ----------------
def kind_of(entity):
    d = entity.split(".", 1)[0]
    return {"light": "light", "button": "button", "input_button": "button", "climate": "climate",
            "binary_sensor": "binary", "sensor": "sensor"}.get(d, "sensor")


def load_config(path=CONFIG, auto=AUTO):
    with open(path) as f:
        cfg = json.load(f)
    slots = {}
    for sid, s in (cfg.get("slots") or {}).items():
        if not SLOT_RE.match(sid) or not ENTITY_RE.match(s.get("entity", "")):
            raise ValueError(f"bad slot {sid!r}: id must be [a-z0-9_]{{1,15}}, entity a plain entity id")
        s = dict(s, kind=s.get("kind") or kind_of(s["entity"]), actions=True)
        allowed = OPS.get(s["kind"], set())
        s["ops"] = set(s.get("ops") or allowed) & allowed
        slots[sid] = s
    try:
        with open(auto) as f:
            for sid, s in (json.load(f).get("slots") or {}).items():
                if sid not in slots and SLOT_RE.match(sid) and ENTITY_RE.match(s.get("entity", "")):
                    slots[sid] = dict(s, kind=s.get("kind") or kind_of(s["entity"]), actions=False, ops=set())
    except (OSError, ValueError):
        pass
    cfg["_slots"] = slots
    return cfg


# ---------------- Home Assistant read paths ----------------
def states_template(entities):
    ents = json.dumps(sorted(set(entities)))
    keys = json.dumps(ATTR_KEYS)
    return ("{%- set ents = " + ents + " -%}{%- set keys = " + keys + " -%}[{%- for e in ents -%}"
            "{%- set s = states[e] -%}{%- set ns = namespace(a={}) -%}{%- if s -%}"
            "{%- for k in keys if k in s.attributes -%}{%- set ns.a = dict(ns.a, **{k: s.attributes[k]}) -%}"
            "{%- endfor -%}{%- endif -%}{{ {'e': e, 's': s.state if s else 'unavailable', "
            "'lc': as_timestamp(s.last_changed) | round(1) if s else none, 'a': ns.a} | to_json }}"
            "{{ ',' if not loop.last }}{%- endfor -%}]")


def ha_template(template):
    """POST /api/template with the long-lived token from .runtime/ha_token; returns the rendered text."""
    req = urllib.request.Request(os.environ["HA_URL"].rstrip("/") + "/api/template",
                                 json.dumps({"template": template}).encode(),
                                 {"Authorization": "Bearer " + read_secret("ha_token"),
                                  "Content-Type": "application/json", "User-Agent": UA})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode()


def parse_states(doc):
    rows = json.loads(doc) if isinstance(doc, str) else doc
    out = {}
    for r in rows:
        if isinstance(r, dict) and ENTITY_RE.match(str(r.get("e", ""))):
            out[r["e"]] = {"s": str(r.get("s")), "lc": r.get("lc"), "a": r.get("a") or {}}
    return out


class PushStore:
    """Read path "push": Home Assistant POSTs the states JSON to http://LISTEN/<secret>; no HA token on the Mac."""

    def __init__(self, listen):
        self.data, self.at, self.lock = {}, 0.0, threading.Lock()
        secret = read_secret("ha_ui_push_secret")
        host, port = listen.rsplit(":", 1)
        store = self

        class H(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                n = int(self.headers.get("Content-Length") or 0)
                if self.path != "/" + secret or not 0 < n <= 64 * 1024:
                    self.send_response(404)
                    self.end_headers()
                    return
                try:
                    data = parse_states(self.rfile.read(n).decode())
                except ValueError:
                    self.send_response(400)
                    self.end_headers()
                    return
                with store.lock:
                    store.data.update(data)
                    store.at = time.time()
                self.send_response(204)
                self.end_headers()

            def log_message(self, *a):
                pass

        self.srv = http.server.ThreadingHTTPServer((host, int(port)), H)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        log("push listener on", f"{host}:{port}")

    def read(self):
        with self.lock:
            if time.time() - self.at > 60:
                return {}
            return dict(self.data)


def make_reader(cfg):
    mode = cfg.get("read", "rest")
    if mode == "rest":
        return lambda ents: parse_states(ha_template(states_template(ents)))
    if mode == "file":
        path = os.path.join(ROOT, cfg.get("dump", ".runtime/ha-ui.dump.json"))

        def from_file(ents):
            with open(path) as f:
                d = json.load(f)
            return parse_states(d.get("states", d) if isinstance(d, dict) else d)
        return from_file
    if mode == "push":
        store = PushStore(cfg.get("push_listen", "0.0.0.0:8766"))
        return lambda ents: store.read()
    raise ValueError(f"unknown read mode {mode!r}")


# ---------------- /tmp/ha.states ----------------
def fnum(v, nd=1):
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    if abs(x) >= 100 or nd == 0:
        return str(int(round(x)))
    return f"{x:.{nd}f}"


def iso_age(s, now):
    m = re.match(r"^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)", s or "")
    if not m:
        return None
    t = calendar.timegm(tuple(int(x) for x in m.groups()) + (0, 0, 0))
    off = re.search(r"([+-])(\d\d):(\d\d)$", s)
    if off:
        t -= (1 if off.group(1) == "+" else -1) * (int(off.group(2)) * 3600 + int(off.group(3)) * 60)
    return max(0, int(now - t))


def slot_keys(sid, slot, st, now):
    """[(tier, key, value)] for one slot: tier 0 is always sent, 1 and 2 only while the size limits allow."""
    st = st or {"s": "unavailable", "lc": None, "a": {}}
    s, a, kind = st["s"], st["a"], slot["kind"]
    p = f"ha.{sid}."
    out = [(1, p + "name", ascii_text(slot.get("name") or a.get("friendly_name") or sid, 24))]
    if s in ("unavailable", "unknown", "None"):
        out.append((0, p + "state", "unavailable"))
        return out
    if kind == "light":
        out.append((0, p + "state", "on" if s == "on" else "off"))
        if s == "on" and a.get("brightness") is not None:
            out.append((0, p + "bri", str(max(1, round(float(a["brightness"]) * 100 / 255)))))
        if s == "on" and a.get("color_mode") == "color_temp" and a.get("color_temp_kelvin"):
            out.append((1, p + "ct", str(int(a["color_temp_kelvin"]))))
        hs = a.get("hs_color")
        if s == "on" and a.get("color_mode") in ("hs", "xy", "rgb", "rgbw", "rgbww") and isinstance(hs, list) and len(hs) == 2:
            out.append((1, p + "hue", str(int(round(float(hs[0]))) % 360)))
            out.append((1, p + "sat", str(int(round(float(hs[1]))))))
    elif kind == "binary":
        out.append((0, p + "state", "on" if s == "on" else "off"))
    elif kind == "climate":
        out.append((0, p + "state", ascii_text(s, 16)))
        out.append((0, p + "mode", ascii_text(s, 16)))
        cur, tgt = fnum(a.get("current_temperature")), fnum(a.get("temperature"))
        if cur is not None:
            out.append((0, p + "val", cur))
            out.append((1, p + "unit", "C"))
        if tgt is not None:
            out.append((0, p + "target", tgt))
    elif kind == "button":
        out.append((0, p + "state", "ready"))
        age = iso_age(s, now)
        if age is not None:
            out.append((2, p + "age", str(age)))
        return out
    else:
        v = fnum(s, int(slot.get("decimals", 1)))
        if v is not None:
            out.append((0, p + "val", v))
        else:
            out.append((0, p + "state", ascii_text(s, 24)))
        unit = ascii_text(a.get("unit_of_measurement") or "", 8)
        if unit:
            out.append((1, p + "unit", unit))
    if st.get("lc"):
        out.append((2, p + "age", str(max(0, int(now - float(st["lc"]))))))
    return out


def build_states(cfg, states, now=None):
    now = time.time() if now is None else now
    order, rows = [], {}
    for sid, slot in cfg["_slots"].items():
        for tier, k, v in slot_keys(sid, slot, states.get(slot["entity"]), now):
            line = f"{k}={v}"[:MAX_LINE]
            rows[k] = (tier, line)
            order.append(k)
    chosen, size, dropped = set(), 0, 0
    for tier in (0, 1, 2):
        for k in order:
            t, line = rows[k]
            if t != tier:
                continue
            if len(chosen) >= MAX_KEYS or size + len(line) + 1 > MAX_BYTES:
                dropped += 1
                continue
            chosen.add(k)
            size += len(line) + 1
    text = "".join(rows[k][1] + "\n" for k in order if k in chosen)
    return text, dropped


# ---------------- device transfers ----------------
def curl(args, timeout):
    return subprocess.run(["curl", "-s", "--max-time", str(timeout)] + args, capture_output=True, timeout=timeout + 5)


def ftp_put_atomic(local, tmp, final):
    """Upload to tmp and rename to final in the same FTP session (RNFR/RNTO); fall back to one mv over homtom-sh."""
    r = curl(["-T", local, FTP + tmp, "-Q", "-RNFR " + tmp, "-Q", "-RNTO " + final], 10)
    if r.returncode == 0:
        return "ftp"
    r2 = curl(["-T", local, FTP + tmp], 10)
    if r2.returncode != 0:
        raise OSError(f"upload failed (curl {r.returncode}/{r2.returncode})")
    subprocess.run([SH, f"mv -f {tmp} {final}", "1"], capture_output=True, timeout=40)
    return "sh"


def ftp_get(path):
    """Bytes of a device file, or None when it does not exist."""
    r = curl([FTP + path], 6)
    if r.returncode == 0:
        return r.stdout
    if r.returncode in (78, 9, 19):
        return None
    raise OSError(f"ftp get {path}: curl {r.returncode}")


def ftp_rotate_events():
    r = curl([FTP + "/", "-Q", "RNFR " + EVENTS_PATH, "-Q", "RNTO " + EVENTS_TAKEN, "-o", os.devnull], 6)
    if r.returncode != 0:
        raise OSError(f"rotate failed: curl {r.returncode}")
    data = ftp_get(EVENTS_TAKEN) or b""
    curl([FTP + "/", "-Q", "DELE " + EVENTS_TAKEN, "-o", os.devnull], 6)
    return data


def device_uptime():
    try:
        return int(json.load(urllib.request.urlopen(DEV_STATUS, timeout=3)).get("uptime_s"))
    except Exception:
        return None


# ---------------- actions ----------------
def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def to_payload(slot, op, arg):
    """Validate and clamp one UI op for a mapped slot; returns the webhook payload or raises ValueError."""
    if op not in slot["ops"]:
        raise ValueError(f"op {op} not allowed for this slot")
    e = slot["entity"]
    if op in ("toggle", "press"):
        return {"kind": "action", "op": op, "entity_id": e}
    if op == "set_bri":
        n = int(float(arg))
        if n <= 0:
            return {"kind": "action", "op": "turn_off", "entity_id": e}
        return {"kind": "action", "op": "set_bri", "entity_id": e, "value": clamp(n, 1, 100)}
    if op == "set_ct":
        lo, hi = int(slot.get("ct_min", 2000)), int(slot.get("ct_max", 6500))
        return {"kind": "action", "op": "set_ct", "entity_id": e, "value": clamp(int(float(arg)), lo, hi)}
    if op == "set_hue_sat":
        h, s = arg.split(",")
        return {"kind": "action", "op": "set_hue_sat", "entity_id": e,
                "value": f"{clamp(int(float(h)), 0, 359)},{clamp(int(float(s)), 0, 100)}"}
    if op == "set_target":
        lo, hi = float(slot.get("min", 10)), float(slot.get("max", 26))
        lo, hi = max(lo, 10.0), min(hi, 26.0)
        t = round(clamp(float(arg), lo, hi) * 2) / 2
        return {"kind": "action", "op": "set_target", "entity_id": e, "value": f"{t:.1f}"}
    if op == "mode":
        modes = slot.get("modes") or ["off", "heat"]
        if arg not in modes:
            raise ValueError(f"mode {arg!r} not allowed")
        return {"kind": "action", "op": "mode", "entity_id": e, "value": arg}
    raise ValueError(f"unknown op {op}")


def post_webhook(payload):
    req = urllib.request.Request(os.environ["HA_URL"].rstrip("/") + "/api/webhook/" + read_secret("ha_ui_webhook"),
                                 json.dumps(payload).encode(), {"Content-Type": "application/json", "User-Agent": UA})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.status


class Limiter:
    def __init__(self):
        self.tokens, self.t, self.minute = float(RATE_BURST), time.monotonic(), []

    def allow(self):
        now = time.monotonic()
        self.tokens = min(RATE_BURST, self.tokens + (now - self.t) * RATE_PER_S)
        self.t = now
        self.minute = [x for x in self.minute if now - x < 60]
        if self.tokens < 1 or len(self.minute) >= RATE_PER_MIN:
            return False
        self.tokens -= 1
        self.minute.append(now)
        return True


class Daemon:
    def __init__(self, dry_run=False, no_device=False):
        self.dry_run, self.no_device = dry_run, no_device
        self.cfg_mtime, self.cfg, self.reader = None, None, None
        self.last_push_text, self.last_push_at = None, 0.0
        self.ev_first, self.ev_done, self.ev_last_seq = None, None, None
        self.last_toggle = {}
        self.limiter = Limiter()
        self.areas_value = None

    def config(self):
        mt = max(os.path.getmtime(p) if os.path.exists(p) else 0 for p in (CONFIG, AUTO))
        if mt != self.cfg_mtime:
            cfg = load_config()
            if self.reader is None or cfg.get("read") != (self.cfg or {}).get("read"):
                self.reader = make_reader(cfg)
            self.cfg, self.cfg_mtime = cfg, mt
            log("config loaded:", len(cfg["_slots"]), "slots,", sum(1 for s in cfg["_slots"].values() if s["actions"]),
                "with actions, read path", cfg.get("read", "rest"))
        return self.cfg

    # ---- states ----
    def tz_text(self):
        """ha.sys.tz_off (minutes east of UTC, daylight saving included) and ha.sys.tz_name from the Home Assistant time zone,
        refreshed every 5 minutes; empty text when it cannot be read (the device then falls back to its own setting or UTC)."""
        now = time.time()
        if now - getattr(self, "tz_at", 0) > 300 or not hasattr(self, "tz_cache"):
            self.tz_at = now
            try:
                out = ha_template("{{ (now().utcoffset().total_seconds() // 60) | int }}|{{ now().tzinfo }}").strip()
                off, name = out.split("|", 1)
                off = int(off)
                if -720 <= off <= 840:
                    self.tz_cache = f"ha.sys.tz_off={off}\nha.sys.tz_name={ascii_text(name, 32)}\n"
            except Exception as e:
                log(f"time zone read failed: {type(e).__name__}")
                self.tz_cache = getattr(self, "tz_cache", "")
        return getattr(self, "tz_cache", "")

    def states_cycle(self):
        cfg = self.config()
        ents = [s["entity"] for s in cfg["_slots"].values()]
        helper = cfg.get("areas_helper")
        if helper and ENTITY_RE.match(helper):
            ents.append(helper)
        states = self.reader(ents)
        self.watch_areas(cfg, states.get(helper) if helper else None)
        text, dropped = build_states(cfg, states)
        text += self.tz_text()
        if dropped:
            log(f"states: {dropped} low-priority keys left out (limit {MAX_KEYS} keys / {MAX_BYTES} bytes)")
        stable = "".join(l for l in text.splitlines(True) if ".age=" not in l)
        if self.no_device:
            return text
        if stable == self.last_push_text and time.time() - self.last_push_at < PUSH_EVERY_S:
            return text
        with tempfile.NamedTemporaryFile("w", dir=RT, prefix="ha.states.", delete=False) as f:
            f.write(text)
            tmp = f.name
        try:
            how = ftp_put_atomic(tmp, STATES_TMP, STATES_PATH)
        finally:
            os.unlink(tmp)
        if stable != self.last_push_text:
            log(f"states pushed ({len(text)} bytes, {text.count(chr(10))} keys, rename via {how})")
        self.last_push_text, self.last_push_at = stable, time.time()
        return text

    def watch_areas(self, cfg, st):
        """HA-side page list (input_text.homtom_ui_areas): regenerate ui.conf when it changes and auto_regen is on."""
        if not st or st["s"] in ("unavailable", "unknown"):
            return
        v = st["s"]
        if self.areas_value is not None and v != self.areas_value:
            log("areas helper changed:", ascii_text(v, 60))
            if cfg.get("auto_regen") and not self.dry_run:
                args = [sys.executable, "-I", os.path.join(ROOT, "tools", "homtom-ha-ui-gen.py")]
                if cfg.get("auto_upload"):
                    args.append("--upload")
                r = subprocess.run(args, capture_output=True, text=True, timeout=120)
                tail = (r.stdout.strip().splitlines() or [""])[-1]
                log("generator exit", r.returncode, tail)
        self.areas_value = v

    # ---- events ----
    def events_cycle(self):
        data = ftp_get(EVENTS_PATH)
        if data is None:
            if self.ev_first is not None:
                log("events: file gone (device reboot or GUI restart); waiting for a new one")
            self.ev_first, self.ev_done = None, 0
            return
        if len(data) >= EVENTS_ROTATE_BYTES and not self.dry_run and self.cfg.get("rotate_events", True):
            data = ftp_rotate_events()
            log(f"events: rotated {len(data)} bytes")
            self.handle_lines(data, rotated=True)
            self.ev_first, self.ev_done = None, 0
            return
        self.handle_lines(data)

    def handle_lines(self, data, rotated=False):
        text = data.decode("ascii", "replace")
        lines = text.split("\n")[:-1]
        if not lines:
            return
        if self.ev_done is None:
            self.ev_first, self.ev_done = lines[0], len(lines)
            log(f"events: skipping {len(lines)} lines written before this daemon started")
            return
        if lines[0] != self.ev_first or len(lines) < self.ev_done:
            if self.ev_first is not None:
                log("events: new file (device reboot, GUI restart or rotation); reading it from the start")
            self.ev_first, self.ev_done, self.ev_last_seq = lines[0], 0, None
        new = lines[self.ev_done:]
        self.ev_done = len(lines)
        if not new:
            return
        dev_up = device_uptime()
        batch = []
        for raw in new:
            m = EVENT_RE.match(raw.strip())
            if not m:
                log("events: unparsed line skipped")
                continue
            seq, up, ev, name, value = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4), m.group(5)
            if self.ev_last_seq is not None and seq != self.ev_last_seq + 1:
                log(f"events: seq {self.ev_last_seq} -> {seq}" + (" (GUI restart)" if seq == 1 else " (gap)"))
            self.ev_last_seq = seq
            if ev != "ha":
                continue
            if dev_up is not None and dev_up - up > STALE_S:
                log(f"refused slot={name} op={value} result=stale ({dev_up - up} s old)")
                continue
            batch.append((name, value))
        coalesced = []
        for i, (name, value) in enumerate(batch):
            op = value.split(":", 1)[0]
            if op.startswith("set_") and any(n == name and v.split(":", 1)[0] == op for n, v in batch[i + 1:]):
                continue
            coalesced.append((name, value))
        for name, value in coalesced:
            self.action(name, value)

    def action(self, sid, value):
        op, _, arg = value.partition(":")
        cfg = self.config()
        slot = cfg["_slots"].get(sid)
        if os.path.exists(DISABLE):
            return log(f"refused slot={sid} op={value} result=disabled (kill switch)")
        if not SLOT_RE.match(sid) or slot is None or not slot["actions"]:
            return log(f"refused slot={ascii_text(sid, 20)} op={ascii_text(value, 30)} result=slot not mapped")
        try:
            payload = to_payload(slot, op, arg)
        except (ValueError, TypeError) as e:
            return log(f"refused slot={sid} op={ascii_text(value, 30)} result=invalid ({e})")
        if op in ("toggle", "press"):
            now = time.monotonic()
            if now - self.last_toggle.get(sid, -9) < DEBOUNCE_S:
                return log(f"refused slot={sid} op={op} result=debounced")
            self.last_toggle[sid] = now
        if not self.limiter.allow():
            return log(f"refused slot={sid} op={value} result=rate limited")
        detail = payload["op"] + (f":{payload['value']}" if "value" in payload else "")
        if op == "press":
            detail += " (button press, no state change expected)"
        if self.dry_run:
            return log(f"dry-run slot={sid} op={detail} entity={slot['entity']}")
        try:
            status = post_webhook(payload)
            log(f"action slot={sid} op={detail} result=http {status}")
        except Exception as e:
            log(f"action slot={sid} op={detail} result=error {type(e).__name__}: {e}")

    def run(self, once=False):
        log("start", "(dry-run)" if self.dry_run else "")
        next_state = next_ev = 0.0
        while True:
            now = time.monotonic()
            if now >= next_state:
                next_state = now + float(self.config().get("state_interval_s", 2))
                try:
                    self.states_cycle()
                except Exception as e:
                    log("states error:", type(e).__name__, e)
            if now >= next_ev and not self.no_device:
                next_ev = now + float(self.config().get("event_interval_s", 1))
                try:
                    self.events_cycle()
                except Exception as e:
                    log("events error:", type(e).__name__, e)
            if once:
                return
            time.sleep(0.2)


if __name__ == "__main__":
    args = sys.argv[1:]
    dry = "--dry-run" in args
    args = [a for a in args if a != "--dry-run"]
    if args[:1] == ["--print-template"]:
        c = load_config()
        print(states_template([s["entity"] for s in c["_slots"].values()]))
        sys.exit(0)
    if "HA_URL" not in os.environ:
        offline = load_config().get("read", "rest") == "file" and (dry or args[:1] == ["--print-states"])
        if not offline:
            sys.exit("HA_URL must be set in the environment")
    if args[:1] == ["--print-states"]:
        d = Daemon(no_device=True)
        sys.stdout.write(d.states_cycle())
        sys.exit(0)
    if args[:1] == ["--action"]:
        if len(args) != 3:
            sys.exit("usage: --action SLOT OP[:ARG]")
        Daemon(dry_run=dry).action(args[1], args[2])
        sys.exit(0)
    d = Daemon(dry_run=dry, no_device="--no-device" in args)
    try:
        d.run(once="--once" in args)
    except KeyboardInterrupt:
        log("stop")

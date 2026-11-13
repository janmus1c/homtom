#!/usr/bin/env python3
"""homtom gateway: serves Home Assistant tiles to the TomTom panel over the USB link.

The device speaks plain HTTP/1.0 with a tab-separated line format (see README.md). This side holds
the Home Assistant URL and token, follows state over the WebSocket API (REST polling as fallback)
and calls services. Python 3 standard library only.
"""
import argparse
import base64
import errno
import json
import os
import socket
import ssl
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PER_PAGE = 6
MAX_WAIT = 30
AUTO_DOMAINS = ("light", "switch", "climate", "scene", "fan", "cover", "input_boolean", "script")
TOGGLE_DOMAINS = {"light", "switch", "input_boolean", "fan", "cover", "humidifier", "siren", "media_player"}
PRESS_SERVICES = {"scene": "turn_on", "script": "turn_on", "button": "press", "input_button": "press"}
ICONS = {"light": "light", "switch": "switch", "input_boolean": "switch", "climate": "climate",
         "sensor": "sensor", "binary_sensor": "sensor", "scene": "scene", "script": "script",
         "button": "script", "input_button": "script", "fan": "fan", "cover": "cover", "lock": "lock"}
UNITS_TIGHT = ("°C", "°F", "%", "°")


def log(*parts):
    print("gateway:", *parts, file=sys.stderr, flush=True)


def clean(text, limit=48):
    return " ".join(str(text).replace("\t", " ").split())[:limit]


def number(value):
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return f


def fmt_num(value):
    f = number(value)
    if f is None:
        return clean(value, 16)
    return ("%.1f" % f).rstrip("0").rstrip(".") if f != int(f) else str(int(f))


def with_unit(value, unit):
    if not unit:
        return fmt_num(value)
    return fmt_num(value) + ("" if unit in UNITS_TIGHT else " ") + unit


def describe(entity_id, state, options=None):
    """(icon, action, on, value) for one entity; state is an HA state object or None."""
    options = options or {}
    domain = entity_id.split(".", 1)[0]
    icon = options.get("icon") or ICONS.get(domain, "generic")
    if domain in TOGGLE_DOMAINS or domain == "lock":
        action = "t"
    elif domain in PRESS_SERVICES:
        action = "p"
    elif domain == "climate":
        action = "c"
    else:
        action = "-"
    if options.get("readonly") or (domain == "lock" and not options.get("allow_unlock")):
        action = "-"
    if not state or state.get("state") in ("unavailable", "unknown", None):
        if state and state.get("state") == "unknown" and domain in PRESS_SERVICES:
            return icon, action, "0", "Scene" if domain == "scene" else "Run"
        return icon, action, "u", "Unavailable"
    s, attrs = state["state"], state.get("attributes") or {}
    if domain == "light":
        pct = attrs.get("brightness")
        if s == "on" and number(pct) is not None:
            return icon, action, "1", "On · %d%%" % round(number(pct) * 100 / 255)
        return icon, action, "1" if s == "on" else "0", "On" if s == "on" else "Off"
    if domain == "climate":
        target = attrs.get("temperature")
        current = attrs.get("current_temperature")
        if s == "off":
            return icon, action, "0", "Off" + (" · %s°" % fmt_num(current) if current is not None else "")
        mode = s.replace("_", " ").capitalize()
        shown = target if target is not None else current
        return icon, action, "1", mode + (" · %s°" % fmt_num(shown) if shown is not None else "")
    if domain in PRESS_SERVICES:
        on = "1" if domain == "script" and s == "on" else "0"
        return icon, action, on, {"scene": "Scene", "script": "Running" if on == "1" else "Script"}.get(domain, "Run")
    if domain == "cover":
        return icon, action, "1" if s in ("open", "opening") else "0", s.capitalize()
    if domain == "lock":
        return icon, action, "1" if s == "locked" else "0", s.capitalize()
    if domain in ("sensor", "input_number", "number"):
        return icon, action, "0", with_unit(s, attrs.get("unit_of_measurement"))
    if s in ("on", "off"):
        return icon, action, "1" if s == "on" else "0", "On" if s == "on" else "Off"
    return icon, action, "0", clean(s, 24).capitalize()


def service_for(entity_id, state, verb):
    """(domain, service, data) for a panel action, or None when the entity takes none."""
    domain = entity_id.split(".", 1)[0]
    data = {"entity_id": entity_id}
    attrs = (state or {}).get("attributes") or {}
    if verb == "toggle":
        if domain in TOGGLE_DOMAINS:
            return domain, "toggle", data
        if domain in PRESS_SERVICES:
            return domain, PRESS_SERVICES[domain], data
        if domain == "climate":
            return domain, "turn_on" if (state or {}).get("state") == "off" else "turn_off", data
        if domain == "lock":
            return domain, "unlock" if (state or {}).get("state") == "locked" else "lock", data
        return None
    if verb in ("up", "down") and domain == "climate":
        target = number(attrs.get("temperature"))
        if target is None:
            return None
        step = number(attrs.get("target_temp_step")) or 0.5
        target += step if verb == "up" else -step
        target = max(number(attrs.get("min_temp")) or 5, min(number(attrs.get("max_temp")) or 35, target))
        data["temperature"] = round(target, 1)
        return domain, "set_temperature", data
    return None


class Store:
    """States plus a change counter. Only formatted changes of panel entities advance it."""

    def __init__(self):
        self.cond = threading.Condition()
        self.states = {}
        self.views = {}
        self.changed = {}
        self.watched = set()
        self.options = {}
        # Wall-clock start keeps the counter rising across gateway restarts.
        self.seq = int(time.time())
        self.layout_seq = self.seq

    def watch(self, entity_ids, options):
        with self.cond:
            self.watched = set(entity_ids)
            self.options = options
            self.seq += 1
            self.layout_seq = self.seq
            self.views = {}
            for entity_id in self.watched:
                self._refresh(entity_id)
            self.cond.notify_all()

    def _refresh(self, entity_id):
        view = describe(entity_id, self.states.get(entity_id), self.options.get(entity_id))
        if self.views.get(entity_id) != view:
            self.views[entity_id] = view
            self.seq += 1
            self.changed[entity_id] = self.seq
            return True
        return False

    def update(self, states, replace=False):
        with self.cond:
            if replace:
                self.states = {}
            changed = False
            for state in states:
                entity_id = state.get("entity_id")
                if not entity_id:
                    continue
                self.states[entity_id] = state
                if entity_id in self.watched:
                    changed |= self._refresh(entity_id)
            if replace:
                for entity_id in self.watched:
                    changed |= self._refresh(entity_id)
            if changed:
                self.cond.notify_all()

    def remove(self, entity_id):
        with self.cond:
            self.states.pop(entity_id, None)
            if entity_id in self.watched and self._refresh(entity_id):
                self.cond.notify_all()

    def get(self, entity_id):
        with self.cond:
            return self.states.get(entity_id)

    def view(self, entity_id):
        with self.cond:
            return self.views.get(entity_id) or describe(entity_id, None, self.options.get(entity_id))

    def wait_changes(self, entity_ids, since, timeout):
        """(seq, reload, [entity ids changed after since]) once something changes or at timeout."""
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                if since < self.layout_seq or since > self.seq:
                    return self.seq, True, []
                changed = [e for e in entity_ids if self.changed.get(e, 0) > since]
                left = deadline - time.monotonic()
                if changed or left <= 0:
                    return self.seq, False, changed
                self.cond.wait(left)


MOCK_STATES = [
    ("light.living_room", "on", {"friendly_name": "Living room", "brightness": 204}),
    ("light.kitchen", "off", {"friendly_name": "Kitchen", "brightness": None}),
    ("switch.coffee_machine", "off", {"friendly_name": "Coffee machine"}),
    ("climate.living_room", "heat", {"friendly_name": "Thermostat", "temperature": 21.5,
                                     "current_temperature": 20.4, "min_temp": 7, "max_temp": 30,
                                     "target_temp_step": 0.5, "hvac_modes": ["off", "heat"]}),
    ("sensor.outdoor_temperature", "8.4", {"friendly_name": "Outside", "unit_of_measurement": "°C"}),
    ("scene.movie_night", "unknown", {"friendly_name": "Movie night"}),
    ("light.hallway", "off", {"friendly_name": "Hallway"}),
    ("switch.garden_pump", "on", {"friendly_name": "Garden pump"}),
    ("sensor.living_room_humidity", "46.2", {"friendly_name": "Humidity", "unit_of_measurement": "%"}),
    ("lock.front_door", "locked", {"friendly_name": "Front door"}),
]

MOCK_PANELS = {"default": {"title": "Home", "pages": [
    {"title": "Living room", "entities": ["light.living_room", "switch.coffee_machine", "climate.living_room",
                                          "sensor.outdoor_temperature", "scene.movie_night", "light.kitchen"]},
    {"title": "House", "entities": ["light.hallway", "switch.garden_pump", "sensor.living_room_humidity",
                                    "lock.front_door"]},
]}}


class MockBackend:
    """Fake Home Assistant: a handful of entities whose services behave like the real ones."""

    def __init__(self, store):
        self.store = store
        self.calls = []
        self.lock = threading.Lock()

    def start(self):
        self.store.update([dict(entity_id=e, state=s, attributes=dict(a)) for e, s, a in MOCK_STATES], replace=True)

    def all_states(self):
        return list(self.store.states.values())

    def call(self, domain, service, data):
        entity_id = data["entity_id"]
        with self.lock:
            self.calls.append(dict(domain=domain, service=service, data=data, at=time.time()))
        state = json.loads(json.dumps(self.store.get(entity_id) or {"entity_id": entity_id, "attributes": {}}))
        attrs = state["attributes"]
        if service == "toggle":
            if domain == "cover":
                state["state"] = "closed" if state.get("state") == "open" else "open"
            else:
                state["state"] = "off" if state.get("state") == "on" else "on"
            if domain == "light":
                attrs["brightness"] = 255 if state["state"] == "on" else None
        elif service in ("turn_on", "turn_off"):
            if domain == "climate":
                state["state"] = "heat" if service == "turn_on" else "off"
            elif domain == "scene":
                state["state"] = time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime())
            else:
                state["state"] = "on" if service == "turn_on" else "off"
        elif service == "set_temperature":
            attrs["temperature"] = data["temperature"]
        elif service in ("lock", "unlock"):
            state["state"] = "locked" if service == "lock" else "unlocked"
        self.store.update([state])
        return [state]

    def set_state(self, entity_id, value):
        state = json.loads(json.dumps(self.store.get(entity_id) or {"entity_id": entity_id, "attributes": {}}))
        state["state"] = value
        self.store.update([state])


class WebSocket:
    """Just enough RFC 6455 client for the Home Assistant WebSocket API."""

    def __init__(self, url, timeout=10):
        parts = urllib.parse.urlsplit(url)
        secure = parts.scheme in ("https", "wss")
        port = parts.port or (443 if secure else 80)
        sock = socket.create_connection((parts.hostname, port), timeout=timeout)
        if secure:
            sock = ssl.create_default_context().wrap_socket(sock, server_hostname=parts.hostname)
        self.sock = sock
        self.buf = b""
        try:
            self._handshake(parts, port)
        except BaseException:
            self.close()
            raise

    def _handshake(self, parts, port):
        key = base64.b64encode(os.urandom(16)).decode()
        path = (parts.path.rstrip("/") or "") + "/api/websocket"
        self.sock.sendall(("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                           % (path, parts.hostname, port, key)).encode())
        while b"\r\n\r\n" not in self.buf:
            self._fill()
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError("websocket upgrade refused: %s" % head.split(b"\r\n", 1)[0].decode(errors="replace"))

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise ConnectionError("websocket closed")
        self.buf += chunk

    def _take(self, n):
        while len(self.buf) < n:
            self._fill()
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            head = struct.pack("!BB", 0x80 | opcode, 0x80 | n)
        elif n < 65536:
            head = struct.pack("!BBH", 0x80 | opcode, 0xFE, n)
        else:
            head = struct.pack("!BBQ", 0x80 | opcode, 0xFF, n)
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def send(self, obj):
        self._send_frame(1, json.dumps(obj).encode())

    def recv(self):
        message = b""
        while True:
            b0, b1 = self._take(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack("!H", self._take(2))[0]
            elif n == 127:
                n = struct.unpack("!Q", self._take(8))[0]
            mask = self._take(4) if b1 & 0x80 else None
            payload = self._take(n)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            opcode = b0 & 0x0F
            if opcode == 8:
                raise ConnectionError("websocket closed by server")
            if opcode == 9:
                self._send_frame(10, payload)
                continue
            if opcode in (0, 1, 2):
                message += payload
                if b0 & 0x80:
                    return json.loads(message)

    def settimeout(self, value):
        self.sock.settimeout(value)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class HomeAssistantBackend:
    def __init__(self, store, url, token, websocket=True, poll_seconds=10):
        self.store = store
        self.url = url.rstrip("/")
        self.token = token
        self.websocket = websocket
        self.poll_seconds = poll_seconds
        self.connected = threading.Event()
        self.stop = threading.Event()
        self.on_sync = None

    def _synced(self, states):
        self.store.update(states, replace=True)
        if self.on_sync:
            self.on_sync()

    def _rest(self, method, path, body=None, timeout=10):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method, headers={
            "Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read() or b"null")
        except urllib.error.HTTPError as e:
            e.close()
            raise

    def sync(self):
        self._synced(self._rest("GET", "/api/states"))

    def all_states(self):
        return list(self.store.states.values())

    def call(self, domain, service, data):
        changed = self._rest("POST", "/api/services/%s/%s" % (domain, service), data) or []
        self.store.update(changed)
        return changed

    def start(self):
        try:
            self.sync()
            self.connected.set()
        except (OSError, ValueError, urllib.error.URLError) as e:
            log("first Home Assistant sync failed:", type(e).__name__, e)
        threading.Thread(target=self._run, daemon=True, name="ha-follow").start()

    def _run(self):
        backoff = 2
        while not self.stop.is_set():
            try:
                if self.websocket:
                    self._follow_websocket()
                else:
                    self.sync()
                    self.connected.set()
                    self.stop.wait(self.poll_seconds)
                    continue
            except (OSError, ValueError, ConnectionError, urllib.error.URLError) as e:
                self.connected.clear()
                log("Home Assistant link lost:", type(e).__name__, e)
            try:
                self.sync()
            except (OSError, ValueError, urllib.error.URLError):
                pass
            self.stop.wait(backoff)
            backoff = min(backoff * 2, 30)

    def _follow_websocket(self):
        ws = WebSocket(self.url)
        try:
            if ws.recv().get("type") != "auth_required":
                raise ConnectionError("unexpected websocket greeting")
            ws.send({"type": "auth", "access_token": self.token})
            reply = ws.recv()
            if reply.get("type") != "auth_ok":
                raise ConnectionError("websocket authentication failed")
            ws.send({"id": 1, "type": "subscribe_events", "event_type": "state_changed"})
            ws.send({"id": 2, "type": "get_states"})
            ws.settimeout(60)
            pinged = time.monotonic()
            next_id = 3
            while not self.stop.is_set():
                try:
                    msg = ws.recv()
                except socket.timeout:
                    if time.monotonic() - pinged > 90:
                        raise ConnectionError("websocket ping timeout")
                    ws.send({"id": next_id, "type": "ping"})
                    next_id += 1
                    continue
                pinged = time.monotonic()
                if msg.get("type") == "result" and msg.get("id") == 2 and msg.get("success"):
                    self._synced(msg.get("result") or [])
                    self.connected.set()
                elif msg.get("type") == "event":
                    data = (msg.get("event") or {}).get("data") or {}
                    if data.get("new_state"):
                        self.store.update([data["new_state"]])
                    elif data.get("entity_id"):
                        self.store.remove(data["entity_id"])
        finally:
            ws.close()


class Panels:
    """Per-panel layout from the config, or every useful entity when no panel is configured."""

    def __init__(self, config, backend, store):
        self.config = config
        self.backend = backend
        self.store = store
        self.layouts = {}

    def build(self):
        panels = self.config.get("panels") or {}
        if not panels:
            entities = sorted(s["entity_id"] for s in self.backend.all_states()
                              if s["entity_id"].split(".", 1)[0] in AUTO_DOMAINS)
            pages = [dict(title="Home", entities=entities[i:i + PER_PAGE]) for i in range(0, len(entities), PER_PAGE)]
            panels = {"default": {"title": "Home", "pages": pages or [dict(title="Home", entities=[])]}}
        layouts, watched, options = {}, set(), {}
        for panel_id, panel in panels.items():
            pages = []
            for page in panel.get("pages", []):
                tiles = []
                for item in page.get("entities", []):
                    item = {"entity": item} if isinstance(item, str) else dict(item)
                    entity_id = item.get("entity", "")
                    if not valid_entity(entity_id):
                        log("skipping invalid entity id in panel", panel_id)
                        continue
                    tiles.append((entity_id, item.get("name")))
                    watched.add(entity_id)
                    options[entity_id] = {k: item[k] for k in ("readonly", "allow_unlock", "icon") if k in item}
                pages.append((clean(page.get("title", ""), 30), tiles))
            layouts[panel_id] = (clean(panel.get("title", "Home"), 30), pages)
        if layouts == self.layouts and watched == self.store.watched and options == self.store.options:
            return
        self.layouts = layouts
        self.store.watch(watched, options)

    def get(self, panel_id):
        return self.layouts.get(panel_id) or self.layouts.get("default")

    def entity_ids(self, panel_id):
        layout = self.get(panel_id)
        return [e for _, tiles in layout[1] for e, _ in tiles] if layout else []

    def name(self, panel_id, entity_id):
        for _, tiles in (self.get(panel_id) or ("", []))[1]:
            for e, name in tiles:
                if e == entity_id and name:
                    return name
        state = self.store.get(entity_id) or {}
        return (state.get("attributes") or {}).get("friendly_name") or entity_id


def valid_entity(entity_id):
    domain, _, obj = entity_id.partition(".")
    ok = set("abcdefghijklmnopqrstuvwxyz0123456789_")
    return bool(domain and obj) and set(domain) <= ok and set(obj) <= ok and len(entity_id) < 64


class Gateway:
    def __init__(self, config, backend, store, mock=False):
        self.config = config
        self.backend = backend
        self.store = store
        self.panels = Panels(config, backend, store)
        self.mock = mock
        self.allow = set(config.get("allow") or ["192.168.1.10", "127.0.0.1", "::1"])

    def state_line(self, entity_id):
        _, _, on, value = self.store.view(entity_id)
        return "S\t%s\t%s\t%s\n" % (entity_id, on, clean(value))

    def panel_text(self, panel_id):
        title, pages = self.panels.get(panel_id) or ("Home", [])
        out = ["H\t1\t%d\t%s\n" % (self.store.seq, title)]
        for page_title, tiles in pages:
            out.append("P\t%s\n" % page_title)
            for entity_id, _ in tiles:
                icon, action, on, value = self.store.view(entity_id)
                out.append("E\t%s\t%s\t%s\t%s\t%s\t%s\n" % (entity_id, icon, action, on, clean(value),
                                                             clean(self.panels.name(panel_id, entity_id), 30)))
        return "".join(out)

    def changes_text(self, panel_id, since, wait):
        seq, reload, changed = self.store.wait_changes(self.panels.entity_ids(panel_id), since, wait)
        if reload:
            return "H\t1\t%d\nR\n" % seq
        return "H\t1\t%d\n" % seq + "".join(self.state_line(e) for e in changed)

    def act(self, panel_id, entity_id, verb):
        if entity_id not in self.panels.entity_ids(panel_id):
            return 404, "not on this panel\n"
        icon, action, on, _ = self.store.view(entity_id)
        if action == "-" or (action != "c" and verb != "toggle"):
            return 403, "read only\n"
        call = service_for(entity_id, self.store.get(entity_id), verb)
        if not call:
            return 400, "no action\n"
        try:
            self.backend.call(*call)
        except (OSError, ValueError, urllib.error.URLError) as e:
            log("service call failed:", call[0], call[1], type(e).__name__)
            return 502, "service call failed\n"
        return 200, "H\t1\t%d\n%s" % (self.store.seq, self.state_line(entity_id))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "homtom-gateway"
    gateway = None
    verbose = False

    def log_message(self, fmt, *args):
        if self.verbose:
            log(self.address_string(), fmt % args)

    def reply(self, code, text):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def route(self, method):
        gw = self.gateway
        if self.client_address[0] not in gw.allow and "*" not in gw.allow:
            return self.reply(403, "forbidden\n")
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query))
        panel_id = query.get("panel", "default")
        parts = [p for p in url.path.split("/") if p]
        if method == "GET" and parts == ["v1", "health"]:
            return self.reply(200, "ok\n")
        if method == "GET" and parts == ["v1", "panel"]:
            return self.reply(200, gw.panel_text(panel_id))
        if method == "GET" and parts == ["v1", "changes"]:
            try:
                since = int(query.get("since", "0"))
                wait = max(0, min(MAX_WAIT, int(query.get("wait", "25"))))
            except ValueError:
                return self.reply(400, "bad query\n")
            return self.reply(200, gw.changes_text(panel_id, since, wait))
        if method == "POST" and len(parts) == 3 and parts[:2] == ["v1", "toggle"]:
            return self.reply(*gw.act(panel_id, parts[2], "toggle"))
        if method == "POST" and len(parts) == 4 and parts[:2] == ["v1", "adjust"] and parts[3] in ("up", "down"):
            return self.reply(*gw.act(panel_id, parts[2], parts[3]))
        if gw.mock and parts[:2] == ["v1", "debug"]:
            if method == "GET" and parts[2:] == ["calls"]:
                with gw.backend.lock:
                    return self.reply(200, json.dumps(gw.backend.calls) + "\n")
            if method == "POST" and len(parts) == 4 and parts[2] == "state" and valid_entity(parts[3]):
                gw.backend.set_state(parts[3], query.get("state", "off"))
                return self.reply(200, "ok\n")
        return self.reply(404, "not found\n")

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(min(length, 4096))
        self.route("POST")


def load_config(path):
    config = {}
    if path:
        with open(os.path.expanduser(path)) as f:
            config = json.load(f)
    return config


def read_token(config):
    token = os.environ.get("HOMTOM_HA_TOKEN")
    if not token and config.get("token_file"):
        with open(os.path.expanduser(config["token_file"])) as f:
            token = f.read().strip()
    return token


def serve(bind, port, handler):
    # The USB address appears only once the TomTom is plugged in; wait for it.
    while True:
        try:
            server = ThreadingHTTPServer((bind, port), handler)
            server.daemon_threads = True
            return server
        except OSError as e:
            if e.errno != errno.EADDRNOTAVAIL:
                raise
            log("waiting for %s to appear" % bind)
            time.sleep(5)


def watch_config(path, gateway):
    mtime = os.path.getmtime(path)
    while True:
        time.sleep(2)
        try:
            now = os.path.getmtime(path)
            if now != mtime:
                mtime = now
                gateway.config.update(load_config(path))
                gateway.panels.config = gateway.config
                gateway.panels.build()
                log("config reloaded")
        except (OSError, ValueError) as e:
            log("config reload failed:", type(e).__name__, e)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", help="JSON config: ha_url, token_file, bind, port, allow, panels")
    parser.add_argument("--mock", action="store_true", help="serve fake entities instead of Home Assistant")
    parser.add_argument("--bind", help="listen address (default 192.168.1.100, the Mac side of the USB link)")
    parser.add_argument("--port", type=int)
    parser.add_argument("--allow", action="append", help="client address allowed to connect ('*' for any)")
    parser.add_argument("--no-websocket", action="store_true", help="poll the REST API instead")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    config = load_config(args.config)
    if args.allow:
        config["allow"] = args.allow
    store = Store()
    if args.mock:
        backend = MockBackend(store)
        config.setdefault("panels", MOCK_PANELS)
    else:
        url = os.environ.get("HOMTOM_HA_URL") or config.get("ha_url")
        token = read_token(config)
        if not url or not token:
            parser.error("set HOMTOM_HA_URL and HOMTOM_HA_TOKEN (or ha_url and token_file in --config), or use --mock")
        backend = HomeAssistantBackend(store, url, token, websocket=not args.no_websocket,
                                       poll_seconds=config.get("poll_seconds", 10))
    gateway = Gateway(config, backend, store, mock=args.mock)
    backend.on_sync = gateway.panels.build
    backend.start()
    gateway.panels.build()
    Handler.gateway = gateway
    Handler.verbose = args.verbose
    bind = args.bind or config.get("bind", "192.168.1.100")
    port = args.port or config.get("port", 8130)
    server = serve(bind, port, Handler)
    if args.config:
        threading.Thread(target=watch_config, args=(args.config, gateway), daemon=True).start()
    log("serving %s on %s:%d%s" % ("mock entities" if args.mock else "Home Assistant", bind, port,
                                   "" if args.mock else " for " + urllib.parse.urlsplit(backend.url).netloc))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

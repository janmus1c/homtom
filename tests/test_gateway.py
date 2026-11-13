import base64
import contextlib
import hashlib
import io
import json
import socket
import socketserver
import struct
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "gateway"))
import homtom_gateway as gw  # noqa: E402

TOKEN = "test-token-not-real"


def state(entity_id, value, **attrs):
    return {"entity_id": entity_id, "state": value, "attributes": attrs}


class FakeHA:
    """Home Assistant stand-in: REST states and services, WebSocket auth, get_states and state_changed."""

    def __init__(self, states, ws_enabled=True):
        self.states = {s["entity_id"]: s for s in states}
        self.calls = []
        self.auth_headers = []
        self.ws_enabled = ws_enabled
        self.ws_clients = []
        self.lock = threading.Lock()
        fake = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                fake.handle(self.request)

        self.server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        for sock in self.ws_clients:
            with contextlib.suppress(OSError):
                sock.close()

    def handle(self, sock):
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                return
            data += chunk
        head, body = data.split(b"\r\n\r\n", 1)
        lines = head.decode().split("\r\n")
        method, path, _ = lines[0].split(" ", 2)
        headers = {k.lower(): v.strip() for k, v in (line.split(":", 1) for line in lines[1:])}
        if headers.get("upgrade", "").lower() == "websocket":
            return self.websocket(sock, headers)
        length = int(headers.get("content-length", 0))
        while len(body) < length:
            body += sock.recv(65536)
        self.auth_headers.append(headers.get("authorization"))
        if headers.get("authorization") != "Bearer " + TOKEN:
            return self.respond(sock, 401, [])
        if method == "GET" and path == "/api/states":
            with self.lock:
                return self.respond(sock, 200, list(self.states.values()))
        if method == "POST" and path.startswith("/api/services/"):
            domain, service = path.split("/")[3:5]
            data = json.loads(body or b"{}")
            with self.lock:
                self.calls.append((domain, service, data))
            changed = self.apply(domain, service, data)
            return self.respond(sock, 200, changed)
        self.respond(sock, 404, {})

    def apply(self, domain, service, data):
        entity_id = data["entity_id"]
        with self.lock:
            st = json.loads(json.dumps(self.states[entity_id]))
            if service == "toggle":
                st["state"] = "off" if st["state"] == "on" else "on"
            elif service == "set_temperature":
                st["attributes"]["temperature"] = data["temperature"]
            elif service in ("turn_on", "turn_off"):
                st["state"] = "on" if service == "turn_on" else "off"
            self.states[entity_id] = st
        return [st]

    def push(self, new_state):
        with self.lock:
            self.states[new_state["entity_id"]] = new_state
            clients = list(self.ws_clients)
        for sock in clients:
            with contextlib.suppress(OSError):
                self.send_ws(sock, {"id": 1, "type": "event", "event": {"event_type": "state_changed", "data": {
                    "entity_id": new_state["entity_id"], "new_state": new_state}}})

    @staticmethod
    def respond(sock, code, obj):
        body = json.dumps(obj).encode()
        sock.sendall(b"HTTP/1.0 %d X\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s"
                     % (code, len(body), body))

    @staticmethod
    def send_ws(sock, obj):
        payload = json.dumps(obj).encode()
        n = len(payload)
        head = struct.pack("!BB", 0x81, n) if n < 126 else (
            struct.pack("!BBH", 0x81, 126, n) if n < 65536 else struct.pack("!BBQ", 0x81, 127, n))
        sock.sendall(head + payload)

    @staticmethod
    def recv_ws(sock):
        def take(n):
            out = b""
            while len(out) < n:
                chunk = sock.recv(n - len(out))
                if not chunk:
                    raise ConnectionError
                out += chunk
            return out
        b0, b1 = take(2)
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack("!H", take(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", take(8))[0]
        mask = take(4)
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(take(n)))
        assert b1 & 0x80, "client frames must be masked"
        return json.loads(payload)

    def websocket(self, sock, headers):
        if not self.ws_enabled:
            return self.respond(sock, 404, {})
        accept = base64.b64encode(hashlib.sha1(
            (headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        sock.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        self.send_ws(sock, {"type": "auth_required"})
        try:
            auth = self.recv_ws(sock)
            if auth.get("access_token") != TOKEN:
                self.send_ws(sock, {"type": "auth_invalid"})
                return
            self.send_ws(sock, {"type": "auth_ok"})
            with self.lock:
                self.ws_clients.append(sock)
            while True:
                msg = self.recv_ws(sock)
                if msg.get("type") == "get_states":
                    with self.lock:
                        result = list(self.states.values())
                    self.send_ws(sock, {"id": msg["id"], "type": "result", "success": True, "result": result})
                else:
                    self.send_ws(sock, {"id": msg["id"], "type": "result", "success": True, "result": None})
        except (ConnectionError, OSError):
            pass


def wait_for(cond, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cond():
            return True
        time.sleep(0.02)
    return False


class GatewayServer:
    def __init__(self, config, backend, store, mock=False):
        self.gateway = gw.Gateway(config, backend, store, mock=mock)
        self.gateway.panels.build()
        handler = type("H", (gw.Handler,), {"gateway": self.gateway})
        self.server = gw.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server.daemon_threads = True
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def get(self, path, method="GET"):
        req = urllib.request.Request(self.base + path, data=b"" if method == "POST" else None, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                return resp.status, resp.read().decode()
        except urllib.error.HTTPError as e:
            with e:
                return e.code, e.read().decode()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def parse_lines(text):
    return [line.split("\t") for line in text.splitlines()]


class DescribeTest(unittest.TestCase):
    def test_formats_each_domain_for_the_tile(self):
        self.assertEqual(gw.describe("light.a", state("light.a", "on", brightness=204)), ("light", "t", "1", "On · 80%"))
        self.assertEqual(gw.describe("light.a", state("light.a", "off")), ("light", "t", "0", "Off"))
        self.assertEqual(gw.describe("switch.a", state("switch.a", "on")), ("switch", "t", "1", "On"))
        self.assertEqual(gw.describe("climate.a", state("climate.a", "heat", temperature=21.5, current_temperature=20)),
                         ("climate", "c", "1", "Heat · 21.5°"))
        self.assertEqual(gw.describe("climate.a", state("climate.a", "off", current_temperature=19.25)),
                         ("climate", "c", "0", "Off · 19.2°"))
        self.assertEqual(gw.describe("sensor.t", state("sensor.t", "8.4000001", unit_of_measurement="°C")),
                         ("sensor", "-", "0", "8.4°C"))
        self.assertEqual(gw.describe("sensor.p", state("sensor.p", "1200", unit_of_measurement="W"))[3], "1200 W")
        self.assertEqual(gw.describe("scene.a", state("scene.a", "unknown")), ("scene", "p", "0", "Scene"))
        self.assertEqual(gw.describe("scene.a", state("scene.a", "2026-11-02T10:00:00+00:00"))[2:], ("0", "Scene"))
        self.assertEqual(gw.describe("light.a", state("light.a", "unavailable"))[2:], ("u", "Unavailable"))
        self.assertEqual(gw.describe("light.a", None)[2], "u")

    def test_locks_and_readonly_entities_take_no_action(self):
        self.assertEqual(gw.describe("lock.door", state("lock.door", "locked"))[1:], ("-", "1", "Locked"))
        self.assertEqual(gw.describe("lock.door", state("lock.door", "locked"), {"allow_unlock": True})[1], "t")
        self.assertEqual(gw.describe("light.a", state("light.a", "on"), {"readonly": True})[1], "-")

    def test_service_mapping(self):
        self.assertEqual(gw.service_for("light.a", state("light.a", "on"), "toggle"),
                         ("light", "toggle", {"entity_id": "light.a"}))
        self.assertEqual(gw.service_for("scene.a", None, "toggle")[:2], ("scene", "turn_on"))
        self.assertEqual(gw.service_for("climate.a", state("climate.a", "off"), "toggle")[1], "turn_on")
        self.assertEqual(gw.service_for("climate.a", state("climate.a", "heat"), "toggle")[1], "turn_off")
        up = gw.service_for("climate.a", state("climate.a", "heat", temperature=29.8, max_temp=30), "up")
        self.assertEqual(up, ("climate", "set_temperature", {"entity_id": "climate.a", "temperature": 30}))
        down = gw.service_for("climate.a", state("climate.a", "heat", temperature=21, target_temp_step=1), "down")
        self.assertEqual(down[2]["temperature"], 20)
        self.assertIsNone(gw.service_for("sensor.a", state("sensor.a", "1"), "toggle"))
        self.assertIsNone(gw.service_for("light.a", state("light.a", "on"), "up"))

    def test_text_never_carries_tabs_or_newlines(self):
        self.assertEqual(gw.clean("a\tb\nc  d"), "a b c d")
        self.assertFalse(gw.valid_entity("light.a\tb"))
        self.assertFalse(gw.valid_entity("Light.A"))
        self.assertTrue(gw.valid_entity("light.living_room_2"))


class StoreTest(unittest.TestCase):
    def test_only_formatted_changes_of_watched_entities_advance(self):
        store = gw.Store()
        store.watch(["sensor.t"], {})
        store.update([state("sensor.t", "8.41", unit_of_measurement="°C")])
        seq = store.seq
        store.update([state("sensor.t", "8.44", unit_of_measurement="°C")])
        store.update([state("sensor.other", "1")])
        self.assertEqual(store.seq, seq)
        store.update([state("sensor.t", "9", unit_of_measurement="°C")])
        self.assertEqual(store.seq, seq + 1)

    def test_wait_changes(self):
        store = gw.Store()
        store.watch(["light.a", "light.b"], {})
        seq = store.seq
        started = time.monotonic()
        self.assertEqual(store.wait_changes(["light.a"], seq, 0.2), (seq, False, []))
        self.assertGreaterEqual(time.monotonic() - started, 0.18)
        threading.Timer(0.1, store.update, [[state("light.b", "on")]]).start()
        new_seq, reload, changed = store.wait_changes(["light.a", "light.b"], seq, 5)
        self.assertEqual((reload, changed), (False, ["light.b"]))
        self.assertGreater(new_seq, seq)
        # Older than the layout or newer than this gateway (a restart): the panel must reload.
        self.assertTrue(store.wait_changes(["light.a"], store.layout_seq - 1, 0)[1])
        self.assertTrue(store.wait_changes(["light.a"], store.seq + 5, 0)[1])


class MockGatewayTest(unittest.TestCase):
    def setUp(self):
        store = gw.Store()
        self.backend = gw.MockBackend(store)
        self.backend.start()
        self.srv = GatewayServer({"panels": gw.MOCK_PANELS}, self.backend, store, mock=True)

    def tearDown(self):
        self.srv.close()

    def test_panel_lists_pages_and_tiles(self):
        code, text = self.srv.get("/v1/panel")
        self.assertEqual(code, 200)
        lines = parse_lines(text)
        self.assertEqual(lines[0][:2], ["H", "1"])
        self.assertEqual(lines[0][3], "Home")
        self.assertEqual([l[1] for l in lines if l[0] == "P"], ["Living room", "House"])
        tiles = {l[1]: l for l in lines if l[0] == "E"}
        self.assertEqual(len(tiles), 10)
        self.assertTrue(all(len(l) == 7 for l in tiles.values()))
        self.assertEqual(tiles["light.living_room"][2:], ["light", "t", "1", "On · 80%", "Living room"])
        self.assertEqual(tiles["climate.living_room"][3], "c")
        self.assertEqual(tiles["lock.front_door"][3], "-")

    def test_toggle_reaches_the_backend_and_returns_the_new_state(self):
        code, text = self.srv.get("/v1/toggle/switch.coffee_machine", "POST")
        self.assertEqual(code, 200)
        self.assertEqual(parse_lines(text)[1], ["S", "switch.coffee_machine", "1", "On"])
        self.assertEqual([(c["domain"], c["service"]) for c in self.backend.calls], [("switch", "toggle")])
        code, text = self.srv.get("/v1/adjust/climate.living_room/down", "POST")
        self.assertEqual(parse_lines(text)[1], ["S", "climate.living_room", "1", "Heat · 21°"])

    def test_refusals(self):
        self.assertEqual(self.srv.get("/v1/toggle/lock.front_door", "POST")[0], 403)
        self.assertEqual(self.srv.get("/v1/toggle/sensor.outdoor_temperature", "POST")[0], 403)
        self.assertEqual(self.srv.get("/v1/adjust/light.kitchen/up", "POST")[0], 403)
        self.assertEqual(self.srv.get("/v1/toggle/light.not_on_panel", "POST")[0], 404)
        self.assertEqual(self.srv.get("/v1/changes?since=x")[0], 400)
        self.assertEqual(self.srv.get("/nope")[0], 404)
        self.assertEqual(self.backend.calls, [])

    def test_long_poll_wakes_on_change(self):
        seq = int(parse_lines(self.srv.get("/v1/panel")[1])[0][2])
        threading.Timer(0.2, self.backend.set_state, ["light.hallway", "on"]).start()
        started = time.monotonic()
        code, text = self.srv.get("/v1/changes?since=%d&wait=10" % seq)
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(parse_lines(text)[1], ["S", "light.hallway", "1", "On"])
        code, text = self.srv.get("/v1/changes?since=%s&wait=0" % parse_lines(text)[0][2])
        self.assertEqual(len(parse_lines(text)), 1)

    def test_debug_calls_list_is_json(self):
        self.srv.get("/v1/toggle/light.kitchen", "POST")
        calls = json.loads(self.srv.get("/v1/debug/calls")[1])
        self.assertEqual(calls[0]["data"], {"entity_id": "light.kitchen"})

    def test_clients_outside_the_allow_list_are_refused(self):
        self.srv.gateway.allow = {"192.168.1.10"}
        self.assertEqual(self.srv.get("/v1/panel")[0], 403)


class HomeAssistantBackendTest(unittest.TestCase):
    STATES = [state("light.desk", "off", friendly_name="Desk"),
              state("climate.office", "heat", temperature=20, current_temperature=19, friendly_name="Office"),
              state("sensor.power", "120", unit_of_measurement="W", friendly_name="Power"),
              state("automation.ignored", "on")]

    def start(self, websocket=True, ws_enabled=True, config=None, token=TOKEN):
        self.ha = FakeHA(self.STATES, ws_enabled=ws_enabled)
        self.addCleanup(self.ha.close)
        store = gw.Store()
        self.backend = gw.HomeAssistantBackend(store, self.ha.url, token, websocket=websocket, poll_seconds=0.2)
        self.addCleanup(self.backend.stop.set)
        config = config if config is not None else {"panels": {"default": {"title": "Office", "pages": [
            {"title": "Desk", "entities": ["light.desk", {"entity": "climate.office", "name": "Heating"},
                                           "sensor.power"]}]}}}
        self.srv = GatewayServer(config, self.backend, store)
        self.backend.on_sync = self.srv.gateway.panels.build
        self.addCleanup(self.srv.close)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.backend.start()
        self.log = stderr
        return store

    def test_websocket_push_and_service_calls(self):
        store = self.start()
        self.assertTrue(wait_for(lambda: self.ha.ws_clients))
        tiles = {l[1]: l for l in parse_lines(self.srv.get("/v1/panel")[1]) if l[0] == "E"}
        self.assertEqual(tiles["climate.office"][6], "Heating")
        self.assertEqual(tiles["light.desk"][5], "Off")
        seq = int(parse_lines(self.srv.get("/v1/panel")[1])[0][2])
        self.ha.push(state("sensor.power", "980", unit_of_measurement="W"))
        text = self.srv.get("/v1/changes?since=%d&wait=5" % seq)[1]
        self.assertEqual(parse_lines(text)[1], ["S", "sensor.power", "0", "980 W"])
        code, text = self.srv.get("/v1/toggle/light.desk", "POST")
        self.assertEqual((code, parse_lines(text)[1][2]), (200, "1"))
        self.assertEqual(self.srv.get("/v1/adjust/climate.office/up", "POST")[0], 200)
        self.assertEqual(self.ha.calls, [("light", "toggle", {"entity_id": "light.desk"}),
                                         ("climate", "set_temperature", {"entity_id": "climate.office",
                                                                         "temperature": 20.5})])
        self.assertTrue(all(h == "Bearer " + TOKEN for h in self.ha.auth_headers))
        self.assertEqual(store.get("climate.office")["attributes"]["temperature"], 20.5)

    def test_rest_polling_without_websocket(self):
        store = self.start(websocket=False)
        self.ha.states["light.desk"] = state("light.desk", "on")
        self.assertTrue(wait_for(lambda: store.view("light.desk")[2] == "1"))

    def test_falls_back_to_rest_when_the_websocket_is_unavailable(self):
        store = self.start(ws_enabled=False)
        self.assertEqual(store.view("light.desk")[3], "Off")
        self.ha.states["light.desk"] = state("light.desk", "on")
        # The follow loop retries the socket after a REST resync.
        self.assertTrue(wait_for(lambda: store.view("light.desk")[2] == "1", timeout=8))

    def test_auto_layout_without_panel_config(self):
        self.start(config={})
        lines = parse_lines(self.srv.get("/v1/panel")[1])
        self.assertEqual([l[1] for l in lines if l[0] == "E"], ["climate.office", "light.desk"])

    def test_bad_token_is_never_logged(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.start(token="wrong-secret-value")
            time.sleep(0.5)
        self.assertNotIn("wrong-secret-value", err.getvalue() + self.log.getvalue())
        self.assertEqual(self.srv.get("/v1/toggle/light.desk", "POST")[0], 502)


class CommandLineTest(unittest.TestCase):
    def test_refuses_to_start_without_url_and_token(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            with unittest.mock.patch.dict("os.environ", {}, clear=True):
                gw.main(["--bind", "127.0.0.1", "--port", "0"])


import unittest.mock  # noqa: E402

if __name__ == "__main__":
    unittest.main()

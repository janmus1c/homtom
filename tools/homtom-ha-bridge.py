#!/usr/bin/env python3
"""Poll the homtom status endpoint and POST it to the Home Assistant webhook (automation "homtom telemetry in").

usage: homtom-ha-bridge.py [interval_s]          poll and post every interval_s seconds (0 = once)
       homtom-ha-bridge.py --event TYPE [X Y]    post one event (touch tap, long_press, swipe_*, or button press) and exit
       homtom-ha-bridge.py --print-discovery     print the MQTT device-discovery payload for homeassistant/device/homtom/config

The webhook id is read from .runtime/ha_webhook and the base URL from the required HA_URL environment variable.
HA stores the payload in the homtom helpers (fallback) and republishes it to the MQTT topics homtom/state, homtom/gps
and homtom/event, which feed the MQTT device "homtom" (docs/HA_LINK.md). Every state key is always sent, null when unknown.
Load, memory and Bluetooth come from the status JSON when homtom-statusd reports them (cpu_load present), as does the
last touch (last_touch); otherwise HOMTOM_EXTRAS_EVERY=N (seconds, default 0 = off) reads load, memory and Bluetooth
links over tools/homtom-sh every N seconds, which opens one root-shell session on the device per read."""
import json, os, re, subprocess, sys, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEV = os.environ.get("HOMTOM_URL", "http://192.168.1.10:8080/")
EXTRAS_EVERY = int(os.environ.get("HOMTOM_EXTRAS_EVERY", "0"))
EVENT_TYPES = ["tap", "long_press", "swipe_left", "swipe_right", "swipe_up", "swipe_down", "button_press", "button_long_press"]
EXPIRE_S = 150

last_up = None
boot_ts = None
extras = {"load_1m": None, "mem_free_kb": None, "mem_total_kb": None, "bt_up": None, "bt_links": None}
extras_at = 0.0


def homtom_sh(cmd, wait):
    return subprocess.run([os.path.join(ROOT, "tools", "homtom-sh"), cmd, str(wait)],
                          capture_output=True, text=True, timeout=40).stdout


def sync_clock():
    """The device has no RTC battery time: set its clock from the Mac (UTC) after each boot."""
    stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())
    homtom_sh(f'date -s "{stamp}" > /dev/null 2>&1', 5)


def read_extras():
    out = homtom_sh("cat /proc/loadavg; grep -E '^(MemTotal|MemAvailable|MemFree):' /proc/meminfo; "
                    "echo bt: $(ls /sys/class/bluetooth 2>/dev/null)", 3)
    m = re.search(r"^(\d+\.\d+) \d+\.\d+ \d+\.\d+ ", out, re.M)
    mem = dict((k, int(v)) for k, v in re.findall(r"^(MemTotal|MemAvailable|MemFree):\s+(\d+) kB", out, re.M))
    bt = re.search(r"^bt:(.*)$", out, re.M)
    if not m or "MemTotal" not in mem or not bt:
        raise ValueError("extras: unexpected output")
    names = bt.group(1).split()
    return {"load_1m": float(m.group(1)), "mem_free_kb": mem.get("MemAvailable", mem.get("MemFree")),
            "mem_total_kb": mem["MemTotal"], "bt_up": int("hci0" in names),
            "bt_links": sum(1 for n in names if re.fullmatch(r"hci0:\d+", n))}


def device_extras(st):
    """Load, memory and Bluetooth from a homtom-statusd that reports them; None for an older daemon."""
    if "cpu_load" not in st:
        return None
    load = st.get("cpu_load")
    return {"load_1m": None if load is None else load / 100, "mem_free_kb": st.get("mem_free_kb"),
            "mem_total_kb": st.get("mem_total_kb"), "bt_up": st.get("bt_up"), "bt_links": st.get("bt_links")}


def touch_fields(st):
    t = st.get("last_touch") if isinstance(st.get("last_touch"), dict) else {}
    return {"touch_type": t.get("type"), "touch_x": t.get("x"), "touch_y": t.get("y"), "touch_age_s": t.get("age_s")}


def nmea_fields(sentence):
    s = (sentence or "").strip()
    if not s.startswith("$") or "*" not in s:
        return None
    body, _, ck = s[1:].partition("*")
    x = 0
    for c in body:
        x ^= ord(c)
    if ck[:2].upper() != f"{x:02X}":
        return None
    return body.split(",")


def nmea_deg(v, hemi):
    if not v or "." not in v:
        return None
    dot = v.index(".")
    d = float(v[:dot - 2]) + float(v[dot - 2:]) / 60
    return round(-d if hemi in ("S", "W") else d, 6)


def parse_gps(gga, rmc):
    g = {"gps_fix": 0, "gps_sats": None, "gps_hdop": None, "gps_alt_m": None, "gps_speed_kmh": None,
         "gps_lat": None, "gps_lon": None, "gps_accuracy_m": None}
    f = nmea_fields(gga)
    if f and f[0].endswith("GGA") and len(f) > 9:
        g["gps_sats"] = int(f[7]) if f[7].isdigit() else None
        g["gps_hdop"] = float(f[8]) if f[8] else None
        if f[6].isdigit() and int(f[6]) > 0:
            g["gps_fix"] = 1
            g["gps_lat"], g["gps_lon"] = nmea_deg(f[2], f[3]), nmea_deg(f[4], f[5])
            g["gps_alt_m"] = float(f[9]) if f[9] else None
            g["gps_accuracy_m"] = round(5 * g["gps_hdop"]) if g["gps_hdop"] else 50
    r = nmea_fields(rmc)
    if r and r[0].endswith("RMC") and len(r) > 7 and r[2] == "A" and r[7]:
        g["gps_speed_kmh"] = round(float(r[7]) * 1.852, 1)
    if g["gps_lat"] is None or g["gps_lon"] is None:
        g.update(gps_fix=0, gps_lat=None, gps_lon=None, gps_accuracy_m=None)
    return g


def post(payload):
    ha = os.environ["HA_URL"]
    hook = open(os.path.join(ROOT, ".runtime", "ha_webhook")).read().strip()
    req = urllib.request.Request(f"{ha}/api/webhook/{hook}", json.dumps(payload).encode(),
                                 {"Content-Type": "application/json", "User-Agent": "homtom-bridge/2"})
    return urllib.request.urlopen(req, timeout=10).status


def once():
    global last_up, boot_ts, extras, extras_at
    st = json.load(urllib.request.urlopen(DEV, timeout=8))
    up = st.get("uptime_s")
    if up is not None and (last_up is None or up < last_up):
        sync_clock()
    last_up = up
    if any(st.get(k) is None for k in ("battery_raw", "battery_pct_est", "battery_mv_est", "light_raw")) or not st.get("battery_raw"):
        raise ValueError("incomplete reading, not posted")
    if up is not None:
        b = int(time.time()) - int(up)
        if boot_ts is None or abs(b - boot_ts) > 90:
            boot_ts = b
    dev_extras = device_extras(st)
    if dev_extras is not None:
        extras = dev_extras
    elif EXTRAS_EVERY > 0 and time.time() - extras_at >= EXTRAS_EVERY:
        extras_at = time.time()
        try:
            extras = read_extras()
        except Exception as e:
            extras = dict.fromkeys(extras)
            print(time.strftime("%H:%M:%S"), "extras error", e, flush=True)
    payload = {"kind": "state"}
    payload.update({k: st.get(k) for k in ("battery_raw", "battery_ref_raw", "battery_pct_est", "battery_mv_est", "light_raw",
                                           "charging", "external_power", "power_key", "uptime_s", "gps_age_s")})
    payload["boot_time"] = time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime(boot_ts)) if boot_ts is not None else None
    payload.update(parse_gps(st.get("gga"), st.get("rmc")))
    payload.update(extras)
    payload.update(touch_fields(st))
    return post(payload)


def bin_tpl(key):
    return "{% set v = value_json.get('" + key + "') %}{{ 'None' if v is none else ('ON' if v else 'OFF') }}"


def num_tpl(key):
    return "{{ value_json.get('" + key + "') }}"


def discovery():
    def s(oid, name, key, **kw):
        c = {"p": "sensor", "name": name, "unique_id": f"homtom_{oid}", "default_entity_id": f"sensor.homtom_{oid}",
             "state_topic": "homtom/state", "value_template": num_tpl(key), "expire_after": EXPIRE_S}
        c.update(kw)
        return oid, c

    def b(oid, name, key, **kw):
        c = {"p": "binary_sensor", "name": name, "unique_id": f"homtom_{oid}", "default_entity_id": f"binary_sensor.homtom_{oid}",
             "state_topic": "homtom/state", "value_template": bin_tpl(key), "expire_after": EXPIRE_S}
        c.update(kw)
        return oid, c

    diag = {"entity_category": "diagnostic"}
    cmps = dict([
        s("battery_level", "Battery", "battery_pct_est", device_class="battery", unit_of_measurement="%", state_class="measurement"),
        s("battery_voltage", "Battery voltage", "battery_mv_est", device_class="voltage", unit_of_measurement="mV",
          state_class="measurement"),
        s("battery_adc", "Battery ADC", "battery_raw", state_class="measurement", icon="mdi:sine-wave", **diag),
        s("battery_ref_adc", "Battery reference ADC", "battery_ref_raw", state_class="measurement", icon="mdi:sine-wave", **diag),
        b("charging", "Charging", "charging", device_class="battery_charging"),
        b("external_power", "External power", "external_power", device_class="power"),
        s("light_level", "Light level (raw)", "light_raw", state_class="measurement", icon="mdi:brightness-5"),
        b("power_key", "Power key", "power_key", icon="mdi:power"),
        s("uptime", "Uptime", "uptime_s", device_class="duration", unit_of_measurement="s", **diag),
        s("last_boot", "Last boot", "boot_time", device_class="timestamp", **diag),
        b("gps_fix", "GPS fix", "gps_fix", icon="mdi:crosshairs-gps"),
        s("gps_satellites", "GPS satellites", "gps_sats", state_class="measurement", icon="mdi:satellite-variant"),
        s("gps_hdop", "GPS HDOP", "gps_hdop", state_class="measurement", icon="mdi:crosshairs-question", **diag),
        s("gps_altitude", "GPS altitude", "gps_alt_m", device_class="distance", unit_of_measurement="m", **diag),
        s("gps_speed", "GPS speed", "gps_speed_kmh", device_class="speed", unit_of_measurement="km/h"),
        s("gps_age", "GPS fix age", "gps_age_s", device_class="duration", unit_of_measurement="s", **diag),
        s("cpu_load", "CPU load (1 min)", "load_1m", state_class="measurement", icon="mdi:cpu-32-bit", **diag),
        s("memory_free", "Memory available", "mem_free_kb", device_class="data_size", unit_of_measurement="kB",
          state_class="measurement", **diag),
        b("bluetooth", "Bluetooth adapter", "bt_up", device_class="running", icon="mdi:bluetooth", **diag),
        b("bluetooth_link", "Bluetooth link", "bt_links", device_class="connectivity", icon="mdi:bluetooth-connect"),
        s("bluetooth_links", "Bluetooth connections", "bt_links", state_class="measurement", icon="mdi:bluetooth-connect", **diag),
        s("last_touch", "Last touch", "touch_type", icon="mdi:gesture-tap", **diag),
        s("last_touch_age", "Last touch age", "touch_age_s", device_class="duration", unit_of_measurement="s", **diag),
    ])
    cmps["location"] = {"p": "device_tracker", "name": "Location", "unique_id": "homtom_location",
                        "default_entity_id": "device_tracker.homtom", "json_attributes_topic": "homtom/gps",
                        "source_type": "gps", "icon": "mdi:map-marker"}
    cmps["touch"] = {"p": "event", "name": "Touch", "unique_id": "homtom_touch", "default_entity_id": "event.homtom_touch",
                     "state_topic": "homtom/event", "event_types": EVENT_TYPES, "icon": "mdi:gesture-tap"}
    return {"dev": {"ids": ["homtom"], "name": "homtom", "mf": "TomTom", "mdl": "GO 630", "hw": "S3C2443",
                    "sw": "homtom (Linux 6.1)"},
            "o": {"name": "homtom-ha-bridge", "sw": "2"}, "qos": 0, "cmps": cmps}


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--print-discovery"]:
        print(json.dumps(discovery(), separators=(",", ":")))
        sys.exit(0)
    if args[:1] == ["--event"]:
        if len(args) < 2 or args[1] not in EVENT_TYPES:
            sys.exit(f"event type must be one of {EVENT_TYPES}")
        ev = {"kind": "event", "event_type": args[1], "ts": int(time.time())}
        if len(args) >= 4:
            ev.update(x=int(args[2]), y=int(args[3]))
        print("posted", post(ev))
        sys.exit(0)
    every = int(args[0]) if args else 30
    while True:
        try:
            print(time.strftime("%H:%M:%S"), "posted", once(), flush=True)
        except Exception as e:
            print(time.strftime("%H:%M:%S"), "error", e, flush=True)
        if every <= 0:
            break
        time.sleep(every)

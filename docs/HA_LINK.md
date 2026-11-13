# Home Assistant link (USB network stage)

The device serves one JSON document on `http://192.168.1.10:8080/` (userland/homtom-status): uptime, battery raw ADC (battery and reference), charging, external power, power key, light raw, latest GPS GGA/RMC sentences. Over the USB-gadget network the Mac is the gateway; Bluetooth replaces this link later (docs/ROADMAP.md phase 7).

Home Assistant REST sensor (put in configuration.yaml when HA can reach the device):

    sensor:
      - platform: rest
        name: homtom
        resource: http://192.168.1.10:8080/
        json_attributes: [uptime_s, battery_raw, battery_ref_raw, charging, external_power, power_key, light_raw, gga, rmc]
        value_template: "{{ value_json.uptime_s }}"
        scan_interval: 30

`tools/homtom-poll.py` records the same document to `.runtime/status.jsonl` as a local stand-in. Battery percent needs calibration (stock: about 0x319 raw at 3.5 V against a 3.3 V AIN4 reference, scaled by `bat * refraw / ref`); raw values are published until that is calibrated.

## Working link (2026-11-07): Mac bridge to an HA webhook
- Device -> Mac: `http://192.168.1.10:8080/` (homtom-status JSON) over the USB-ECM link.
- Mac -> HA: `tools/homtom-ha-bridge.py [interval_s]` POSTs battery_raw, battery_pct_est, light_raw, charging, external_power, uptime_s (HA_URL is a required environment variable; incomplete readings are not posted) to a webhook trigger of the automation `automation.homtom_telemetry_in`. The webhook id is a secret kept in `.runtime/ha_webhook` (not in git). The proxy in front of HA returns 403 for Python's default User-Agent; the bridge sets its own.
- HA side (created through the HA MCP): helpers `input_number.homtom_battery_raw`, `input_number.homtom_battery_percent_est`, `input_number.homtom_light_raw`, `input_boolean.homtom_charging`, `input_boolean.homtom_external_power`, `input_datetime.homtom_last_seen`, and the automation above. To remove: delete those seven objects.
- Verified: a POST of the live status changed `input_number.homtom_battery_raw` to the posted value.
- The webhook is reachable from the internet, so its id is a bearer secret (kept in .runtime/ha_webhook, never committed).
- Still open: battery percentage calibration (the percent is an estimate) and the Bluetooth link. GPS, touch events and availability now exist as entities of the MQTT device (section below).
- 2026-11-07 14:15: added `input_number.homtom_battery_mv_est` (mV) to the webhook automation and the bridge payload; verified (4223). Dashboard, `binary_sensor.homtom_online`, `sensor.homtom_battery` and `automation.homtom_offline_alert` are documented in docs/HA_DASHBOARD.md.

## homtom as a Home Assistant device (MQTT discovery, 2026-11-07 14:35)

### Route chosen and why

| Route | Verdict |
|---|---|
| MQTT discovery | Chosen. HA has the MQTT integration on the Mosquitto broker app (loaded). HA's own `mqtt.publish` action can write both the retained discovery message and the state, so the Mac needs no broker credentials and the internet-facing webhook stays the only transport. Everything was created through the HA MCP; no UI step is required. |
| Webhook plus template entities | Template helpers can be attached to a device, but only to an existing one; they cannot create a device. Kept only for `binary_sensor.homtom_online`. |
| Custom integration (`custom_components/homtom`) | Would work, but the MCP has no file write into `config/` for Python and it needs an HA restart. Not needed. |
| Bridge publishes to MQTT directly | Possible later (a broker user and paho-mqtt on the Mac). Not needed while the webhook carries everything. |

### Data path

    device statusd JSON --(USB, poll 30 s)--> tools/homtom-ha-bridge.py --(HTTPS POST)--> webhook
      --> automation.homtom_telemetry_in
            kind == "event":  mqtt.publish homtom/event, stop
            otherwise:        set the seven input_* helpers (unchanged fallback)
                              mqtt.publish homtom/state (not retained)
                              mqtt.publish homtom/gps (retained) only when gps_fix == 1
      --> MQTT device "homtom" (discovery topic homeassistant/device/homtom/config, retained)

The automation runs in `queued` mode, max 10. Its conditions use templates because a webhook payload has no native condition.

### Device and entities

Device id `e9c85be9c33aafe2f1daa62878e7cdd0`, name `homtom`, MQTT identifier `homtom`, manufacturer TomTom, model GO 630. 23 MQTT entities plus the linked template `binary_sensor.homtom_online`.

| Entity | Payload key | Notes |
|---|---|---|
| `sensor.homtom_battery_level` | `battery_pct_est` | %, device class battery. Still an estimate |
| `sensor.homtom_battery_voltage` | `battery_mv_est` | mV, estimate |
| `sensor.homtom_battery_adc`, `sensor.homtom_battery_ref_adc` | `battery_raw`, `battery_ref_raw` | diagnostic |
| `binary_sensor.homtom_charging` | `charging` | battery_charging |
| `binary_sensor.homtom_external_power` | `external_power` | power |
| `sensor.homtom_light_level` | `light_raw` | raw ADC, not lux |
| `binary_sensor.homtom_power_key` | `power_key` | sampled at the poll, so short presses are missed |
| `sensor.homtom_uptime`, `sensor.homtom_last_boot` | `uptime_s`, `boot_time` | diagnostic; `boot_time` is computed by the bridge and kept stable within 90 s |
| `binary_sensor.homtom_gps_fix`, `sensor.homtom_gps_satellites`, `sensor.homtom_gps_hdop`, `sensor.homtom_gps_altitude`, `sensor.homtom_gps_speed`, `sensor.homtom_gps_age` | `gps_fix`, `gps_sats`, `gps_hdop`, `gps_alt_m`, `gps_speed_kmh`, `gps_age_s` | parsed from GGA/RMC on the Mac, checksum verified |
| `device_tracker.homtom` | `homtom/gps`: latitude, longitude, gps_accuracy (about 5 m x HDOP) | last fix retained, so it survives an HA restart |
| `event.homtom_touch` | `event_type` (tap, long_press, swipe_left/right/up/down, button_press, button_long_press), optional `x`, `y` | one POST per event (`homtom-ha-bridge.py --event TYPE [X Y]`) |
| `sensor.homtom_cpu_load`, `sensor.homtom_memory_free`, `binary_sensor.homtom_bluetooth`, `binary_sensor.homtom_bluetooth_link`, `sensor.homtom_bluetooth_links` | `load_1m`, `mem_free_kb`, `bt_up`, `bt_links` | only with `HOMTOM_EXTRAS_EVERY` set (see below) |
| `binary_sensor.homtom_online` | template helper on `input_datetime.homtom_last_seen` | linked to the device on 2026-11-07; entity id unchanged |

All state sensors have `expire_after: 150`: with no post for 150 s they turn `unavailable`, which is the device's own offline signal. A key that is absent or null reads `unknown`. The discovery JSON is generated by `tools/homtom-ha-bridge.py --print-discovery`; to change entities, edit `discovery()` and publish the output again, retained, to `homeassistant/device/homtom/config` (HA: Tools > Actions > `mqtt.publish`, or the MCP).

### Verified (2026-11-07 14:33, bridge still running the previous code)
- The device and all 23 entities were created with the intended ids. After the next post: battery 100 %, 4223 mV, ADC 655, light 4, uptime 932 s, charging on, external power on; keys the old bridge does not send read `unknown`.
- `homtom-ha-bridge.py --event tap 120 80` set `event.homtom_touch` (event_type tap, x 120, y 80) and left `input_number.homtom_battery_raw` unchanged, so event posts never reach the helpers.
- Not verified: `device_tracker.homtom` (no GPS fix indoors), GPS sensors with real values, and the extras (load, memory, Bluetooth), which need the bridge restarted with the new code.

### Bridge (tools/homtom-ha-bridge.py, version 2)
- Sends every key on every post, null when unknown, plus `kind: "state"`. Adds parsed GPS fields, `boot_time`, and the `battery_ref_raw`, `power_key` and `gps_age_s` fields the device already reports. The input_* helper path is unchanged, so the old dashboard keeps working.
- `--event TYPE [X Y]` posts one event; `--print-discovery` prints the discovery payload.
- `HOMTOM_EXTRAS_EVERY=N` (default 0, off): every N seconds read `/proc/loadavg`, `/proc/meminfo` and `/sys/class/bluetooth` (hci0 present, number of `hci0:<handle>` ACL links) over `tools/homtom-sh`. This opens a root-shell session on the device each time, so keep N at 300 or more. Untested against the device. The cleaner fix is to add these fields to homtom-statusd (no forks) and drop the shell read.
- The running process keeps the old code until it is restarted with `HA_URL` set.

### System and touch fields from homtom-statusd (2026-11-07, built, not yet in the image)
- `tools/devtools/homtom-statusd.new` (source workspace/statusd) adds `cpu_load`, `loadavg_x100`, `mem_free_kb`, `mem_total_kb`, `bt_up`, `bt_links` and `last_touch` to the JSON (workspace/statusd/README.md). Tested on the device as a passive second instance on port 8081; the live daemon on 8080 was not touched.
- The bridge uses these fields whenever the JSON has `cpu_load` (no shell session, `HOMTOM_EXTRAS_EVERY` is then ignored) and forwards `last_touch` as `touch_type`, `touch_x`, `touch_y`, `touch_age_s`. `discovery()` gained `sensor.homtom_last_touch` and `sensor.homtom_last_touch_age`; they exist in HA only after the discovery payload is published again, and values arrive only after the bridge is restarted and the new daemon is in the image.
- Proposed change to `userland/homtom-ui-start` (not applied): insert one line inside the `while read x y e` loop, before `case $e in`:

      [ "$e" = move ] || echo "$x $y $e" > /tmp/touch.last

  `[` and `echo` are ash builtins, so no fork per event; only down and up are written, not every move. A reader can catch the file between truncate and write; statusd then reports `last_touch: null` for that request.

### Bluetooth presence
- HA's Bluetooth integration (one USB adapter here) handles Bluetooth Low Energy only: it listens to LE advertisements. The CSR BlueCore4 in the GO 630 is Bluetooth 1.2 (LMP 3) Classic and has no LE radio, so HA cannot see homtom. Checked: no HA device or entity related to the homtom radio exists.
- The legacy `bluetooth_tracker` platform (Classic inquiry, YAML only, PyBluez) is not a fit for this install and is not recommended.
- Presence of homtom itself: use `binary_sensor.homtom_online` (any post within 120 s) and the 150 s `expire_after` on the MQTT entities. When the Mac's RFCOMM client (`tools/host/homtom-bt-rfcomm.swift`) carries the status instead of USB, post that JSON to the same webhook; the entities stay the same.
- Presence of phones near homtom (homtom as the sensor, PERIPHERALS U5): add a periodic Remote Name Request to known, paired addresses in homtom-btd and expose a count or per-peer pseudonymous flag in the status JSON. The bridge then forwards it like `bt_links`. No MAC addresses in HA attributes or in the repo.

### Removal
Publish an empty retained payload to `homeassistant/device/homtom/config` (removes the device and its 23 entities), publish an empty retained payload to `homtom/gps`, and remove the two `mqtt.publish` steps and the event branch from `automation.homtom_telemetry_in`. To unlink the online helper: options of the "homtom online" template helper, clear Device.

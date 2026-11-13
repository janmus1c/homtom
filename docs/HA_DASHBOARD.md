# Home Assistant objects for homtom

Created 2026-11-07 through the HA MCP, next to the telemetry objects listed in `docs/HA_LINK.md`. Every id starts with `homtom`. Nothing else in HA was changed, and `automation.homtom_telemetry_in` is untouched.

## Objects created

| Id | Kind | Purpose | Remove |
|---|---|---|---|
| `binary_sensor.homtom_online` | Template helper (config flow, entry `01M6TW19WCWK04EAJ8VX1STP7E`), device class connectivity | On while `input_datetime.homtom_last_seen` is under 120 s old. The template uses `now()`, so HA re-evaluates it every minute. | Settings > Devices & services > Helpers > "homtom online" > Delete, or `ha_remove_helpers_integrations` with the entry id |
| `sensor.homtom_battery` | Template helper (entry `01M6TW1AM6H0QZ3N45WQEK7RDR`), device class battery, unit %, state class measurement | Mirrors `input_number.homtom_battery_percent_est`. The percent is still an estimate. | Same as above, "homtom battery" |
| `automation.homtom_offline_alert` | Automation (config id `1794050672111`) | Raises persistent notification `homtom_offline` after `binary_sensor.homtom_online` has been off for 10 minutes. Dismisses it when the sensor turns on. No mobile push. | Settings > Automations > delete, or `ha_config_set_automation` followed by the automation delete tool |
| Dashboard `homtom` (url_path `homtom-dash`, id `homtom_dash`) | Storage-mode Lovelace dashboard | Battery gauge, charging and external power tiles, light tile with trend, online and last-seen tiles, 12 h battery history. Accent colour `#18BCF2` (Home blue, `brand/README.md`). | Settings > Dashboards > homtom > Delete, or `ha_config_delete_dashboard` with `homtom-dash` |
| Persistent notification `homtom_offline` | Created on demand by the automation | Offline notice | Dismiss in the UI (the automation also dismisses it) |

To remove everything created here: delete the dashboard, the automation, and the two template helpers. The seven telemetry objects from `docs/HA_LINK.md` are separate.

## Verification (2026-11-07 12:24)

`binary_sensor.homtom_online` read back `on` with `last_seen` 8 s old. `sensor.homtom_battery` read `100.0 %` with device class battery. `automation.homtom_offline_alert` read `on`. The dashboard appears in the dashboard list with title `homtom`. The offline path (10 minutes off, notification, dismissal) was not exercised, because that would need the bridge stopped for 10 minutes.

## Since then (2026-11-07 14:35)

- The voltage estimate exists twice: `input_number.homtom_battery_mv_est` (helper, from the webhook) and `sensor.homtom_battery_voltage` (MQTT device). The planned template `sensor.homtom_battery_voltage` was not created; the MQTT entity took that id.
- The entities planned in the old "future entities" table now exist on the MQTT device `homtom` (device id `e9c85be9c33aafe2f1daa62878e7cdd0`): GPS fix, satellites, HDOP, altitude, speed, fix age, `device_tracker.homtom`, `event.homtom_touch`, Bluetooth adapter and link, CPU load, memory, uptime, last boot, power key. Ids, payload keys and the data path are in `docs/HA_LINK.md`, section "homtom as a Home Assistant device".
- `binary_sensor.homtom_online` is now attached to that device. Its entity id, template and the offline alert are unchanged.
- Superseded by the section below (dashboard rebuilt). Earlier suggestion: point the cards at the device entities (`sensor.homtom_battery_level`, `sensor.homtom_battery_voltage`, `binary_sensor.homtom_charging`, `binary_sensor.homtom_external_power`, `sensor.homtom_light_level`), add a map card for `device_tracker.homtom`, a logbook card for `event.homtom_touch`, and a tile row for uptime, CPU load, memory and Bluetooth link. After that the seven input_* helpers and `sensor.homtom_battery` could go, together with the helper steps in `automation.homtom_telemetry_in`; until then they are the fallback.

## Dashboard rebuilt on the device entities (2026-11-07)

Dashboard `homtom-dash` now reads only entities of the MQTT device `homtom` (plus `binary_sensor.homtom_online`). No card uses `input_*` helpers or `sensor.homtom_battery`; those are still in HA as the fallback, as is `automation.homtom_telemetry_in`. All references are entity ids (no device_id). Sections views, tile cards as the default, accent `#18BCF2`, graph and map cards full width with fixed rows so they read on a phone.

| View (path) | Card | Entity |
|---|---|---|
| Overview (`homtom`) | View badges: online, charging, external power | `binary_sensor.homtom_online`, `binary_sensor.homtom_charging`, `binary_sensor.homtom_external_power` |
| | Status: online tile, uptime tile, last boot tile, online history | `binary_sensor.homtom_online`, `sensor.homtom_uptime`, `sensor.homtom_last_boot` |
| | Battery: gauge (red under 20, yellow under 40), charging and external power tiles, voltage tile, 24 h history graph with charging overlay | `sensor.homtom_battery_level`, `binary_sensor.homtom_charging`, `binary_sensor.homtom_external_power`, `sensor.homtom_battery_voltage` |
| | Light: tile and 12 h graph (raw ADC, not lux) | `sensor.homtom_light_level` |
| Location (`location`) | Badge: GPS fix | `binary_sensor.homtom_gps_fix` |
| | Map card, 24 h trail | `device_tracker.homtom` |
| | GPS tiles: fix, satellites, HDOP, altitude, speed, fix age; satellites 12 h graph | `binary_sensor.homtom_gps_fix`, `sensor.homtom_gps_satellites`, `sensor.homtom_gps_hdop`, `sensor.homtom_gps_altitude`, `sensor.homtom_gps_speed`, `sensor.homtom_gps_age` |
| Device (`device`) | Touch events logbook, 48 h | `event.homtom_touch` |
| | Bluetooth: adapter, link, link count tiles | `binary_sensor.homtom_bluetooth`, `binary_sensor.homtom_bluetooth_link`, `sensor.homtom_bluetooth_links` |
| | System: CPU load and memory free tiles, 12 h graph each | `sensor.homtom_cpu_load`, `sensor.homtom_memory_free` |

### Cards that are empty or unknown right now
- Map, GPS altitude, speed, HDOP, fix age: no GPS fix yet (fix off, 0 satellites). They fill when the device has a fix; `device_tracker.homtom` is `unknown` until the first retained `homtom/gps` post.
- CPU load, memory free, Bluetooth adapter, link and link count: read `unknown` because the running bridge is not sending the extras (`HOMTOM_EXTRAS_EVERY` unset or bridge not restarted). The durable fix is device-side: add `load_1m`, `mem_free_kb`, `bt_up`, `bt_links` to homtom-statusd so no root-shell read is needed.
- Touch logbook: shows only events posted so far (one test tap). Real touch events wait on the device-side touch reporting calling `homtom-ha-bridge.py --event`.
- Battery percent and voltage are still estimates; light is a raw ADC value.

## Payload rules for new fields

Every key is sent on every state post, null when unknown (the MQTT entities then read `unknown`). Events are separate posts with `kind: "event"` and an `event_type` from the list in `tools/homtom-ha-bridge.py`; the automation forwards them to `homtom/event` and stops, so they never touch the helpers. A new field needs: the key in the bridge payload, an entry in `discovery()`, and the discovery message published again. Bluetooth peers are reported as counts or pseudonymous labels, never MAC addresses.

The webhook id is a bearer secret kept in `.runtime/ha_webhook`. Do not copy it into docs or commits.

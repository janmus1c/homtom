# Home Assistant areas on the homtom screen: Mac side

Status, 2026-11-07: the Mac daemon, the generator and the Home Assistant objects exist and were tested (section "Verified"). The device GUI that reads `/tmp/ha.states` and writes `event=ha` lines is not on the device yet, so nothing displays the states. The device side of the contract is described in `docs/HA_UI.md` (GUI side); this file covers only the Mac and Home Assistant side.

## Data path

    Home Assistant --(read path, every ~2 s)--> tools/homtom-ha-ui.py --(FTP: /tmp/ha.states.new, RNFR/RNTO)--> /tmp/ha.states
    /tmp/ui.events --(FTP get, every ~1 s)--> tools/homtom-ha-ui.py --(HTTPS POST webhook)--> automation.homtom_ui_actions
                                                                                               --> allow-listed action

| File | Role | In git |
|---|---|---|
| `tools/homtom-ha-ui.py` | Daemon: pushes states, turns UI events into actions | yes |
| `tools/homtom-ha-ui-gen.py` | Generates `ui.conf`: area pages merged in front of the base file | yes |
| `docs/ha-ui.example.json` | Example map with generic entity ids | yes |
| `.runtime/ha-ui.json` | The user's map: slots, areas, read path | no |
| `.runtime/ha-ui.auto.json` | Display-only slots the generator made for areas without a map | no |
| `.runtime/ha_ui_webhook` | Webhook id of `automation.homtom_ui_actions` (a bearer secret) | no |
| `.runtime/ha_token` | Long-lived access token, only for the `rest` read path | no |
| `.runtime/ha_ui_push_secret` | URL secret, only for the `push` read path | no |
| `.runtime/ha-ui.disable` | Kill switch: while it exists every action is refused | no |
| `.runtime/ha-ui.log` | Log: every action with slot, op and result; no URL, id or token | no |
| `.runtime/ui.conf.generated` | Generator output | no |
| `userland/ui.conf.default` | Base file the generator appends after the area pages | yes |

`HA_URL` comes from the environment and is never written to a file. Both scripts run with `python3 -I` and use the standard library only.

## Interface contract (with the GUI)

**States**, Mac to device: `/tmp/ha.states`, `key=value` lines, at most 4 KB, 64 keys, 80 characters a line, printable ASCII. Written atomically: uploaded as `/tmp/ha.states.new` and renamed with FTP `RNFR`/`RNTO` in the same session (fallback: one `mv` over `tools/homtom-sh`). Keys per slot (`[a-z0-9_]{1,15}`):

| Key | Slots | Value |
|---|---|---|
| `ha.SLOT.name` | all | display name (config `name`, else HA friendly name), 24 characters |
| `ha.SLOT.state` | all except numeric sensors | `on`, `off`, `unavailable`; climate: the HVAC mode; button: `ready`; text sensor: its text |
| `ha.SLOT.bri` | light, on | 1..100 |
| `ha.SLOT.ct` | light in colour-temperature mode | Kelvin |
| `ha.SLOT.hue`, `.sat` | light in a colour mode | 0..359, 0..100 |
| `ha.SLOT.val`, `.unit` | numeric sensor, climate (current temperature) | number (one decimal below 100), ASCII unit (`C`, `%`, `ppm`, `ug/m3`) |
| `ha.SLOT.target`, `.mode` | climate | target temperature `T.T`, HVAC mode |
| `ha.SLOT.age` | all | seconds since the state changed (button: since the last press) |

When the limits are reached, `.age` goes first, then `.name`, `.unit`, `.ct`, `.hue`, `.sat`; `.state`, `.val`, `.bri`, `.target` and `.mode` are always sent. The office map produces 52 keys, 1.4 KB. The file is uploaded when anything other than an age changed, and at least every 20 s.

**Actions**, device to Mac: lines appended to `/tmp/ui.events`:

    seq=N up=S event=ha name=SLOT value=OP[:ARG] page=P

| OP | Slot kind | Sent to HA |
|---|---|---|
| `toggle` | light | `light.toggle` |
| `set_bri:N` | light | `light.turn_on` `brightness_pct` 1..100; `N` = 0 sends `light.turn_off` |
| `set_ct:K` | light | `light.turn_on` `color_temp_kelvin`, clamped to the slot's `ct_min`..`ct_max` (HA clamps again to 2000..6500) |
| `set_hue_sat:H,S` | light | `light.turn_on` `hs_color`, H 0..359, S 0..100 |
| `press` | button | `button.press` |
| `set_target:T.T` | climate | `climate.set_temperature`, clamped to 10..26 and rounded to 0.5 on both sides |
| `mode:NAME` | climate | `climate.set_hvac_mode`, only names in the slot's `modes` (default `off`, `heat`) |

A slot's `ops` list can narrow these (the ambient bulb has no colour, so its slot allows only `toggle` and `set_bri`).

## Reading Home Assistant: token or token-free

Writing is token-free: the webhook automation (below). Reading was checked on 2026-11-07 against Home Assistant 2026.10.4:

| Path | Result | Evidence |
|---|---|---|
| Webhook response | Not possible | A POST to the new webhook returned `HTTP 200` with `content-length: 0`. A webhook trigger has no response body, and an automation cannot return data to its caller |
| REST without a token | Not possible | `GET /api/states/...` returned `401` |
| HA pushes states to the Mac | Possible, not set up | Needs a `rest_command` (YAML only, so a `configuration.yaml` edit) and an address of the Mac that Home Assistant can reach. HA is reached through a public proxy; whether it can reach the Mac directly was not checked |
| MQTT, HA publishes the states | Possible, not set up | The Mac would need broker credentials (also a secret) and a reachable broker port |
| REST with a long-lived token | Works | One `POST /api/template` per cycle renders all mapped states as compact JSON (the same template was rendered through the MCP and parsed by the daemon) |

Decision: **the read path is REST with a long-lived access token in `.runtime/ha_token`** (`"read": "rest"`, the default). There is no token-free read path that works without a configuration change on the HA host. The daemon also implements `"read": "push"`: it listens on `push_listen` and accepts the same JSON at `http://ADDRESS:PORT/<.runtime/ha_ui_push_secret>`, so the user can switch to it after setting up the HA side (see "Push read path"). `"read": "file"` reads a dump (`.runtime/ha-ui.dump.json`) for development; the current `.runtime/ha-ui.json` uses it because no token exists yet.

A long-lived token carries all rights of the user who created it, including calling any action. To limit that, create it as a dedicated non-admin user (steps below). Non-admin users can still read every state and call actions; Home Assistant has no read-only token.

## Steps for the user

### 1. Create the token (needed for live states)

1. Optional, recommended: in Home Assistant open **Settings > People > Users** (turn on **Advanced mode** in your profile first if the Users tab is missing), **Add user**, display name `homtom ui`, username `homtom-ui`, a long random password, leave **Administrator** off, **Create**. Log out and log in as `homtom-ui` for step 2.
2. Click the user name at the bottom of the sidebar (profile), open the **Security** tab, scroll to **Long-lived access tokens**, **Create token**, name `homtom-ui`, **OK**, and copy the token (it is shown once).
3. On the Mac, in the project directory:

       umask 077; pbpaste | tr -d '\n' > .runtime/ha_token

4. In `.runtime/ha-ui.json` set `"read": "rest"`.
5. Check: `HA_URL=https://... python3 -I tools/homtom-ha-ui.py --print-states` prints the key=value lines.

To revoke: the same **Security** tab, delete the token (or delete the `homtom-ui` user).

### 2. Run

    export HA_URL=https://...              # same value as for the telemetry bridge
    python3 -I tools/homtom-ha-ui.py --dry-run --once     # one state push, events read, actions only logged
    python3 -I tools/homtom-ha-ui.py                      # the loop; stop with Ctrl-C
    touch .runtime/ha-ui.disable                          # refuse all actions at once (states keep flowing)

`--no-device` reads Home Assistant without touching the device; `--action SLOT OP[:ARG]` runs one action through all the checks (for testing).

### 3. Generate and upload the config

    python3 -I tools/homtom-ha-ui-gen.py                  # merge with userland/ui.conf.default, via the token
    python3 -I tools/homtom-ha-ui-gen.py --from-dump .runtime/ha-ui.dump.json
    python3 -I tools/homtom-ha-ui-gen.py --merge-base PATH   # another base file
    python3 -I tools/homtom-ha-ui-gen.py --standalone     # old form: area pages only, own `ui` line
    python3 -I tools/homtom-ha-ui-gen.py --upload         # check, then replace INTERNAL:/homtom/ui.conf

The output is `.runtime/ui.conf.generated` (`--out FILE` overrides, `--config FILE` reads another map). The run prints the file's size, pages (visible and hidden), widgets and the parser limits, then the host parser's verdict. `--upload` refuses a file that exceeds a limit (exit 3) or that the host parser (`workspace/gui/out/preview`) rejects (exit 2, `--force` overrides the parser check, never the limits). **The upload replaces the whole `INTERNAL:/homtom/ui.conf`**: write `ui.conf.new`, rename in the same FTP session; the GUI hot-reloads within 2 s and keeps the last good config if it rejects the new one. To go back to the base alone, upload `userland/ui.conf.default` (or delete the file on the device).

Limits come from `workspace/gui/cfg.h` (read at run time): 8192 bytes, 200 characters a line, 12 pages (`CFG_PAGES`), 6 visible (`CFG_NAV`), 64 widgets. Area pages get the room the base leaves; if they do not fit, the last light detail pages go first, then the vent page, then area pages beyond the visible limit (each with a warning). The base's `row`, `on` and `var` lines count as the parser counts them: only `ui`, `page`, `var` and `on` are not widgets.

## Merge contract

The generated file is: generator comments, the base `ui` line (the only one), the base lines before its first `page` (`var`, `on key=...` bindings, comments), the generated area pages in area order (top level, swipe order; each followed by its hidden pages), then every base page, line for line, unchanged.

| Part | Rule |
|---|---|
| `ui` line | Taken from the base with all its keys. The generator changes only `start=` (the first area page; kept as the base had it when no area page is generated), and `nav=` / `title=` when `.runtime/ha-ui.json` sets `nav` / `ui_title` explicitly. It never writes a second `ui` line |
| Preamble | Base lines before its first `page` are kept in place, before the area pages |
| Area pages | First in the file, so they are the first visible pages and `start` points at one. Hidden pages (vent, light details) follow their area page |
| Base pages | Appended unchanged. A base with a hub page and hidden subpages (`hidden=1 parent=system`) therefore stays reachable through the power key bindings the base declares (`page:system`, `page:cycle`) |
| Ids | Generated page ids: the area id (max 15), `AREA_vent`, `AREA_l1..`. A base page with the same id makes the parser reject the file; the generator does not rename base pages |

Generated files carry no `ui` options of their own beyond that header, so changing the base (title, wrap, anim) changes the result at the next regeneration.

## Office page layout

Content area without bottom navigation: 464 x 216, a 4x3 grid of 110 x 66 px cells (8 px gaps). With tabs it is 464 x 160 and 110 x 48 cells; both are above the 40 px touch floor. One page holds at most the 8 cells below, so it stays calm:

    row 0   [ thermostat card, 2 cells ] [ all lights, compact lightctl, 2 cells: brightness slider, tap opens the lights ]
    row 1   [ Presence ] [ CO2 ] [ Temp ] [ Humidity ]
    row 2   [ Vent, 2 cells: tap = on/off, hold = Vent page ]                      (two cells free)

The individual lights live on the hidden pages `office_l1..` (swipe between them, Back returns). PM2.5 and any slot not named in the layout are not drawn; the generator prints them as a warning ("not on the page"). Choose in `.runtime/ha-ui.json`:

    "office_layout": ["thermo", "all", "presence", "co2", "temp", "hum", "pm25", "vent"]

The key is `AREA_layout` for any area. Entries, in display order: `thermo` (each controllable climate slot, 2 cells), `all` (the main light as compact `lightctl`, 2 cells, `detail=` the first light page), `lights` (every other light as an `entity` tile: tap toggles, hold opens its page), `vent` (below), a role (`presence`, `co2`, `temp`, `hum`, `pm25`), a slot id, or `rest` (everything not yet placed). Roles are guessed from the slot id and name (override with `"role": "co2"` in a slot). Default when the key is absent: `thermo, all, presence, co2, temp, hum, vent`. Four light tiles instead of the hero: `["lights", "thermo", "presence", "co2", "vent"]` (one page row of lights, the thermostat, then three items). Areas with only display-only slots (no map) keep the old reading-order packing.

## Vent

The three IR buttons are stateless: HA cannot know whether the vent is on, so nothing on the screen shows a state. The `ha.SLOT.state` of a button is `ready` and `ha.SLOT.age` counts from the last press. The Office page carries one tile:

    entity at=0,2 span=2,1 slot=SLOT_ONOFF label=Vent icon=ac tap=ha:press confirm=1 hold=page:office_vent

`icon=ac` is in the parser's icon table (`workspace/gui/cfg.c`); the generator checks the table and drops the icon when a renderer lacks it. The tile shows `ready`. A hold opens the hidden page `office_vent` (title Vent, `hidden=1 parent=office`) with three large `button` widgets (`tap=ha:press`, `slot=` the matching IR slot, 149 x 120 px):

    page id=office_vent title=Vent hidden=1 parent=office
    button x=8 y=72 w=149 h=120 label=On/Off slot=SLOT_ONOFF tap=ha:press confirm=1
    button x=165 y=72 w=149 h=120 label=High slot=SLOT_HIGH tap=ha:press
    button x=322 y=72 w=149 h=120 label=Low slot=SLOT_LOW tap=ha:press

Slots are found among the area's mapped button slots by role (`"role": "vent_onoff"`, `vent_high`, `vent_low`), else by words in the slot id or name: `pwr`/`power`/`on/off`, `high`, `low`. Missing buttons are left out of the page; with no on/off slot the tile only opens the page. `confirm=1` follows the slot's `confirm` in the map. The daemon accepts `press` only for mapped `button` slots (any other slot, an unmapped one or an auto slot is refused as `slot not mapped`), debounces a repeated press of one slot within 400 ms (an IR button must not fire twice from one tap), and logs `dry-run slot=... op=press (button press, no state change expected)` or `action slot=... op=press (...) result=http 200`.

## Generated ui.conf

The generator writes against the contract; the GUI side is `docs/HA_UI.md`. Example output for the generic example map (area pages, then the base):

    ui version=1 title=homtom start=office nav=both
    page id=office title=Office grid=4x3
    thermo at=0,0 span=2,1 slot=office_thermo label=Heating min=10 max=26 step=0.5 modes=off,heat
    lightctl at=2,0 span=2,1 slot=office_all label="All lights" ct_min=2000 ct_max=6500 color=1 detail=office_l1
    entity at=0,1 slot=office_presence label=Presence icon=person
    entity at=1,1 slot=office_co2 label=CO2 icon=co2 good_below=1000
    entity at=2,1 slot=office_temp label=Temp icon=thermometer
    entity at=3,1 slot=office_hum label=Humidity icon=drop
    entity at=0,2 span=2,1 slot=office_ac_pwr label=Vent icon=ac tap=ha:press confirm=1 hold=page:office_vent
    page id=office_vent title=Vent hidden=1 parent=office
    ...
    page id=office_l1 title=Ambient grid=1x1 hidden=1 group=office_lights parent=office
    lightctl at=0,0 slot=office_amb label=Ambient color=0

| Item | Meaning |
|---|---|
| `lightctl`, `thermo`, `entity`, `button slot=` | Contract widgets bound to `slot=` |
| `ct_min`, `ct_max`, `color=0/1`, `dim=0` | What the light supports; no `ct_` keys = no colour temperature |
| `detail=PAGE` | Tap on the light control opens that page |
| `tap=ha:press`, `tap=ha:toggle`, `hold=page:ID` | Which op a tap sends and what a hold opens |
| `good_below=N` | Value colour turns `ok` below N |
| `hidden=1 group=NAME parent=PAGE` | Detail pages: not in the tab bar or dots; same `group` = swipe neighbours; Back returns to `parent` |

## Safety on the Mac

- Actions only for slots in `.runtime/ha-ui.json`; slots the generator added (`ha-ui.auto.json`) are display-only. Each op must be in the slot's kind and `ops`.
- Values are clamped as in the table above, before posting; the HA automation checks and clamps again.
- A repeated `toggle` of the same slot within 400 ms is ignored. Events arrive in batches of one FTP read per second and `up` has whole seconds, so two toggles of one slot in the same batch count as a repeat.
- In one batch only the last `set_*` of each slot and op is sent (a dragged slider sends one value).
- Rate limit: 3 actions a second with a burst of 6, and 60 a minute. Excess actions are refused and logged.
- An event whose `up` is more than 15 s behind the device uptime (status JSON) is refused as stale.
- Events written before the daemon started are skipped, never replayed.
- The kill switch `.runtime/ha-ui.disable` refuses every action.
- Every action is logged with slot, op and result (`http 200`, `refused ... result=...`, `dry-run`).

### Events and restarts

The daemon fetches the whole `/tmp/ui.events` (at most 16 KB) and remembers its first line and the number of lines done. New lines are those after that count. A different first line or a shorter file means a new file: device reboot (`/tmp` is RAM), GUI restart after a rotation, or a rotation. Those lines are read from the start, and the stale check drops old ones. `seq` is checked for gaps, and `seq` dropping to 1 within the file is logged as a GUI restart. At 12 KB the daemon renames the file to `/tmp/ui.events.taken` over FTP, reads it and deletes it (`"rotate_events": false` turns this off), because the GUI stops appending at 16 KB.

## Home Assistant objects

All created through the HA MCP on 2026-11-07. Nothing else in Home Assistant was changed.

| Id | Kind | Purpose | Remove |
|---|---|---|---|
| `automation.homtom_ui_actions` | Automation (config id `1794066412837`), webhook trigger, POST only, `queued` max 5 | Runs only the allow-listed actions. The lists of allowed lights, buttons and climate entities, and the allowed HVAC modes, are in its `variables`. Anything else ends in the default branch `stop: refused` | Settings > Automations > homtom ui actions > Delete |
| `input_text.homtom_ui_areas` | Helper, max 120, pattern `[a-z0-9_]` list | Page list for the generator, comma-separated area ids in order. Value now `office` | Settings > Devices & services > Helpers > homtom ui areas > Delete |

The webhook id is in `.runtime/ha_ui_webhook` only. To add an entity to the action allow-list, edit the automation's `variables` (Settings > Automations > homtom ui actions > Edit in YAML) and add the slot to `.runtime/ha-ui.json`; both sides must allow it.

Templates in the action targets and conditions are deliberate: a webhook payload has no native condition, and every templated target is guarded by an `entity in list` condition, so a target can only be one of the listed entities.

## Editing pages from Home Assistant

`input_text.homtom_ui_areas` holds the page order as area ids, e.g. `office,kitchen`. The generator reads it through the same read path (or from a dump) and makes one page per listed area. An area that has slots in `.runtime/ha-ui.json` gets those; any other area gets display-only slots (lights, climate, occupancy/presence/motion and CO2, temperature, humidity, PM2.5 sensors that are not unavailable), written to `.runtime/ha-ui.auto.json` so the daemon pushes their states. Removing an area id hides its page. Actions for a new area need both its slots in `ha-ui.json` and its entities in the automation's allow-list, so the HA UI alone cannot extend what the screen may control.

With `"auto_regen": true` the daemon notices a changed value (within one read cycle) and runs the generator; with `"auto_upload": true` as well, the generator uploads the result when the host parser accepts it. Both default to off. Tested with a dump containing `office,kitchen`: a Kitchen page with two display-only slots.

Not done: an `input_select` per area. A single text list keeps the order in one place and needs no helper per area.

## Push read path (optional, not set up)

For a setup without a token on the Mac:

1. Create `.runtime/ha_ui_push_secret` (`python3 -I -c "import secrets;print(secrets.token_hex(16))" > .runtime/ha_ui_push_secret`), set `"read": "push"` and `"push_listen": "0.0.0.0:8766"`.
2. In Home Assistant add a `rest_command` named `homtom_ui_push` (YAML, `configuration.yaml`; keep a copy, then **Tools > YAML > Check configuration** and reload) with `url: http://MAC-ADDRESS:8766/SECRET`, `method: post`, `content_type: application/json`, `payload: "{{ payload }}"`.
3. Create an automation `homtom ui states out`: triggers a state change of any mapped entity plus a `time_pattern` every 30 s; action `rest_command.homtom_ui_push` with `payload` set to the template printed by `python3 -I tools/homtom-ha-ui.py --print-template` (it contains the user's entity ids, so it stays out of the repo).

The daemon treats pushed states older than 60 s as unavailable. This path needs Home Assistant to reach the Mac; check that before choosing it.

## Verified (2026-11-07)

| Test | Result |
|---|---|
| Office entities | Read through the MCP: 5 lights (group with 4 members), occupancy, Alpstuga CO2, temperature, humidity, PM2.5, the climate entity in `heat` (modes `off`, `heat`), three IR buttons |
| `--print-states` from the dump | 52 keys, 1359 bytes, longest line 33 characters |
| Upload to the device | `/tmp/ha.states` written through `.new` and FTP rename (`rename via ftp`); `/tmp/ha.states.new` not left behind; the device copy equals the local text (age keys excluded) |
| `/tmp/ui.events` read | Read-only FTP get; two existing lines (`reload`, `start`) skipped as written before the daemon started |
| Offline event tests (scratch harness, dry run) | Backlog skipped; new toggle sent; repeat toggle debounced; `set_*` coalesced to the last value; clamps (a batch with `set_target:40` then `set_target:21.3` sends only 21.5, `set_ct:1000` as 2202, `set_hue_sat:400,-5` as 359,0, `set_bri:0` as turn_off); refusals for unmapped slot, sensor toggle, colour op on the ambient bulb, mode `cool`, unparsed line; GUI restart (`seq 16 -> 1`) logged; device reboot (new file) read from the start; stale event refused; kill switch; rate limit allows 6 in a burst |
| Light path | `--action office_all toggle` posted to the webhook (`http 200`); `light.office_all_lights` and its one lit member turned off at once (context with no user = automation). Restored with `--action office_amb set_bri:15`: group on, ambient at brightness 38, the other three off, exactly as before |
| HA-side refusal | A post for a non-existent light ran the default branch `refused: op or entity not on the allow-list` (trace) |
| Vent and merge (offline, 2026-11-07) | `python3 -m unittest tests.test_ha_ui_gen`: merge order and single `ui` line, host parser accepts the merged file, Vent tile and page, layout override, generic slot names, limits; dry run of three `press` events for the vent slots accepted, a press for an unmapped or display-only slot and for a light refused, a double press debounced. No webhook was called |
| Not exercised | IR buttons, thermostat, colour and colour-temperature ops against HA (allowed by the user request only for the one light test); the `rest` and `push` read paths against the live HA (no token, no push setup); a rotation of `/tmp/ui.events` |

## Removal

Delete `automation.homtom_ui_actions` and `input_text.homtom_ui_areas`, the token (if created) and the `homtom-ui` user; delete `.runtime/ha-ui*`, `.runtime/ha_ui_webhook`, `.runtime/ha_token`; remove `/tmp/ha.states` from the device or reboot it.

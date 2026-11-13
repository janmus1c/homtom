# Home Assistant on the touch UI (GUI side)

Status: draft implementation, 2026-11-07; restyled to `docs/UI_DESIGN.md` (Home Assistant look) the same day, with the System hub, screen lock and time zone (`docs/UI_CONFIG.md`). Built and tested on the Mac only (host preview renders, action traces, rejected configs, `homtom-e2e --fake`), including swipe navigation, page dots and groups. The device binary `workspace/gui/out/homtom-gui-cfg` cross-compiles and links but has not run on the device. Code: `workspace/gui/cfg.c`, `cfg.h`, `hw_cfg.c`, `host.c`. Sample: `workspace/gui/conf/office.conf` with `workspace/gui/tests/ha.states.sample`. Config syntax: `docs/UI_CONFIG.md`.

The Mac talks to Home Assistant; the device never does. Two plain files in the device's RAM carry everything:

    Mac  --(writes, atomic rename)-->  /tmp/ha.states  -->  homtom-gui-cfg   (what to show)
    Mac  <--(takes by rename)--------  /tmp/ui.events  <--  homtom-gui-cfg   (what the user did)

This document is the contract for both files as the GUI implements it, plus what the GUI does on its side. The Mac and HA side is a separate implementation.

## 1. States file: `/tmp/ha.states`

Written by the Mac, read by the GUI every second (every 250 ms while a finger is down). Replace it atomically: write `/tmp/ha.states.new`, then rename.

    office_amb.name=Ambient
    office_amb.state=on
    office_amb.bri=60
    office_amb.ct=3000
    office_co2.name=CO2
    office_co2.val=1204
    office_co2.unit=ppm

| Rule | Value |
|---|---|
| Line | `KEY=VALUE` and a newline; at most 80 characters without the newline |
| Key | `SLOT.FIELD`, characters `[a-z0-9_.]`, at most 31. A leading `ha.` is accepted and dropped, so `ha.office_amb.state` and `office_amb.state` are the same key (a slot can therefore not be called `ha`) |
| Slot | `[a-z0-9_]`, 1 to 15 characters, chosen by the Mac (generic ids such as `office_amb`, `office_hue1`) |
| Value | printable ASCII (32..126); a tab or `\r` makes the line invalid |
| File | the first 4096 bytes are read; a line cut by that limit is ignored. A last line without a newline is accepted when the file is shorter than 4 KB |
| Keys | the first 64 valid lines; later ones are ignored. A repeated key keeps its first value |
| Invalid lines | skipped silently, the rest of the file is used |

Inside the GUI each key is visible as `ha.SLOT.FIELD` (for `tile`, `bar`, `gauge`, `row` and `label`), so `key=ha.office_co2.val` shows the CO2 value.

### Fields per slot

| Field | Meaning | Used by |
|---|---|---|
| `name` | short label | entity, lightctl, thermo caption (`label=` in the config overrides it; without either the slot id is shown) |
| `state` | `on`, `off`, `unavailable`, `unknown` or any other text (`heat`, `home`, ...) | entity value and colour, lightctl On/Off button, `ha.SLOT.on` |
| `bri` | brightness 0..100 | entity value of a light that is on (`60%`), lightctl brightness slider |
| `ct` | colour temperature in kelvin, 2000..6500 | lightctl shows the colour temperature slider (2200..6500 K) only when this key exists; bulb icon tint |
| `hue`, `sat` | 0..359, 0..100 | lightctl shows the palette and hue strip only when `hue` exists; bulb icon colour |
| `val` | sensor value as text (`1204`, `22.4`) | entity value, thermo current value (two-line big digits when numeric) |
| `unit` | `ppm`, `C`, `%` | after `val`, in small type (dropped when it does not fit the tile) |
| `target` | thermostat target, `21.5` | thermo target, `+`/`-` |
| `mode` | `heat`, `off`, `cool`, ... | thermo mode button |
| `age` | seconds since the entity last changed in HA (buttons: since the last press) | light page footer `Changed 4 min ago`; a button entity's state line (`25 min ago`); also the key `ha.SLOT.age` |
| `icon` | optional: the HA icon, an mdi name with or without `mdi:` | the entity's icon when it is one of the compiled icons (`workspace/gui/gen_assets.sh`); otherwise the kind decides (`docs/UI_CONFIG.md`, `entity`) |

Units are shown as HA shows them: `C` as `°C`, `F` as `°F`, `ug/m3` as `µg/m³`, after a space (`22.4 °C`, `48 %`).

### Reserved slot `sys` (Mac daemon to GUI)

| Key | Meaning |
|---|---|
| `sys.tz_off` | The current UTC offset in minutes, summer time included (`60`, `120`, `-240`), computed by the Mac from the IANA zone in the Home Assistant configuration and refreshed every few minutes. Used for the header clock when `INTERNAL:/homtom/tz` is missing or `auto` (`docs/UI_CONFIG.md`, "Time zone") |
| `sys.tz_name` | The zone's name as text (at most 24 characters shown), for the Time and date page |

The GUI needs no summer time rule for `auto`: the Mac sends the offset in effect. A slot may therefore not be named `sys`.

Derived key `ha.SLOT.on`: `1` when `state` is `on`, `home`, `open`, `heat`, `cool`, `heat_cool`, `auto`, `dry` or `fan_only`; `0` for any other state; unset (shown as `--`) when `state` is missing, `unavailable` or `unknown`. Use it for colours: `colorkey=ha.office_amb.on good=1`.

An entity whose `state` is a number and that has no `val` shows the state as its value, so a sensor works with either key.

### Staleness (offline look)

The GUI compares the file's modification time with the device clock. When the config uses HA at all (any `slot=`, `action=ha` or `ha.*` key) and the file is older than 60 s (or more than 60 s in the future) or missing:

| Element | Look |
|---|---|
| Header | `cloud-off-outline` icon and `Offline N min` in the warning colour in place of the clock (`Offline` alone when there is no file) |
| Entities, lights, thermostat | The last values stay; state colours turn grey (`text2`), chips `chip_off`, no brightness fill, the thermostat says `Last: heating`, its buttons and HA buttons draw disabled |
| Taps | Still sent: the event lines wait in `/tmp/ui.events` until the Mac takes them (`docs/UI_STYLE_NOTES.md` explains why taps are not blocked) |
| No value | `No reading` (a missing file); `Unavailable` in the disabled colour for `unavailable`/`unknown` states |

Write the file at least every 10 to 15 s even when nothing changed, so a working link never looks stale.

## 2. Events: `/tmp/ui.events`

The GUI appends one line per HA action to the existing event log (`docs/UI_CONFIG.md`, "Events"); same format, same 16 KB cap (the GUI stops appending until the consumer has taken the file by renaming it), same `seq`/`up` counters:

    seq=12 up=93790 event=ha name=office_amb value=toggle page=office
    seq=13 up=93791 event=ha name=office_hue1 value=set_bri:52 page=light
    seq=14 up=93795 event=ha name=office_clim value=set_target:21.0 page=climate

`name` is the slot, `value` is `OP` or `OP:ARG`, `page` the page id the user was on.

| `value` | Sent by | Argument |
|---|---|---|
| `toggle` | entity `tap=ha:toggle`, lightctl On/Off button, `action=ha arg=toggle` | none |
| `press` | entity `tap=ha:press`, `action=ha arg=press` (button entities, scenes, scripts) | none |
| `set_bri:N` | lightctl brightness slider; `action=ha arg=set_bri:N` | 0..100 (0 = off) |
| `set_ct:K` | lightctl colour temperature slider (2200..6500 in 50 K steps); config 2000..6500 | kelvin |
| `set_hue_sat:H,S` | lightctl hue strip (with the current `sat`, or 100 when `sat` is missing or below 20) and the 12 colour swatches | H 0..359, S 0..100 |
| `set_target:T.T` | thermo `+`/`-` (0.5 steps, clamped to the widget's `min`..`max`, default 7..30); config | always one decimal: `21.0`, `21.5` |
| `mode:NAME` | thermo mode button: the entry after the current `mode` in the widget's `modes=` (default `heat,off`), the first one when the current mode is not listed | `[a-z0-9_]`, max 15 |

### When they are sent

| Input | Timing |
|---|---|
| Taps (toggle, press, mode, On/Off) | at once, on release |
| Brightness, colour temperature, hue strip (drag) | the value under the finger is sent when the finger lands, then again whenever it changed, at most once every 300 ms (at most 3.3 lines a second); the last value is always sent, at the latest 300 ms after the finger lifts. A value equal to the last one sent is not repeated |
| Colour swatch | as a drag value: at once unless the previous HA line went out less than 300 ms ago |
| Thermostat `+`/`-` | 1 s after the last tap, once, with the final target |

A queued value for a different slot or operation is sent before the new one is queued, so nothing is lost when the user moves from one control to another.

### Optimistic display

After an action the GUI shows the expected result at once instead of waiting for the next states file:

| Operation | Fields shown |
|---|---|
| `toggle` | `state` flips between `on` and `off` (only when the current state is known) |
| `set_bri:N` | `bri` = N and `state` = `on` (`off` for 0) |
| `set_ct:K` | `ct` |
| `set_hue_sat:H,S` | `hue`, `sat` |
| `set_target:T.T` | `target` (amber while the send waits for its 1 s) |
| `mode:NAME` | `mode` |
| `press` | nothing |

Each such value is kept until a states file contains exactly the same string for that key (confirmed), or 5 s have passed; then the file's value shows again. A states file that still has the old value does not end it early, because HA usually needs a moment to report the change. The comparison is textual: write `bri` as a whole number, `target` with one decimal (`21.5`, not `21.50`), `ct` in kelvin. A value HA rounds differently (asked 60, reports 59) is shown as asked for up to 5 s and then as reported. At most 8 fields are held at once; the oldest is dropped first.

## 3. Config (summary; full syntax in `docs/UI_CONFIG.md`)

    ui version=1 title=homtom start=office nav=both
    page id=office title=Office grid=4x3
    lightctl at=0,0 span=2,1 slot=office_all label="All lights" ct_min=2000 ct_max=6500 color=1 detail=office_l1
    thermo at=2,0 span=2,1 slot=office_thermo label=Heating min=10 max=26 step=0.5 modes=off,heat
    entity at=0,1 slot=office_co2 icon=co2 label=CO2 good_below=1000
    entity at=1,1 slot=office_amb icon=bulb tap=ha:toggle hold=page:office_l1
    entity at=1,2 slot=office_ac_pwr label="Vent on/off" tap=press confirm=1
    button at=0,2 label="All off" action=ha slot=office_all arg=set_bri:0

    page id=office_l1 title=Ambient grid=1x1 hidden=1 group=office_lights parent=office
    lightctl at=0,0 slot=office_amb label=Ambient color=0

| Item | What it does |
|---|---|
| `entity` | With `tap=`/`hold=`: an HA tile (36 px icon chip in the state colour, name, state; a light that is on shows its brightness as a fill). Without: a reading (icon, value, name); readings side by side share one card (sensor strip). `icon=` short names or any compiled mdi name; without it the states `icon` field or the kind decides. `Unavailable` in grey, `No reading` without data. `high=`/`low=` turn a value red |
| `tap=ha:OP`, `tap=toggle`, `tap=press`, `tap=page:ID` | Action on a short tap (`tap=` is the same as `action=` + `arg=`) |
| `hold=page:ID` / `hold=ha:OP` | Action on a long press: held 600 ms without moving more than 12 px. It fires while the finger is still down, once; the release that follows does nothing |
| `action=ha slot=SLOT arg=OP` | The same operations on a `button`, `tile`, `bar` or `gauge` |
| `lightctl` | 206 px high or more: the full light control (big brightness, power button, brightness slider, colour temperature gradient, 7 swatches with `More` for the hue strip, `Changed ... ago`); Back is the header arrow. Lower: compact (chip, name and brightness, slider; tapping the left part opens `detail=PAGE`, or toggles without it). `ct_min`/`ct_max` (range of the colour temperature slider), `color=0/1`, `dim=0/1` say what the light supports; without them the states keys decide (`ct` present: temperature slider, `hue` present: palette). Needs at least 200 x 40 px |
| `thermo` | 150 px high or more: the thermostat card (HVAC action, `Target` big, `Now`, minus and plus; a mode button from 260 px wide, else the top row cycles the mode). Lower: compact (name, current value and mode on the left, tap there for the next mode; `-` target `+` on the right). `step=` in tenths allowed (default `0.5`), `min=`/`max=` bound the target (default 7..30), `modes=` the mode cycle (default `heat,off`). Needs at least 200 x 40 px |
| `slot=*` | The slot of the widget whose tap or hold opened the page; one detail page serves every entity. Also usable in keys: `key=ha.*.val` |
| `hidden=1`, `group=`, `parent=` | Detail pages: not in the tabs or dots, reached by actions; pages of one group are swipe neighbours and show their own dots; Back (and `action=page arg=back`) returns to `parent` |

The existing `tile`, `bar`, `gauge`, `label` and `row` work unchanged with `ha.*` keys, e.g. `tile key=ha.office_amb.state fmt=onoff colorkey=ha.office_amb.on good=1`.

### The Mac-side generator

`tools/homtom-ha-ui-gen.py` writes the area pages and puts them in front of the base config `userland/ui.conf.default` (merge contract and reserved page ids: `docs/UI_CONFIG.md`, "Base config and the merge with area pages"). Ventilation: one `Vent` tile per area (`entity ... slot=vent_onoff label=Vent icon=fan tap=ha:press hold=page:office_vent`) and a hidden page `office_vent` (`parent=office`) with three large buttons (`button ... icon=power action=ha slot=vent_onoff arg=press confirm=1`, the same with `icon=fan` for `vent_high` and `vent_low`); `conf/office.conf` is the sample. The generator writes `entity`, `lightctl` and `thermo` with `slot=`, `label=`, `tap=press`/`tap=toggle`, `confirm=1`, `good_below=`, `ct_min=`/`ct_max=`, `color=0/1`, `dim=0`, `detail=`, `step=0.5`, `modes=`, `min=`/`max=`, pages with `hidden=1 group= parent=`, and `ui ... nav=both`. The parser accepts all of them; `workspace/gui/tests/conf/generated.conf` is the generator's documented example verbatim (plus a second area page) and is rendered and driven in `tests/run.sh`. Differences that remain:

| Generator | GUI |
|---|---|
| no `icon=` on entities | allowed; the GUI picks the icon by kind and unit (or the states `icon` field) |
| `pages_max` default 6, comment "the current parser limit" | the limit is now 20 pages, at most 6 area pages; the base config takes 10 |
| `docs/HA_UI_BRIDGE.md` table "Parser today: pending" | all those keys are implemented here; that table and its "Today it rejects lightctl" note are out of date (left for the Mac side to update) |

### Navigation (swipes, dots, header)

Area pages are the top level and swipe in file order (60 px or more horizontally, more than twice the vertical travel); the System hub is reached by the gear in the header or a long power key press, and its subpages and the light pages have a back arrow in the header. Dots (6 px, 12 px apart, centred on y 260) show only with two or more swipe neighbours: never for a single area page, never on the hub or its subpages, always for a light group. A gesture that starts on a slider, a thermo, or a lightctl slider or hue strip stays with that widget; a still finger is a long press. The header is a 32 px strip: the 18 px mark, the Poppins SemiBold `homtom` wordmark (a pre-rendered bitmap), a separator and the page title, on the right the gear, the battery indicator and the clock. `ui anim=1` is refused as not implemented.

## 4. Limits, memory and binary size

| Item | Value | Why |
|---|---|---|
| Pages | 20, at most 6 area pages | the base config (hub and subpages) takes 10 |
| Widgets, file, line, string pool | 96, 12 KB, 200, 6 KB | the base takes 59 widgets and 4.8 KB |
| HA keys | 4 KB file, 64 keys, 80-character lines | the contract |
| Optimistic values | 8 fields | one light page uses up to 4 (`bri`, `state`, `hue`, `sat`) |
| RAM (`bss`) | 336 KB | back buffer 261 KB, three config records, the states table |
| Binary (stripped) | 129,500 bytes; cap 160 KB in `tests/run.sh` | Roboto at five sizes (Latin-1), 44 icons at 20 and 24 px, the wordmark, the new widgets; reasoning in `docs/UI_CONFIG.md`, "Binary size" |

The size rules stay: `-O1`, no libgcc (divisions by variables through `gui_udiv`), no `memcpy`/`memset`, `-march=armv4t -marm`, `-fno-pie`, `--gc-sections`, no initialised writable data in `hw_cfg.c`.

## 5. Host preview and tests

    workspace/gui/build.sh preview                        # out/cfg/office-<n>-<page>.png with tests/ha.states.sample
    workspace/gui/out/preview -c conf/office.conf -f sample.txt -e tests/ha.states.sample -A 3 -L 181,81 -S 400,70,200,70 -o x.png
    workspace/gui/build.sh test                           # renders, traces, rejected configs, size cap
    workspace/gui/build.sh dev-cfg                        # out/homtom-gui-cfg (does not touch tools/devtools/)

Preview options for HA and gestures: `-S x0,y0,x1,y1` swipe (down, five moves, up), `-e FILE` states file, `-A SECONDS` its age (default from its mtime; tests use 3 for fresh and 45 for stale), `-E FILE` a new states file arrives at that point, `-L x,y` long press (down, 700 ms, up), `-w MS` let time pass (queued sends, the 5 s expiry). The simulated clock moves 10 ms per touch event and otherwise only with `-w`, so traces are deterministic.

Tests (`workspace/gui/tests/run.sh`, 96 renders, 23 traces, the rejected configs in `tests/bad/`, 62 time zone cases): every page of every sample and fixture, `conf/office.conf` merged with the base config, the light theme, the offline look (5 min) and the missing states file, header battery states (full and charging, 55 %, 12 %, missing), the unfitted light sensor, a long press into a light page, a brightness drag, the hue strip, optimistic values, a pending thermostat target, the vent page and its confirmation, the time zone set on the device. Traces cover the taps, holds and drags of the office page (`ha-taps`, `ha-light`, `ha-colour`, `ha-move`, `ha-thermo`, `ha-group`, `vent`), the generator's widgets (`generated`), `action=ha` buttons (`ha-buttons`), swipes and dots (`swipe`, `wrap`), the gear, Back and the power key on the merged config (`keys`), screen lock (`builtin`, `autolock`), the power key hold hints (`hold`), Restart and Power off with their confirmations (`power`, `storage`), `page:cycle` (`cycle`) and the time zone picker (`tz`). The suite also checks that the compiled-in config equals `conf/default.conf`, that `userland/ui.conf.default` equals `conf/full.conf`, and (when `tools/gui-assets/fetch.sh` has run) that the font, wordmark and icon tables regenerate byte for byte. `tests/e2e/ha-hold.e2e`, `hub.e2e`, `slider.e2e`, `lock.e2e` and `power.e2e` drive the same flows through `homtom-e2e` (`--fake` passes).

## 6. What to test on the LCD

Nothing of the restyle has run on the device. With the merged config (or `conf/office.conf` plus `userland/ui.conf.default`) as `ui.conf` and a states file in place:

1. Text: Roboto at 12, 14, 16, 20 and 28 px; check stems and edges at 2x zoom in a `tools/homtom-screen` shot and by eye at arm's length; check `°`, `µ`, `·`, `…` and Slovenian letters in a label.
2. Header: the mark and wordmark centred on one axis; the wordmark in bone on the dark strip; battery fill colours and the charging bolt readable; the clock correct for the zone (Time and date page).
3. Dark greys on the TN panel: card `#212021` against background `#101010`, borders `#313031`, the off chip `#292829`. If cards vanish at an angle, raise `card` (the spec's fallback is the reverse, `#181818`, if it looks too light).
4. State colours: amber lights, orange heating, steel sensors, the primary blue only on controls; the brightness fill of light tiles (amber at 10 %) visible but quiet.
5. The colour temperature track: the 4x4 dither should hide banding; note any visible pattern.
6. Touch: every target with a fingertip and a stylus: tiles (146 x 76), the 40 px sliders, the 36 x 40 compact thermostat buttons, the header back arrow and gear (48 x 40), the 40 px time zone rows, the dot strip ends.
7. Power key: short press locks (backlight off, touches ignored), short again unlocks with current data; a 2 s hold opens the hub from a light page and from a locked screen; `/tmp/ui.events` shows `event=screen`. Hold hints (once the daemon writes `key hold5`/`key hold10`): the card appears, also on a locked screen, and the release does not toggle the lock. Device, Power: each button asks first; test Cancel on the device, and Restart only when a restart is wanted.
8. Offline: stop the Mac side for over a minute: `Offline 1 min`, grey states, values kept.
9. Redraw time of the light page (gradient and hue strip) and of a full page, and the swipe feel, as before.

## 7. Contract notes

The implementation follows the interface contract as given. Points where the contract left room, and the choice made:

| Point | Choice |
|---|---|
| Key prefix in the file | Both `SLOT.FIELD` and `ha.SLOT.FIELD` are accepted (one consequence: no slot named `ha`) |
| "until the next states file arrives (revert after 5 s if no confirmation)" | Kept until a file confirms the exact value, or 5 s. A newer file that still has the old value does not revert it early (it would flicker back while HA catches up) |
| Brightness "on release and every 300 ms while dragging" | Also sent when the finger lands; repeats are skipped; the release value can wait up to 300 ms so the 300 ms spacing (3.3 a second, under the 4 a second cap) also holds at release |
| Hue strip saturation | the light's current `sat`, or 100 when it is missing or below 20 (otherwise the strip would only pick near-white) |
| `toggle` with unknown state | sent, but no optimistic change (the GUI cannot know the result) |
| Stale marker | after 60 s: `Offline N min` in the header and the grey offline look (section 1) |
| `age` field | passed through as a key, not interpreted |
| Supersets | `tap=page:ID` and `hold=ha:OP` are allowed as well; `action=ha` works on buttons, tiles, bars and gauges; `thermo` has no built-in Back button (the sample uses `button ... action=page arg=back`) |
| `lightctl` size | the contract describes the detail control; the generator also places it as a 2x1 tile, so a box lower than 206 px draws the compact form |
| `thermo` mode on the compact form | no room for a button: the mode is shown after the current value and a tap on the left part selects the next mode |
| Swipe on a full-page `lightctl` | "a drag that starts on a lightctl stays with it" would make detail pages unswipeable; only its sliders and hue strip keep the gesture |
| Dots default | `nav` defaults to `both` with two or more visible pages; with one visible page to dots only, so light groups still get dots and the single page keeps its full height |
| `anim=1` | refused (not implemented), see "Navigation" |

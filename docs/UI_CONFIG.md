# Configurable touch UI (`ui.conf`)

Status: draft implementation, 2026-11-07, restyled to `docs/UI_DESIGN.md` the same day (Home Assistant look, System hub, screen lock, time zone). Built and tested on the Mac only (host preview, regression tests, `homtom-e2e --fake`); the device binary `workspace/gui/out/homtom-gui-cfg` cross-compiles (ARMv4T, 129,500 bytes) and links but the restyled build has not run on the device. Code: `workspace/gui/cfg.c`, `cfg.h`, `hw_cfg.c`, `gui.c`. Samples: `workspace/gui/conf/*.conf`. Home Assistant widgets, keys and the states/events contract: `docs/HA_UI.md`. Visual rules: `docs/UI_DESIGN.md`; where the renderer differs from the mockups: `docs/UI_STYLE_NOTES.md`.

`homtom-gui-cfg` draws the screens described in a plain text file: how many pages, what is on each page and what each button, slider or toggle does. Without a valid file it uses its built-in config, which is `conf/default.conf` compiled in (a System hub with Display, GPS and Device).

## File, default and reload

| Item | Value |
|---|---|
| Location | `INTERNAL:/homtom/ui.conf`, on the device `/mnt/internal/homtom/ui.conf`. Override with `-c FILE` |
| Missing file | Built-in config (`conf/default.conf`; `tests/run.sh` checks that the copy in `cfg.c` matches) |
| Invalid file | First start: built-in config. Later: the last good config stays active. Either way the header shows `ui.conf line N: reason` for about 8 s |
| Hot reload | The file is re-read every 2 s and re-parsed when its content hash changes. `SIGHUP` and the `reload` action force a reload |
| Limits | 12 KB file, 200 characters a line, 20 pages of which at most 6 area pages (not `hidden=1`, not `hub=1`), 96 widgets, 6 KB of strings, 12 variables |

Write the file atomically (write `ui.conf.new`, then rename) so the GUI never parses half a file. A half-written file is not dangerous anyway: it fails to parse and the previous config stays. Variable values (backlight, volume, mute, your own) survive a reload; the current page is kept when it still exists.

The built-in config is a small System hub (Display with the brightness slider, GPS, Device) with the shipped power key bindings; the older `homtom-gui` dashboard's Home page is gone.

## Syntax

One directive per line, then `key=value` pairs separated by spaces. A value with spaces goes in double quotes (no escapes). `#` starts a comment line. Tabs count as spaces. Unknown directives, unknown keys and bad values are errors: a typo never silently changes what a button does.

    # comment
    ui version=1 title=homtom start=home nav=both
    page id=home title=Home grid=2x2
    tile at=1,1 label=GPS key=gps.fix colorkey=gps.fixq good=1 color=warn

### Directives

| Directive | Keys | Meaning |
|---|---|---|
| `ui` | `version=1`, `title=`, `start=PAGE_ID`, `nav=`, `wrap=0/1`, `anim=0`, `theme=dark/light`, `tz=MIN[,eu/us]`, `autolock=SECONDS` | First page (not a hidden one; default the first area page, else the hub), navigation (below). `wrap=1`: swiping past the last page goes to the first and back. `anim=0` is accepted; `anim=1` (slide transition) is rejected as not implemented. `theme`: token table of `docs/UI_DESIGN.md` section 5, default dark. `tz`: fallback clock offset ("Time zone"). `autolock`: lock the screen after that many seconds without touch or key, 10..3600, 0 off (default). `title=` is accepted and no longer drawn (the header shows the brand wordmark and the page title). Several `ui` lines may appear; later keys win |
| `page` | `id=` (required, `[a-z0-9_-]`, max 15), `title=` (header title and tab label, default id), `grid=CxR` (1..4 each, default `3x2`), `hidden=1`, `group=NAME`, `parent=PAGE_ID`, `hub=1` | Starts a page; the widgets that follow belong to it. Hidden, group, parent and hub: see "Navigation" |
| `var` | `name=`, `value=` | Declares a variable and its start value. Built in: `backlight` (100), `volume` (60), `mute` (0) |
| `on` | `key=short/long`, then `ACTION:ARG` (`screen:toggle`, `page:system`, `page:cycle`, ...) or `action=` and `arg=` | Power key binding (see "Power key and screen lock"). A bare `ACTION:ARG` is the same as `tap=ACTION:ARG` |
| widget | see below | `label tile bar gauge battery button slider toggle list row entity lightctl thermo` |

`nav=` takes `tabs` (the area pages as text tabs in the header, after the title separator), `dots` (a row of page dots), `both` or `none`; `1` means `both` and `0` means `none`, as written by older files. Default (kept for older files): `both` when there is more than one area page, else `dots`. There is no bottom tab row any more. Tabs show only with 2 to 5 area pages whose names fit in the header (up to x 300), and never on the hub or subpages; otherwise `tabs` and `both` fall back to dots. Dots show only with two or more swipe neighbours (so a single area page has none) and never on the hub or its subpages; hidden pages of one `group=` keep their dots. The shipped base config uses `nav=dots`.

### Placement

The screen is 480 x 272. The header is the 32 px strip at the top (`docs/UI_DESIGN.md` section 8); the content area starts at y 40 and is 464 px wide at x 8:

| Page | Content area | Below it |
|---|---|---|
| with dots (two or more swipe neighbours) | 464 x 208 (y 40..247) | dots centred on y 260 |
| without dots (single area page, hub, subpage without group) | 464 x 224 (y 40..263) | 8 px margin |

`at=COL,ROW` and `span=W,H` place a widget on the page grid with 8 px gaps; with dots a `3x2` grid gives 149 x 100 px cells and `4x3` gives 110 x 64; without dots 149 x 108 and 110 x 69. Alternatively `x= y= w= h=` give pixels on the 480 x 272 screen (all four required); `conf/office.conf` uses them to follow the mockup. A widget outside the grid or the screen is an error.

Header: on area pages the 18 px mark, the `homtom` wordmark (Poppins SemiBold, a pre-rendered bitmap), a separator and the page title (or the tabs); on the right the gear (when the config has a hub), the battery indicator and the clock. On the hub (when there is an area page to go back to) and on every hidden page a back arrow and the page title replace the mark and wordmark. A header note (reload error, refused command) or the offline state takes the clock's place. Battery: outline 22 x 11 with a nub, filled by `battery_pct_est` (green from 40 %, amber from 15 %, red below), a bolt while `charging` is 1 (`external_power` when there is no `charging` key), `--%` in grey without a value.

### Widgets

Common keys: `label=` (or `text=`), `key=` (status key, below), `fmt=`, `unit=` (appended), `sub=`/`subunit=` (second, smaller value), `color=`, `low=`, `good=`, `high=`, `good_below=`, `unavailable_below=`, `colorkey=`, `font=s/m/l`, `icon=`, `action=`, `arg=`, `tap=`, `hold=`, `confirm=1`, `var=`, `slot=`, `verb=`.

`icon=` takes the older short names (`bulb`, `thermometer`, `drop`, `co2`/`cloud`, `person`, `presence`, `fire`, `heat`, `snow`, `ac`, `power`) or any Material Design Icons name compiled into `icon_data.h` (`workspace/gui/gen_assets.sh` lists them: `fan`, `crosshairs-gps`, `bluetooth`, `microphone`, `usb`, `clock-outline`, ...; an `mdi:` prefix is accepted). `unavailable_below=N`: when the numeric value of `key=` is below N the widget shows `Unavailable` in the disabled colour (the light sensor reads 2 to 6 on this unit, probably not fitted).

| Widget | Shows | Touch |
|---|---|---|
| `label` | `text=`, or the value of `key=`; `font=`, `color=` (default `muted`), `align=left/center/right` | none |
| `tile` | Card with a small caption, the value (20 px; 28 px with `font=l`), `sub=` below or at the right | `action=` if given |
| `bar` | Caption, value and a thin bar from `min=` to `max=` (default 0..100) in the sensor colour | `action=` if given |
| `gauge` | Caption and a 270 degree arc from `min=` to `max=` with the value in the middle | `action=` if given |
| `battery` | Battery card: caption, the level, a bar, `Charging` / `External power` / `On battery`. Keys default to `key=battery_pct_est charging=charging power=external_power`. Large layout when the widget is at least 140 px high | none |
| `button` | Text button (label in the primary colour, red for `action=storage`). With `icon=` and at least 72 px high: a tile with an icon chip, the label, the value of `key=` under it and a chevron when it opens a page (the hub tiles). With `verb=TEXT`: a list row (icon, label, the value of `key=` or else `unit=` as a fixed second line) with a text button reading TEXT at the right; the whole row is the target. Highlighted while its page is shown or `mute` is on | runs `action=` (or `tap=`) |
| `slider` | Caption, current value plus `unit=`, and a 40 px control slider. `min=`, `max=`, `step=`. Bound to `var=` (defaults to the variable of `action=backlight/volume`) | drag updates the variable; release runs `action=` once with the value |
| `toggle` | Label (optional `icon=`, `key=` status line) and a switch bound to `var=` (defaults as for slider; `action=mute` uses `mute`) | flips the variable, then runs `action=` with 0 or 1; with no action it only sets the variable. With `action=cmd arg=NAME` it runs `NAME-on` or `NAME-off` from the allow-list (`arg=gps`); a refused start flips the switch back |
| `list` | Card with an optional caption; the `row` lines that directly follow it are its rows (up to 42 px each). Buttons and toggles placed inside its rectangle draw as its rows (no card of their own, dividers between them) | none |
| `row` | One list row: `label=` (optional `icon=`) on the left, the value of `key=` (with `fmt=`, `unit=`, `low=`, `good=`) on the right. With `tap=page:ID` a chevron; with `tap=tz:set:...` a check mark on the current setting | `tap=page:` / `tap=tz:` only |
| `entity` | Home Assistant entity for `slot=`. With `tap=` or `hold=`: a tile (36 px chip, name, state; lights show their brightness as a fill; 28 px chip under 140 px wide; one line under 56 px high). Without: a reading (20 px icon, value, name under it from 56 px); readings side by side on one row (same y and height, gap 0..8 px) share one card, a sensor strip. Icon: `icon=`, else the slot's `icon` field (mdi name), else by kind: light, climate, button (`tap=press`), presence (read-only on/off: `Detected`/`Clear`), sensor by unit (`C` thermometer, `%` humidity, `ppm` CO2, `ug/m3` PM, else eye) | `tap=`, `hold=` |
| `lightctl` | Light control for `slot=`. At least 206 px high: full control as `docs/ui-mockups/light-detail.png` (big brightness, description, power button, brightness slider, colour temperature gradient, 7 swatches and `More`, which swaps the swatches for a hue strip; `Changed N min ago` under it from the slot's `age`). Lower (at least 40): compact (chip, name and brightness on the left 9/16, slider on the right) | full: its parts; compact: the left part opens `detail=PAGE` (else toggles), the slider sets brightness |
| `thermo` | Thermostat for `slot=`. At least 150 px high: card as the mockup (chip and HVAC action, `Target` in 28 px, `Now`, minus and plus 62 x 40 at the bottom right; a mode button at the top right from 260 px wide, else the top row cycles the mode). Lower: compact (name, mode and current value on the left, tap there for the next mode; minus, target, plus on the right) | `-`/`+` change the target by `step=` (tenths allowed, default `0.5`) within `min=`..`max=` (default 7..30) and send it after 1 s; the target shows grey until HA confirms |

`lightctl` and `thermo` need at least 200 x 40 px (a full `thermo`, 150 px or higher, may be 140 wide) and take no `action=`, `tap=` or `hold=`. Keys only for `lightctl`: `detail=PAGE_ID`; `ct_min=`/`ct_max=` (kelvin, 2000..6500: the light has colour temperature, slider range), `color=0/1` (palette and hue strip), `dim=0/1` (`dim=0`: no brightness slider). When any of these four is given they decide which controls the full form shows; otherwise it shows a colour temperature slider when the states file has `ct` for the slot and the palette when it has `hue`.

`fmt=`: `text` (default), `int` (rounded), `tenths` (one decimal), `yesno`, `onoff`, `pct` (position between `min` and `max`, 0..100, add `unit=" %"`), `uptime` (seconds as `1d 2h`).

Colour: the value is drawn in `color=` (`bone` default = the text colour, `muted`, `blue`, `ok`, `warn`, `bad`, `ink`), mapped to the theme tokens; `ok` is the normal text colour, because colour is spent on problems and state only. With `high=N` it turns `bad` (error red) at N or above; with `low=N` below N; `good=N` (at or above) and `good_below=N` (below) make it the normal colour (checked in that order). `colorkey=` tests a different key than the one shown. An unknown value shows `No reading` in grey. Fonts: `font=s` 12 px, `m` 14 px (default), `l` 28 px, which has only digits, `. , : % + - °` and `C K`.

`tap=` and `hold=`: `tap=ACTION:ARG` is the same as `action=ACTION arg=ARG`; `hold=ACTION:ARG` runs on a long press (held 600 ms, moved at most 12 px). Both accept `ha:OP` and `page:ID`; `tap=toggle` and `tap=press` mean `tap=ha:toggle` and `tap=ha:press`. `hold=` works on `entity`, `tile`, `bar`, `gauge` and `button`.

### Status keys

Values come from `/tmp/homtom-ui.txt` (written every second by `homtom-statusd`; `-f FILE` overrides). Any key in that file can be shown by its name. Derived keys:

| Key | Value |
|---|---|
| `uptime` | `uptime_s` formatted, `1d 2h` |
| `light.pct` | `light_raw` as 0..100 |
| `gps.fix`, `gps.fixq` | `No fix` / `GPS fix` / `DGPS fix`; GGA fix quality number |
| `gps.sats`, `gps.utc`, `gps.alt`, `gps.speed`, `gps.course`, `gps.hdop` | From the GGA/RMC sentences; altitude in m, speed in km/h. Latitude and longitude are never offered |
| `bt.visible` | `4:35 left` while the Bluetooth pairing window is open (`bt_visible_s` > 0), else `bt_visible` (`closed`, `paired`, `no btd`, `no PIN`) |
| `mic.result` | `-38 dB` (live `mic_level`) while `mic_state` is `running` (`starting` before the first value), then `OK` or `quiet`; `error` |
| `mic.sub` | `pk -12 dB  14 s` (`mic_peak`, `mic_t`) while it runs, `pk -12 dB  last -38` when done; unset on error |
| `mic.ok` | 1 when `mic_state` is `ok`, 0 for any other state: use as `colorkey=mic.ok good=1` |
| `ha.SLOT.FIELD` | Home Assistant values from `/tmp/ha.states` (`-e FILE` overrides): `name`, `state`, `bri`, `ct`, `hue`, `sat`, `val`, `unit`, `target`, `mode`, `age` (`docs/HA_UI.md`). `ha.*.FIELD` uses the slot that opened the page |
| `ha.SLOT.on` | 1 when the slot's state is on (or an active mode), 0 when not, unset when unknown or unavailable |
| `var.NAME` | A variable, e.g. `var.backlight` |
| `page` | Title of the current page |
| `clock`, `date` | Local time `14:32` and `Saturday 7 November 2026` ("Time zone"); unset while the device clock is before 2026 |
| `tz.source`, `tz.zone`, `tz.manual` | Where the offset comes from (`Set on device`, `Auto - Home Assistant`, `Config default`, `UTC`); the zone name and `UTC+01:00`; `Fixed offset UTC+01:00` (the stepper's value) |

Keys statusd writes today: `battery_pct_est`, `charging`, `external_power`, `light_raw`, `uptime_s`, `gga`, `rmc`, `gps_fix`, `gps_sats`, `gps_speed_kn`, plus the lines of `/tmp/ui.extra` (below). Keys a config may already name but nothing writes yet (shown as `No reading` until statusd or another writer adds them): `power_key`, `bt_up`, `bt_links`, `battery_mv_est`, `headphone`.

### Extra keys: `/tmp/ui.extra`

Helper programs publish results without touching statusd: `homtom-statusd` re-reads `/tmp/ui.extra` every second and appends its valid lines to `/tmp/homtom-ui.txt`, and returns them in its JSON under `"extra"` (all values as strings). Rules (`sd_parse_extra` in `workspace/statusd/statusd_core.h`): `key=value` lines, key 1..31 characters of `[a-z0-9_]`, value printable ASCII without `"` and `\`, at most 64 characters a line, 16 keys, the first 1 KB of the file; a last line without a newline is ignored (a writer that is not done), a repeated key keeps its first value, statusd's own keys cannot be overridden. Write it with `homtom-ui-extra KEY=VALUE ...` (replaces those keys, keeps the others, renames over the old file under a lock).

| Key | Writer | Values |
|---|---|---|
| `gps_aid` | `homtom-gps-aid` | `sending`, `OK 12:00`, `sent 12:00`, `no gps.aid`, `bad gps.aid`, `clock unset`, `refused`, `port error`, `failed N` |
| `bt_visible_s`, `bt_visible` | `homtom-bt-visible` | seconds left (every 5 s, 0 when closed); `open`, `closing`, `closed`, `paired`, `no btd`, `no PIN` |
| `mic_level`, `mic_peak`, `mic_t`, `mic_state` | `homtom-mic-test` | dBFS RMS (`-38`), updated about every second for 20 s; highest peak dBFS; seconds left; `running`, `ok`, `quiet`, `error` |

The files only live in RAM: after a reboot the rows show `No reading` until the button is pressed. What each button does on the device: `docs/SCREEN_TOOLS.md`.

## Navigation

Page roles: area pages (not `hidden`, not `hub`; the top level, swiped in file order), the hub (`hub=1`, one per config, the System page: reached by the gear in the header and by `page:system`/a long power key press, never swiped and without dots), and hidden pages (subpages of the hub with `parent=system`, light detail pages with `group=`, the vent page; reached only by actions).

| Gesture or element | Effect |
|---|---|
| Swipe left / right | Next / previous area page: at least 60 px of horizontal travel and more than twice the vertical travel, completed when the finger lifts. No wrap unless `ui wrap=1`. The widget under the finger is not tapped |
| Swipe on a hidden page | Moves between the hidden pages of the same `group=` (file order); a hidden page without a group does not swipe |
| Gesture starting on a `slider`, a `thermo`, or a `lightctl` slider or hue strip | Belongs to that widget, never a swipe (the header row and swatch row of a full `lightctl` do start swipes) |
| Long press (600 ms, at most 12 px movement) | Runs the widget's `hold=` while the finger is down; the release does nothing |
| Dots | One per swipe neighbour, the current one highlighted (rules under `nav=`). A tap in the 40 px left or right of the dots goes to the previous / next one |
| Header tabs | Shows that area page (`nav=tabs`/`both`, 2..5 area pages) |
| Header gear (x 432..479) | The hub; shown on area pages when the config has one |
| Header back arrow (x 0..47) | The page's `parent=`; from the hub the last area page shown; else the page shown before. Same as `action=page arg=back` |

`action=page` (and `tap=page:`, `hold=page:`, `on ... page:`) also takes `cycle`: on a hidden page it is Back; on an area page the next area page, after the last one the hub; on the hub the first area page.

A typical light group:

    page id=office title=Office grid=4x3
    lightctl at=0,0 span=2,1 slot=office_all label="All lights" detail=office_l1
    page id=office_l1 title=Ambient grid=1x1 hidden=1 group=office_lights parent=office
    lightctl at=0,0 slot=office_amb
    page id=office_l2 title="Hue 1" grid=1x1 hidden=1 group=office_lights parent=office
    lightctl at=0,0 slot=office_hue1

There is no slide animation: each page change is one full redraw, as before (`anim=1` is refused rather than ignored, so a config cannot ask for something that does not happen).

## Actions

The config can only name actions from this table. There is no action that takes a path, a command line or shell text.

| `action=` | `arg=` | Effect | Process started |
|---|---|---|---|
| `page` | page id, `next`, `prev`, `back`, `cycle` | Show that page. `next`/`prev` follow the swipe order and wrap; `back` and `cycle` as in "Navigation" | none |
| `backlight` | 5..100 (or the slider value) | Sets `backlight`, clamped to at least 5 % so the UI can never black the panel out | `/bin/homtom-bl LEVEL` (LEVEL = percent x 255 / 100; the tool caps duty at the stock 62.5 %) |
| `volume` | 0..100 | Sets `volume`. There is no mixer daemon; the value is the level of the next `beep` | none |
| `mute` | 0/1, or none to flip | Sets `mute`; `beep` is silent while it is 1 | none |
| `beep` | Hz, 200..4000 (default 1000) | 1 s tone at the current volume, skipped when muted or volume 0 | `/bin/homtom-snd2 --hz HZ --secs 1 --vol RAW` with RAW 48..117 (117 = stock level; never louder) |
| `cmd` | name from the allow-list below | Runs a fixed command | see allow-list |
| `event` | name, `[a-z0-9_-]` max 15 | Appends an event for Home Assistant | none |
| `ha` | `toggle`, `press`, `set_bri:N`, `set_ct:K`, `set_hue_sat:H,S`, `set_target:T.T`, `mode:NAME`; needs `slot=` | Appends an `event=ha` line for the Mac, shows the expected result at once (`docs/HA_UI.md`) | none |
| `set` | number (buttons) | Sets `var=` | none |
| `reload` | | Re-reads `ui.conf` now | none |
| `screen` (only as `tap=screen:...`, `hold=` or an `on` line) | `lock`, `unlock`, `toggle` | Screen lock ("Power key and screen lock") | `/bin/homtom-bl 0`, and on unlock `/bin/homtom-bl LEVEL` |
| `tz` (only as `tap=tz:...`) | `set:auto`, `set:MIN`, `set:MIN,eu`, `set:MIN,us`, `step:N` (-120..120) | Writes `INTERNAL:/homtom/tz` ("Time zone"); `step` moves the fixed offset by N minutes within -720..840, starting from the offset in use | none (the GUI writes the file itself) |
| `storage` | | Reboot into storage mode. Always asks for confirmation first | `/bin/reboot` (the initramfs wrapper: `INFORM0=5` + software reset; with USB attached and the Mac unlocked the unit comes up in storage mode, `docs/LESSONS.md`) |

`confirm=1` on any widget shows an OK/Cancel dialog before the action runs; `storage`, `cmd restart` and `cmd power-off` always do, with their own text and a red action button (`Restart`, `Power off`).

Command allow-list (compiled into the binary, `cmd_name`/`cmd_argv` in `cfg.c`):

| Name | argv | Slot |
|---|---|---|
| `gps-on` | `/bin/homtom-hw gps-on` | gps |
| `gps-off` | `/bin/homtom-hw gps-off` | gps |
| `gps-aid` | `/bin/homtom-gps-aid` (sends position and time with `gpsaid`, `docs/GPS_AIDING.md` section 5) | gps |
| `bt-visible` | `/bin/homtom-bt-visible` (opens the Bluetooth pairing window for 300 s) | bt |
| `mic-test` | `/bin/homtom-mic-test` (20 s live microphone meter) | audio |
| `screenshot` | `/tmp/homtom-shot -o /tmp/homtom-shot.hts` (present only after `tools/homtom-screen` uploaded it) | cmd |
| `restart` | `/bin/reboot` (restarts through the storage window and back; always asks first) | reboot |
| `power-off` | `/bin/homtom-poweroff` (`docs/POWER_KEY.md`; the unit stays off until the power key is pressed; always asks first) | reboot |

The three wrappers are BusyBox scripts in `userland/` with no arguments; they write their result to `/tmp/ui.extra`. `mic-test` shares the audio slot with `beep`, so the codec never plays and records at once; the GPS commands share one slot, so aiding and switching power never overlap.

Not on the list, on purpose: `homtom-bt-discoverable` (it sends HCI commands next to the running `homtom-btd`; the unit is discoverable anyway, `bt-visible` only opens the pairing window), anything that writes GPIO beyond the masked helpers, and power-off (`poweroff` reboots today).

### Process rules

Commands are started with `fork` + `execve` of the fixed argv, with stdin, stdout and stderr on `/dev/null` and no shell. There are six slots (backlight, audio, cmd, reboot, gps, bt; the allow-list names the slot of each command) and each runs at most one child; a second press while one runs is refused with a header note (`mic-test busy or failed`). Once the reboot child exists nothing else starts. Children are reaped with `wait4(WNOHANG)` every loop. A slider starts its process only on release, not while dragging. Nothing runs at start-up: the `backlight` variable starts at 100 to match the panel after boot, and the GUI does not apply it until the user touches a control.

## Events for Home Assistant: `/tmp/ui.events`

Every state change and event appends one line:

    seq=3 up=93784 event=set name=backlight value=40 page=controls
    seq=4 up=93790 event=press name=lights_on value=1 page=scenes
    seq=5 up=93795 event=key name=short value=1 page=scenes
    seq=6 up=93797 event=ha name=office_amb value=toggle page=office

| `event=` | When | `name=` / `value=` |
|---|---|---|
| `start` | GUI started | `gui` / 1 with a config, 0 built-in config |
| `reload` | ui.conf parsed | `ui.conf` / 1 ok, 0 rejected |
| `set` | A variable changed (backlight, volume, mute, toggles and sliders without an action, `set`) | variable / new value |
| `press` | `action=event` | the `arg` / 1, or the toggle/slider value |
| `beep`, `cmd`, `reboot` | Those actions ran | beep / volume, command name / 0, storage / 1 |
| `key` | Power key line on stdin | `short` or `long` / 1 |
| `ha` | An HA operation (tap, hold, light or thermostat control) | slot / `OP` or `OP:ARG` (`docs/HA_UI.md` section 2) |
| `screen` | The screen was locked or unlocked (key, button or autolock) | `lock` or `unlock` / 1 |
| `tz` | The time zone setting was written | the new setting (`60,eu`, `auto`, `90`) / 1 |

`seq` counts from 1 per GUI start and `up` is the device uptime from the status file, so the consumer can detect restarts and gaps. `page` is the id of the page shown (the built-in config's pages are `home`, `gps`, `system`). The file is opened with `O_APPEND` for each line (each line is far below `PIPE_BUF`, so lines never interleave). When it reaches 16 KB the GUI stops appending until the consumer has taken it.

Consumer: `tools/homtom-ha-ui.py` takes the file by renaming it and forwards `event=ha` lines (`docs/HA_UI_BRIDGE.md`).

## Power key and screen lock

`homtom-keyd` writes `key short` and `key long` lines into the GUI's input pipe (`docs/POWER_KEY.md`); `on key=short ...` and `on key=long ...` bind them. The shipped base config and the built-in config use:

    on key=short screen:toggle
    on key=long page:system

| User gesture | Effect |
|---|---|
| Short press | Locks the screen; the next short press unlocks it |
| Hold about 2 s (1.5 to 5 s) | The System hub from any page; on a locked screen it unlocks first |
| Hold 5 s | Reboot to the storage window (the daemon, not the GUI) |
| Hold 10 s, release | Power off (the daemon) |

Lock: the GUI keeps the `backlight` variable (the level the Brightness slider last set, 100 after boot) and runs `/bin/homtom-bl 0` in the backlight slot, the same fixed argv as the slider; unlock runs `/bin/homtom-bl` with that level, at least the slider's 5 % floor. While locked every touch (tap, hold, swipe, drag) is ignored, nothing is drawn (the framebuffer keeps the last frame), and the loop reads the status and states files every 2 s instead of every second; the key pipe is still read, so the unlock press always arrives. Unlocking redraws at once with current data. Any key that is not bound to a `screen:` action unlocks first and then runs its binding. `ui autolock=SECONDS` locks after that long without touch or key. Each lock and unlock appends `event=screen name=lock|unlock`. A backlight child still running when the next lock or unlock comes is refused like any busy slot (header note `backlight busy or failed`); press again.

`page:cycle` (next area page, then the hub, then the first area again; Back on a subpage) is available for `on key=short` but not the default.

Hold hints: the key daemon writes `key hold5` when the key has been held 5 s (release to power off) and `key hold10` at 10 s (emergency reboot). The GUI then shows a card in the middle of the screen over everything: `Release to power off` in amber, or `Restarting…` in red. On a locked screen it lights the backlight for the card (the saved level) and keeps the lock; the card ends with the next `key short` or `key long` (the release of that hold), which then does nothing else (no lock toggle, no page change), or after 15 s; on a locked screen the backlight goes off again. Touches are ignored while the card shows. Older GUI builds ignore the hold lines (unknown key names are dropped).

## Time zone

The header clock (HH:MM, left of the battery) and the `clock`/`date` keys use the device clock (`CLOCK_REALTIME`, UTC) plus an offset; nothing is shown while the clock reads a year before 2026. The offset, first match wins:

1. `INTERNAL:/homtom/tz` (`/mnt/internal/homtom/tz`), one line: `MIN`, `MIN,eu` or `MIN,us`. `auto`, a missing file or anything else falls through. Re-read every 2 s.
2. `sys.tz_off` from Home Assistant (`/tmp/ha.states`, minutes east of UTC including summer time, sent by the Mac; `sys.tz_name` names the zone; `docs/HA_UI.md`).
3. `ui tz=MIN[,eu/us]` in the config.
4. 0 (UTC).

Rules: `eu` adds 60 minutes from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC; `us` from the second Sunday of March 02:00 local standard time to the first Sunday of November 02:00 local summer time. Both are computed in `cfg_tz_offset` (`cfg.c`); `tests/tz.cases` holds 62 boundary cases (one minute either side of each change in 2026, 2027 and 2030, for three EU and two US zones) computed independently with Python, and `tests/run.sh` checks them.

The base config's Time and date page (hub tile) shows the time and date, the source and the zone, and a two-page picker (swipe or dots): Auto (from Home Assistant), UTC, Central Europe (`60,eu`), Eastern Europe (`120,eu`), UK and Ireland (`0,eu`), US Eastern, Central, Mountain, Pacific (`-300,us` ... `-480,us`), and a fixed offset stepper (`-30 min`, `+30 min`, no summer time). A row is `row label=... tap=tz:set:VALUE`; the current one has a check mark. Selecting writes `tz.new` on INTERNAL (mounted `rw,sync`), fsyncs it and renames it over `tz`, takes effect at once and logs `event=tz`. A failed write shows `time zone not saved` in the header.

## Base config and the merge with area pages

The device's `ui.conf` is two parts, concatenated by the Mac-side generator (`tools/homtom-ha-ui-gen.py`):

| Part | Provides | Page ids |
|---|---|---|
| Generator output, first | The area pages in order (one per HA area, `Office` first today), each area's hidden pages: lights (`group=` per area, `parent=` the area) and the vent page. No `ui` line is needed; one is allowed (later lines win) | `office`, `office_l1`..`office_lN`, `office_vent`; further areas likewise |
| Base: `userland/ui.conf.default` (identical to `workspace/gui/conf/full.conf`; `tests/run.sh` checks it) | The `ui` line (`nav=dots`, no `start=`), the power key bindings, the variable `gps`, the hub and all its subpages | `system` (hub), `display`, `gps`, `bluetooth`, `audio`, `device`, `power`, `time`, `tz1`, `tz2` |

Rules: area page ids must not use the base's ids; the first area page becomes the start page (the base has no `start=`); with no area page at all the hub is the start page and its back arrow is hidden. Budget: the base uses 10 of the 20 pages, 59 of the 96 widgets (the two `on` lines included) and 4.8 KB of the 12 KB file, so the area part has 10 pages, 37 widgets and about 7 KB (today's generated Office part: 6 pages, 13 widgets). `tests/run.sh` renders `conf/office.conf` merged with the base and drives the gear, Back, the hub tiles and the power key on it.

## Device capabilities and where they stand

| Capability | In ui.conf |
|---|---|
| Touch | All widgets, swipes, long press; calibrated coordinates from `homtom-touch` via `homtom-ui-start` |
| Backlight | `action=backlight` (slider, preset buttons, power key) |
| Speaker volume, beep | `action=volume`, `action=mute`, `action=beep` |
| Microphone level | `cmd mic-test`; keys `mic_level`, `mic_peak`, `mic_t`, `mic_state`, `mic.result`, `mic.sub` |
| GPS | `gps.*` keys; `cmd gps-on/gps-off`, a toggle with `action=cmd arg=gps`; `cmd gps-aid` |
| Battery, charging, mains | `battery` widget, `battery_pct_est`, `charging`, `external_power` |
| Light | `light_raw`, `light.pct` |
| Bluetooth | `cmd bt-visible` (pairing window, `bt.visible`); keys `bt_up`, `bt_links` once statusd writes them to the UI file (they are in its JSON today) |
| Home Assistant | `entity`, `lightctl`, `thermo`, `action=ha`, `ha.*` keys (`docs/HA_UI.md`) |
| LED | None: the board has no LED (`docs/PERIPHERALS.md` section 1) |
| Power key | `on key=short/long`; `homtom-keyd` writes the lines (`docs/POWER_KEY.md`) |
| Screen lock | `screen:lock/unlock/toggle`, `ui autolock=` |
| Clock and time zone | header clock, `clock`/`date`/`tz.*` keys, `tap=tz:` |
| Reboot to storage mode | `action=storage`, always confirmed |

## Editing from Home Assistant

The format is designed so HA can write it: plain lines, a strict parser that names the bad line, the last good config kept on error, and a `reload` event (value 0 or 1) that tells HA whether its upload was accepted. `tools/homtom-ha-ui-gen.py` generates area pages and uploads them as `ui.conf.new` over FTP, renamed to `ui.conf` (`docs/HA_UI_BRIDGE.md`).

## Samples and preview

| File | Pages |
|---|---|
| `conf/default.conf` | The built-in config (compiled into `cfg.c`): hub with Display, GPS, Device |
| `conf/full.conf` | The base config, identical to `userland/ui.conf.default`: hub, Display and sound, GPS, Bluetooth, Sound test, Time and date (two time zone pages), Device with its Power page (Restart, Power off, USB storage, each confirmed) |
| `conf/office.conf` | Office as `docs/ui-mockups/office.png` (three light tiles, the Vent tile, a sensor strip, the thermostat card), a light page per light in one swipe group, the Vent page |
| `conf/controls.conf` | Controls (sliders, beep, mute) and Scenes (HA event buttons, a confirmed one, backlight presets, a toggle) with header tabs and dots |
| `conf/system.conf` | System (device list, battery gauge, GPS tile, storage-mode button) and Tools (allow-listed commands, reload, test event) |

Test fixtures in `workspace/gui/tests/conf/`: `ha-nav.conf` (HA widgets in a 4x3 grid, `ha.*` keys in tile/bar/gauge/list), `generated.conf` (the generator's example output), `swipe.conf`, `wrap.conf`, `cycle.conf` (two areas, a hub, `page:cycle`, autolock).

    workspace/gui/build.sh preview          # out/cfg/<conf>-<n>-<page>.png for every page of every sample
    workspace/gui/out/preview -c conf/full.conf -f sample.txt -W 1794058320 -p time -o x.png   # fixed clock
    workspace/gui/build.sh test             # tests/run.sh: renders, traces, rejected configs, time zone rules, size cap
    workspace/gui/build.sh dev-cfg          # out/homtom-gui-cfg (ARMv4T, 129,500 bytes stripped)
    workspace/gui/build.sh assets           # regenerate the font, wordmark and icon tables (needs network once)

Preview options: `-t x,y` finger held (repeat to drag), `-u` finger up, `-T x,y` tap, `-L x,y` long press, `-S x0,y0,x1,y1` swipe, `-w MS` time passes, `-K short|long` power key, `-P` print the page (and the lock), `-e FILE` HA states (`-A SECONDS` its age, `-E FILE` a new one arrives), `-W SECONDS` the device clock (UTC), `-Z FILE` the time zone file, `-Q MIN,RULE,SECONDS` print an offset, `-a PREFIX` all pages. Actions print `spawn[slot]: argv`, `event: line` and `tz-file: VALUE` instead of running.

## Binary size

The cap in `tests/run.sh` is 160 KB (it was 66 KB). The restyled binary is 129,500 bytes stripped (67,272 before): 49.5 KB of new tables (Roboto at five sizes 38.4 KB, with Latin-1 in the 12, 14 and 16 px styles; icons 10.7 KB; the wordmark 0.3 KB) replace 12.2 KB of Poppins tables, a net 37.3 KB; the other 23 KB are code and strings (widgets, header, battery indicator, lock, time zone, the larger base and built-in configs). The device has 64 MB of RAM and the binary is loaded from the initramfs, so the only cost is the initramfs growing by the same 60 KB (compressed less, since the glyph tables are already run-length coded); `bss` is 336 KB (back buffer 261 KB, three config records). The build rules are unchanged: `-O1`, no libgcc, divisions by variables through `gui_udiv`, `-march=armv4t -marm`; `readelf -A` reports `Tag_CPU_arch: v4T` and the disassembly has no instruction above ARMv4T and no helper call.

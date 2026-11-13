# gui: on-device UI for the 480x272 LCD

A small freestanding C library (`gui.c`) plus the `homtom-gui` status screen. There is no `/dev/fb0`: the device
program maps `/dev/mem` at physical `0x33F00000` (261120 bytes, RGB565, stride 960), draws into a RAM back buffer
and copies it over. All drawing code is hardware independent, so the same `gui.c` and `ui.c` build for macOS and
write a PNG.

| File | Role |
|---|---|
| `gui.h`, `gui.c` | Drawing primitives, text, widgets. No libc, no libgcc, no `memcpy`/`memset` |
| `font_data.h`, `gen_font.py` | Roboto glyph tables (12/400, 14/500, 16/500, 20/400, 28/400 digits), 4-bit alpha, run-length coded (`pack()`, decoder `draw_glyph` in `gui.c`), UTF-8 extras by codepoint index, tabular digits; generated offline (Pillow, host only). `OFL.txt` is Roboto's licence |
| `wordmark_data.h`, `gen_wordmark.py` | The `homtom` wordmark, Poppins SemiBold 16 px, -0.045 em, one bitmap (`brand/fonts`) |
| `icon_data.h` | Material Design Icons (Apache 2.0) at 20 and 24 px, 2 bpp, from `tools/gui-assets/gen_icons.py --table` |
| `gen_assets.sh` | Regenerates the three tables from the pinned sources that `tools/gui-assets/fetch.sh` puts in `build/gui-assets/` (SHA-256 checked) |
| `ui.h`, `ui.c` | Dashboard of `homtom-gui`: `key=value` parser, three pages, touch handling. `homtom-gui-cfg` uses only the parser |
| `hw_dev.c` | Device layer: raw EABI syscalls, `/dev/mem` mmap, blit, stdin touch, main loop |
| `host.c` | Host layer: PNG writer (stored deflate, no zlib), touch simulation |
| `sample.txt` | Example `/tmp/homtom-ui.txt` with synthetic values |
| `cfg.h`, `cfg.c` | Configurable UI: `ui.conf` parser, widgets (Home Assistant entities, light and thermostat controls), touch (swipes, long press), action allow-list, `/tmp/ha.states` (`docs/UI_CONFIG.md`, `docs/HA_UI.md`) |
| `hw_cfg.c` | Device layer of `homtom-gui-cfg`: as `hw_dev.c` plus config hot reload, `/tmp/ui.events`, `/tmp/ha.states`, timers, fork/execve |
| `conf/*.conf` | Sample configs: default (also compiled in as the built-in config), full (the base config, same as `userland/ui.conf.default`), office (Home Assistant, as the mockup), controls, system |
| `tests/run.sh` | Host regression tests: render hashes (`tests/golden.sha256`), action traces (`tests/expected`), rejected configs (`tests/bad`), time zone rules (`tests/tz.cases`), fixtures `tests/conf`, HA sample `tests/ha.states.sample`, table regeneration, size cap |
| `build.sh` | `dev`, `dev-cfg`, `preview`, `test` and `assets` targets |

## Build and preview

    ./build.sh preview     # clang -> out/preview, out/{home,gps,system}.png
    ./build.sh dev         # docker debian:bookworm-slim + gcc-arm-linux-gnueabi -> out/homtom-gui (stripped, 74 KB)
    out/preview -f sample.txt -p 2 -t 100,240 -z 2 -o x.png   # page, simulated finger, 2x zoom

The device build uses `-nostdlib -march=armv4t -marm -static -fno-stack-protector -O1` (plus `-fno-builtin
-fno-tree-loop-distribute-patterns`). A missing `__aeabi_*`, `memcpy` or `memset` shows up as a link error.

## Configurable UI (`homtom-gui-cfg`)

    ./build.sh dev-cfg     # out/homtom-gui-cfg (129,500 bytes stripped, cap 160 KB); tools/devtools/ is untouched
    ./build.sh test        # after a deliberate visual change: tests/run.sh --update, then look at out/test/*.png
    homtom-gui-cfg [-c /mnt/internal/homtom/ui.conf] [-f /tmp/homtom-ui.txt] [-p page] [-n] [-1]

Same stdin protocol as `homtom-gui` plus `key short` / `key long`, and `-e /tmp/ha.states`. Without a valid `ui.conf` it
uses the built-in config (`conf/default.conf`, compiled in). Format, actions and safety limits: `docs/UI_CONFIG.md`;
Home Assistant: `docs/HA_UI.md`. Look: `docs/UI_DESIGN.md`, deviations `docs/UI_STYLE_NOTES.md`. The `dev-cfg` build adds `-fno-pie -no-pie -ffunction-sections -fdata-sections
-Wl,--gc-sections` to the flags above (reasons in `build.sh`). Not yet run on the device.

## Run on the device

    echo 1 > /sys/kernel/homtom_splash          # done by homtom-gui itself unless -n is given
    homtom-gui [-f file] [-p page] [-n] [-1]
    while :; do { echo battery_pct_est=...; ... } > /tmp/homtom-ui.txt.new; mv /tmp/homtom-ui.txt.new /tmp/homtom-ui.txt; sleep 1; done &
    homtom-ts | homtom-gui                      # touch: homtom-ts prints "x y" raw while down, "up" on release

`homtom-gui` re-reads the file every second (every 250 ms during a touch) and redraws. `-1` draws once and exits with
the picture left on the LCD and the console still muted. SIGTERM/SIGINT leave the loop and write `0` to
`homtom_splash`. Use `mv` to replace the file so a read never sees a half-written one.

Keys (unknown keys ignored, missing keys show as unknown): `battery_pct_est`, `charging`, `external_power`
(`1`/`true`/`yes`/`on`), `light_raw` (10-bit ADC, 0..1023), `uptime_s`, `gga`, `rmc` (raw NMEA sentences, checksum not
verified). Latitude and longitude are never shown.

## API (`gui.h`)

Colours come from the theme token table (`TK(T_CARD)`, `TK(T_AMBER)`, ...; `gui_theme(0/1)` dark/light, docs/UI_DESIGN.md section 5); the older `C_*` names map onto it. Coordinates are pixels, origin top left. Everything is clipped (`gui_clip`, `gui_clip_in`/`gui_clip_out` to nest).

- Primitives: `gui_clear`, `gui_px`, `gui_blend(x,y,c,a16)`, `gui_hline`, `gui_vline`, `gui_rect`, `gui_frame`, `gui_line`, anti-aliased `gui_circle`, `gui_ring`, `gui_rrect`, `gui_rrect_line`; sub-pixel `gui_stroke_f`, `gui_tri_f`.
- Text (UTF-8): `gui_text` (y = top of the line box), `gui_tb` (y = baseline), `gui_tfit` (cut with `…`), `gui_tr` (right-aligned), `gui_text_w`, `gui_text_c`, `gui_text_r`. Fonts `GUI_T12`, `GUI_T14`, `GUI_T16`, `GUI_T20`, `GUI_T28` (digits and `. , : % + - ° C K` only); `GUI_FONT_S/M/L` are `T12/T14/T28`. `gui_wordmark(x, baseline, c)`.
- Icons: `gui_icon(ICON_*, 20|24, x, y, c)`, `gui_icon_find("mdi-name")`, `gui_chip(cx, cy, d, bg, icon, size, fg)`.
- Widgets: `gui_mark`, `gui_card` / `gui_card_c` (radius 12, border), `gui_button(x,y,w,h,label,flags)` (`GUI_PRESSED`, `GUI_ACTIVE`, `GUI_DANGER`, `GUI_OFF`), `gui_ctl_slider` (HA control slider), `gui_ct_track` (dithered colour temperature), `gui_handle`, `gui_switch`, `gui_progress`, `gui_battery`, `gui_batt_ind` (header battery), `gui_slider` and `gui_slider_pct` (track x and width).
- Strings and maths: `gui_cat`, `gui_utoa`, `gui_ftoa`, `gui_pad2`, `gui_udiv`, `gui_isqrt`, `gui_mix`.

Divisions by constants compile to multiplies; divisions by variables must go through `gui_udiv`.

## Adding a page

Add a `page_x()` in `ui.c` built from `tile()`/`gui_card()`, raise `UI_PAGES`, extend `names[]` in `ui_draw()`
and the `ui_draw` dispatch. Nav buttons are laid out as `8 + i * 157`, so three fit; more need a narrower width.

## Fonts, icons and licences

Glyph tables are derived from Roboto (SIL Open Font License 1.1, `OFL.txt` here) and the wordmark from Poppins SemiBold (SIL OFL 1.1, `brand/fonts/OFL.txt`); the icons are Material Design Icons (Apache 2.0). The generated headers keep the notices and the SHA-256 of their sources. To change sizes or the icon set, edit `gen_font.py` / `gen_assets.sh` and run `./build.sh assets` (first `tools/gui-assets/fetch.sh` downloads the pinned sources into `build/gui-assets/`); `tests/run.sh` checks that the checked-in tables regenerate byte for byte.

## Known limits

- The device binary has not been run on the device (no device access in this session); it links clean and the
  shared drawing and parsing code is exercised by the preview.
- Touch calibration is a placeholder (`TS_X0..TS_Y1` in `hw_dev.c`), not `touch.cal`. Axis direction is unverified.
  `t X Y` lines on stdin bypass it.
- Full-frame blit every redraw (about 255 KB write to uncached memory), no tearing control or dirty rectangles.
- The slider and the kernel signal-handler restore path are untested on hardware; the slider does nothing.
- No kerning, no rotation. UTF-8 for ASCII, Latin-1 and the extras listed in `gen_font.py` (Slovenian and Croatian letters, `−`, `…`). `bss` is 336 KB (back buffer 261 KB).

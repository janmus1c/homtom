# End-to-end UI tests: input injection plus screenshots

Status: written 2026-11-07. The Mac side is tested with the host renderer (`--fake`) and against the current device image, where it correctly reports that injection is not available yet. The device side (`/tmp/ui.in`) has not run on the device: it needs the image with the modified `userland/homtom-ui-start`, booted.

## How it works

    tools/homtom-tap  --homtom-sh-->  echo "t X Y" > /tmp/ui.in  -->  homtom-gui-cfg stdin  (same path as real touches)
    tools/homtom-screen --FTP-->      LCD framebuffer  -->  PNG  -->  compare with workspace/gui/out/preview render

`userland/homtom-ui-start` creates the named pipe `/tmp/ui.in` (`mknod p`; `mkfifo` is not in `kernel/scripts/busybox-applets.txt`) and starts the GUI as `touch_lines > /tmp/ui.in | $GUI < /tmp/ui.in`. The touch reader and any injector are both writers of that pipe, so `t X Y`, `up`, `key short|long` lines reach the GUI exactly as calibrated touches do (`hw_cfg.c` `input_line`). If the pipe cannot be created the launcher falls back to the original pipeline. The single-instance lock and the 2 s respawn loop are unchanged; when the GUI dies the pipeline ends at the next touch, as before.

## homtom-tap

    tools/homtom-tap tap X Y [--hold MS]
    tools/homtom-tap swipe X0 Y0 X1 Y1 [--ms N] [--steps K]
    tools/homtom-tap slider XLEFT XRIGHT Y PCT [PCT ...]
    tools/homtom-tap key short|long
    options: --shot [OUT.png]  --settle S  --dry-run

Coordinates are screen pixels (0..479, 0..271). `slider` presses at the first PCT of the track `XLEFT..XRIGHT` at row Y, moves through the others and releases (the action runs on release). Default config System page: track `40 440`, Y `182`. `--shot` runs `tools/homtom-screen` after `--settle` seconds (default 1). `--dry-run` prints the lines and the device command and touches nothing.

The device shell has only whole-second `sleep`, so `--hold` and `--ms` are rounded up to seconds, and anything shorter is sent as one burst (the GUI reacts to positions, not timing). The writer runs as a background job on the device; if nothing reads the pipe it is killed after a few seconds. A request that gets no answer is retried once.

Exit status: 0 ok, 2 bad arguments, 3 `/tmp/ui.in` missing (the running image has the old launcher; nothing was sent), 4 pipe exists but the GUI does not read it (`gui.disable`, crashed, starting), 5 device not in LINUX or no answer.

## homtom-e2e

    tools/homtom-e2e [--fake] [-o DIR] SCENARIO...

Plain text, one command per line, `#` comments. Artefacts (shots as PNG and HTS, host references, `*.diff.png` for failures) go to `.runtime/e2e/<scenario>/`. Exit 1 when a check fails.

| Command | Meaning |
|---|---|
| `config PATH`, `data PATH` | ui.conf and status file for host renders (defaults: `conf/default.conf`, `sample.txt`) |
| `ha PATH` | Home Assistant states file for host renders (`preview -e PATH -A 0`); none by default |
| `ignore X Y W H` | mask a rectangle in all comparisons; use it for live data and the clock |
| `settle SEC`, `wait SEC` | pause after each input (default 1), explicit pause |
| `tap X Y [HOLDMS]`, `swipe X0 Y0 X1 Y1 [MS]`, `slider XL XR Y PCT...`, `key short\|long` | inputs, as `homtom-tap` |
| `shot NAME` | screenshot |
| `expect-match PAGE [MAXPCT]` | last shot vs host render of page id or number; at most MAXPCT percent of pixels differ (default 1) |
| `expect-region PAGE X Y W H [MAXPCT]` (alias `expect-text`) | the same, limited to a rectangle; use it to check a label or value by pixels |
| `expect-replay [MAXPCT]` | last shot vs a host render that replays all inputs so far (sliders, toggles, key presses) |
| `expect-changed [MINPCT]`, `expect-same [MAXPCT]` | last shot vs the previous one |
| `expect-pixel X Y RRGGBB [TOL]` | colour of one pixel, per-channel tolerance (default 8) |

Output per check: `PASS|FAIL label: P% differ (n/total px), limit L%`. Percentages count unmasked pixels whose RGB565 value differs.

`--fake` replaces the device by the host renderer with all injected input replayed; the pause of a held tap (`tap X Y 1000`) is replayed as `preview -w`, so a long press works in the replay too. It validates the scenario, the runner and the UI logic of the host build, nothing about the device. Build the renderer first when needed: `workspace/gui/build.sh preview`.

Examples: `tests/e2e/ha-hold.e2e` (long press on an Office light tile opens its light page, Back returns; needs `workspace/gui/conf/office.conf` as `ui.conf` on the device), `tests/e2e/tabs.e2e` (taps Home, GPS, System, Home and compares the header and tab bar with the host render of each page) and `tests/e2e/slider.e2e` (backlight slider drag against a replayed host render).

## Adding a scenario

1. Copy `tests/e2e/tabs.e2e`. Render the screens first: `workspace/gui/out/preview -c CONF -f workspace/gui/sample.txt -p PAGE -o x.png` and read the coordinates off the image (a `3x2` grid cell is 149 x 76 px; nav tabs are at y 240).
2. Mask everything that is live on the device but fixed in `sample.txt`: tiles, the uptime text at `380 0 100 40`, the GPS values. Without masks `expect-match` fails on data, not on logic.
3. Prefer `expect-match` or `expect-region` for what a screen must look like, `expect-replay` for state built up by several inputs, `expect-changed` as a guard that an input did something.
4. Run with `--fake` first, then on the device.

## Limitations

- The injected input bypasses the touch controller, ADC, calibration and the `homtom-touch` event path. It tests UI logic and drawing, not the panel or the touch hardware. The real-touch path stays covered by `homtom-touch` tests.
- Injection exists only after the image with the modified launcher is booted; before that every tool says so and exits with status 3 without sending anything.
- Actions run for real: a button bound to `reboot`, `backlight` or `cmd` does what it does on a user press. Do not script destructive buttons; `confirm=1` dialogs need a second tap.
- Timing is coarse (whole seconds, bursts); a shot is not synchronised to drawing (`docs/SCREEN.md`), so `settle` at least 1 s, and each step is a few seconds over `homtom-sh`.
- Host references use `sample.txt` data and the host font/blit path; anything driven by live data must be masked.
- A tap during the 2 s respawn window, or with the GUI disabled, is reported as status 4 and not applied.

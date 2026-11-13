# homtom-touch: touch experiment protocol

`homtom-touch.c` is one freestanding ARMv4T tool for the GO 630 resistive panel. Its modes are
raw, scan, cal, draw, drag, slide, targets and events. Nothing here has run on the device yet. Every
statement about the panel below is a hypothesis until a run of this tool confirms it.

## Build

```sh
cd workspace/touch
docker run --rm -v "$PWD":/w -w /w debian:bookworm-slim sh -c '
  apt-get update -qq && apt-get install -qq -y gcc-arm-linux-gnueabi >/dev/null &&
  arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 \
    -o homtom-touch homtom-touch.c'
```

The build needs no libc, memset/memcpy or libgcc. The link fails if any of them is pulled in. To check
the arithmetic without the device:
`docker run --rm --platform linux/arm/v5 -v "$PWD":/w -w /w arm32v5/debian:bookworm-slim ./homtom-touch selftest`
should print `PASS`.

To install it, run `tools/homtom-put workspace/touch/homtom-touch /tmp/homtom-touch`, then
`chmod +x /tmp/homtom-touch` on the device. /tmp is RAM, so repeat after each reboot.

## Most likely cause of the weak readings (check this first)

The stock pin table declares XPON/YPON as `PIN_OPEN_EMITTER` and XMON/YMON/TSDOWN as
`PIN_OPEN_COLLECTOR` (`tomtomgo-iomodena.h:127-131`). `IOP_Deactivate` puts such a pin into
**input mode (floating)** (`tomtomgo-iopins.h:42-43`, `tomtomgo-type.c:1770-1795`). Both earlier
samplers drove the "off" pins to the opposite level instead. In the old `k` mode the detect phase
therefore drove XPON low, XMON high and YPON low. That puts the X plane across the supply and pins the
Y plane, and it fits an idle `a` of 74 that moves only 6-10 counts.

`homtom-touch` floats inactive pins by default. Pin strings are 4 characters for GPG12..15 = XPON XMON
YPON YMON, each `0`, `1` or `z` (float):

| phase | stock default | old k mode (`--legacy`) |
|---|---|---|
| detect (`--det`) | `zzz0`, ADCTSC 0x50, mux 9 | `0100`, 0x50 |
| X (`--px`) | `10zz`, ADCTSC 0x58, mux 5 | `1001`, 0x50 |
| Y (`--py`) | `zz10`, ADCTSC 0x58, mux 9 | `0110`, 0x50 |

With the stock wiring, mux 9 in the detect phase should idle near 1023 (pull-up) and drop well below
100 when pressed.

## Pen-down is self-calibrating

Every mode except scan starts with about a second of **hands off**. The tool learns the idle level of the
detect channel (`base`) and its peak noise. It then sets `margin = max(--margin-min, noise*--mul + 4)`.
A reading counts as pressed when it moves more than `margin` away from `base`, and as released below
`margin/2`. By default either direction counts; `--dir down|up` picks one. While the screen is idle,
the baseline follows slow drift. The `learn ...` line prints all of these values. If you touch the
screen during the learning phase, the baseline is wrong: restart the tool.

## Protocol at the device

Use a stylus or fingernail for every press, and press firmly. For each step, record which command was
run and the lines it printed: copy the shell output, or `/tmp/touch-scan.log` and `/tmp/touch-test.log`.

### 1. Reproduce the old numbers (5 min)

`/tmp/homtom-touch raw --legacy`
Expect the old `ts a/b/c d/e/f g/h/i` layout and roughly the old values (a about 74). This confirms
the tool reads the same channels as the old k mode. Stop with Ctrl-C.

### 2. Stock switching (the main test)

`/tmp/homtom-touch raw`
Watch `det=`, `p=` and `DOWN|up` while you alternately leave the screen alone and press the centre.
- **Good:** idle `det` is near 1023 and drops by hundreds when pressed. `DOWN` follows the finger.
  The middle triple (X phase, mux 5) and the first number of the third triple (Y phase, mux 9) change
  when you press different corners.
- To see it on the LCD without the Mac, add `--kmsg`. It logs on change or every 2.5 s.

### 3. If step 2 is still weak: scan

`/tmp/homtom-touch scan --kmsg`
Follow the prompts: hands off, then press and hold the centre when told, then release. The scan
compares 12 switch configurations across mux 5..9 and marks rows whose idle/pressed delta is 40 or
more with `*`. The marked rows go to the LCD and all rows go to `/tmp/touch-scan.log`. A `det-*` row
with a large delta is a usable pen-down channel. An `x-*`/`y-*` row that changes a lot between two
corners is a usable axis: run scan twice, pressing first at top-left and then at bottom-right, and
compare.

Then retest with the winning configuration, for example:
`raw --det zzz0 --tsc-det 0x50 --mux-det 9`, `raw --tsc 0x58 --ymon 1`, `raw --px 10zz --tsc-x 0x50`.
Useful quick knobs:
- `--xpon/--xmon/--ypon/--ymon 0|1|z` change one pin of the detect phase.
- `--settle 10` waits longer after switching, for the filter capacitors.
- `--udp keep|0..3` sets the GPG12-15 pull bits.
- `--presc 49 --avg 1 --discard 0` reproduces the old ADC timing.

### 4. Calibrate

`/tmp/homtom-touch cal [the same hardware options]`
The console mutes (`/sys/kernel/homtom_splash` = 1). Keep hands off while the screen says
HANDS OFF, then hold each of the 5 crosses until the next one appears. The tool fits integer affine
coefficients, which also handles swapped or inverted axes, and prints the residual of each point.
- A residual above `--maxerr` (15 px) means the result is not saved; repeat the run. `--force`
  saves it anyway.
- The result is written to `/mnt/internal/homtom/touch.cal` only when
  `/mnt/internal/ttsystem.lab2613` exists, so it lands on INTERNAL and not on the SD card. A copy
  always goes to `/tmp/touch.cal`.
- File format: line 1 is `A B C D E F S 480 272` (tslib pointercal order, `sx = (A*x + B*y + C)/S`).
  Line 2 is `opts ...` with the hardware options used, which later modes reload automatically.

### 5. Acceptance tests (roadmap exit criteria)

Each test logs pass/fail lines to `/dev/kmsg` and appends them to `/tmp/touch-test.log`.

- `targets`: tap the 9 squares. PASS means the error is at most `--tol` (12 px). Exit criterion: all
  four corners and the centre pass.
- `draw`: draw a diagonal line. It should be unbroken. Jumps over 40 px are logged. CLR (top left)
  clears the screen and END (top right) finishes.
- `drag`: drag along 3 paths from green to red in one stroke each. PASS needs start and end within
  30 px, 90% progress along the path, deviation at most 2*tol and no jump over 40 px.
- `slide`: drag the slider to the yellow mark and lift. PASS means within 3%.

`--unmute` restores the console text on exit. Otherwise run `echo 0 > /sys/kernel/homtom_splash`.

### 6. Stream

`/tmp/homtom-touch events [--uinput] [--kmsg]` prints `x y down|move|up` on stdout, calibrated
if touch.cal exists. Diagnostics go to stderr. `--uinput` needs `insmod uinput.ko` first.

## Known caveats

- The status loop (`homtom-hw`) also uses the ADC. The tool rereads ADCMUX after each conversion and
  retries if another user changed it. It reports `adc clashes=` on exit, which only shows when a mode
  ends normally, not after Ctrl-C.
- Exit codes: 0 ok, 1 /dev/mem or framebuffer, 2 usage, 3 cal timeout, 4 cal residual too high or
  degenerate, 5 not written (guard missing or write failed), 6 test failed.
- On exit, GPG12-15 are left floating and ADCTSC is set to 0x58, as stock leaves them on suspend.
  raw and events run until killed, so they leave the last phase applied.

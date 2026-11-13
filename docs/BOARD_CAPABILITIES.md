# GO 630 / homtom board capabilities

Live inventory for the Home Assistant product and the modern-kernel ladder.
Measured **2026-11-02** over USB gadget Telnet unless noted.

## Product target

See **`docs/HOMTOM_OS.md`**: own homtom OS from scratch (no maps, no Nano-X).
Bluetooth Classic is the field link; every sensor/radio/audio/power path above
must work. Kernel baseline: **Linux 6.1.y** (last with S3C24xx in-tree; **6.2
removed** the platform).

## Confirmed SoC / OS

| Item | Value | Evidence |
| --- | --- | --- |
| SoC | S3C2443 | Boot/dmesg earlier; platform `s3c-hsudc` |
| CPU | ARM920T, ARMv4T, part 0x920 | `/proc/cpuinfo` |
| Board | TomTom GO, rev `b000a`, type 25 / GO 630 | cpuinfo + `/proc/barcelona` |
| Kernel | 2.6.13 #1 Wed Mar 8 13:28:14 UTC 2023 | `uname -a` |
| RAM | ~62 MiB class | earlier MemTotal |

## Device nodes present (`/dev`, 2026-11-02)

Sensors / radio / audio / power (Barcelona-style):

`acc`, `gacc`, `gyro`, `gyro2`, `compass`, `adc`, `gps`, `battery`,
`barc-snd`, `buz`, `fm2010`, `fmt`, `pwm`, `hwstatus`, `mep`

Display / input: `fb`, `input`, …

Storage: `mtd0`/`mtd1`, `mmcblk0` (+p1…), `mmcblk1` (+p1…), `flash`

Serial: `ttySAC1` (barcelona notes 921600), `ttySAC2`, `ttySAC3`

These nodes mean the **stock 2.6.13 tree already has drivers/userspace hooks**
for motion, GPS, battery, audio codec path, etc. They are not yet a Home
Assistant integration.

## Bluetooth

Modules on disk under `/lib/modules/2.6.13/kernel/drivers/bluetooth/`:
`bfusb.ko`, `hci_usb.ko`, `bcm203x.ko`, `bpa10x.ko`, plus `net/bluetooth/bnep`.

Not loaded at inventory time (`lsmod` showed only `g_ether` + `s3c_udc_hs`).
Transport chip and whether HCI comes up on this PCB are **not yet proven** —
next measurement: load path used by OpenTom (`rc.bluetooth` / barcelona BT
proc), `hciconfig`/`hcitool` if present, dmesg on insert.

## USB / network (dev pipe only)

Gadget ECM via `s3c_udc_hs` + `g_ether`. Host `en9`, device `192.168.1.10`.
Firmware ships hsudc-v9; RAM trials through v13d. Good enough for development;
do not polish further unless the link dies.

## Branding

FAT `splashw` / lowbattery / antitheft on INTERNAL = homtom. Storage-mode /
NOR chrome = still TomTom; the NOR hand logo is now black (R1, see `docs/NOR_PLAN.md`); see
`docs/flash-safety.md`.

## What is already in the repo (do not redo)

| Area | Where |
| --- | --- |
| NOR image format | `firmware/norimage.py` |
| Brand / splash | `brand/` |
| Flash safety / failed write | `docs/flash-safety.md` |
| Emulator roadmap (incl. NOR model) | `docs/emulator-plan.md` |
| USB gadget history | `docs/ROBUSTNESS.md` |
| 2.6.13 build tree | `.runtime/kernel-build`, toolchain under `artifacts/source/` |

## What is not done yet

1. Durable **HA bridge** (BT Classic or USB) and entity map for the sensors above.
2. **Linux 6.1** (or 5.10) board bring-up — no boot artifact yet.
3. Proven BT radio bring-up on this hardware.
4. NOR branding flash (needs programmer / unlock).
5. Update stale lines in `docs/HARDWARE.md` (still calls S3C2443 a hypothesis).

## Suggested order from here

1. Finish BT/sensor smoke on 2.6.13 (what actually returns data).
2. Offline: mainline 6.1 `s3c2443` / `s3c-hsudc` / FB / MMC support map → smallest
   boot plan (serial or FB + initramfs).
3. HA integration design against measured sensors (not against a wishlist).
4. Emulator SoC only as needed to test boot/NOR without bricking.

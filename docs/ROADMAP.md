# homtom roadmap

Goal (docs/HOMTOM_OS.md): own Linux 6.1 + userspace on the GO 630 as a Home Assistant edge node; Bluetooth Classic as the main link; all board functions working; homtom branding. Stock TomTom software is not a goal.

Working rules
- Work the phases in order; do not start a later phase while an earlier exit criterion is unmet, except read-only research.
- Prefer live, reboot-free experiments (devmem, sysfs, loadable modules) over new kernel images. Every new kernel image is a stranding risk: stage it only after `ttsystem.prev` and `ttsystem.lab2613` are on INTERNAL, and verify it boots (tools/homtom-wait LINUX).
- Safety net: INFORM0=5 set at boot plus a watchdog feeder resets a hung unit (storage mode follows only if the Mac is unlocked). The kernel arms the WDT at boot (s3c2410_wdt.tmr_atboot=1, open_timeout=90; untested against a real early hang). Unattended runs may stage new ttsystem only with the Mac unlocked and kept awake (caffeinate), keeping ttsystem.prev images; changes are staged when the user is present.
- Record every lesson in docs/LESSONS.md as it happens. Keep a dated log of unattended runs (time, action, result).
- Commit small; keep personal data (usernames, paths, serials, addresses) out of commits, logs and issues; do not change host system settings.
- Every ~3 completed steps, or after any kernel/driver change, do a review pass: drift against this roadmap, code review of the diff, open bugs. Act on the findings or note why not.

## Phases and exit criteria
0. Dev loop [done]: boot, USB network, root shell, reboot into storage mode, stage and eject, state tooling.
1. Resilience: INFORM0=5 + watchdog at boot; boot-failure recovery script (restage prev/lab image from storage mode); logs persisted to INTERNAL (/mnt/internal/homtom/logs). Exit: a deliberately hung unit recovers to storage mode on its own.
2. Storage: SD slot (legacy SDI, NO NAME) working; INTERNAL mounted at boot. Exit: both cards readable from Linux.
3. Hardware map: stock-source survey of pins/devices (BT, GPS, sensors, ADC/battery, audio, touch, backlight). Exit: docs/HARDWARE_MAP.md with evidence per item.
4. Display and touch: backlight control, framebuffer or fbtext splash, touchscreen events. Exit: touch coordinates readable in userspace.
5. Power: battery voltage/charge, power key events, clean poweroff semantics. Exit: values readable, key handled.
6. Sensors: accelerometer/gyro/compass/ADC, GPS UART. Exit: raw samples readable.
7. Bluetooth: identify chip/transport, bring up HCI, pair with the Mac (MacBook Bluetooth as test peer). Exit: Mac sees and connects to the device.
8. Home Assistant link: small client over BT/USB network publishing sensor data. Exit: entity visible in HA (or mock HA on the Mac).
9. Cleanup: remove debug hooks, final cmdline, full backup, tested recovery, docs.

## Touch toolkit (added 2026-11-07, user request)
One reusable freestanding tool `homtom-touch` (tools/devtools, builds with the other ARMv4T tools), on top of an earlier sampler draft (workspace/homtom-ts-v2.c):
- `raw`: print raw readings; `cal`: 5-point crosshair calibration drawn straight into the LCD framebuffer (mmap /dev/mem 0x33F00000, RGB565 480x272, console muted with /sys/kernel/homtom_splash), writes /mnt/internal/homtom/touch.cal (integer affine coefficients, checked against INTERNAL's lab2613 guard).
- `draw` (paint trail), `drag` (follow a path), `slide` (slider bar), `targets` (tap squares) with pass/fail written to the kernel log and to a log file; `events`: calibrated "x y down/up" stream for HA and a later uinput/evdev path.
- Exit: after calibration, taps land within ~10 px of the targets on all four corners and the centre, and the draw test shows an unbroken line for a diagonal drag.
Needs the user at the device for each calibration/test run.

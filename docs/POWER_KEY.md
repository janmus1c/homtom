# Power key daemon (homtom-keyd)

Status: written and host-tested, **not on the device**. Kernel k3 (`docs/KERNEL_K3.md`, patch 0005) turned the power key
into the evdev device `homtom-keys` (`/dev/input/event0`, `KEY_POWER` 116) and removed the kernel's own 2 s power-off. Until
this daemon runs, the key does nothing in Linux.

| File | Role |
| --- | --- |
| `tools/devtools/homtom-keyd.c` | Freestanding ARMv4T daemon: finds the device, polls, runs actions, logs to `/dev/kmsg` |
| `tools/devtools/homtom-keyd-logic.h` | The timing and safety rules, pure C (no syscalls, no division); shared by the device build and the host test |
| `tools/devtools/tests/keyd_test.c` | Host test with fake event streams |
| `userland/homtom-keyd-start` | busybox ash launcher: `homtom-lock keyd`, respawn loop, `INTERNAL:/homtom/keyd.disable` switch |
| `userland/homtom-poweroff` | `sync`, unmount INTERNAL, INFORM0 = 1, SWRST |

## What the user sees

| Gesture | Result |
| --- | --- |
| Press and release in under 0.8 s | `key short`: the screen action bound with `on key=short ...` in `ui.conf` (screen lock/unlock) |
| Release between 0.8 s and 1.5 s | Nothing (a dead zone so that a slow tap is never read as a long press) |
| Hold 1.5 s to under 5 s, then release | `key long`: the action bound with `on key=long ...` (System hub) |
| Hold reaches 5 s | `key hold5` goes to the GUI (hint: "release to power off"). Nothing else happens yet |
| Release between 5 s and 10 s | **Power off** (`homtom-poweroff`, runs on the release) |
| Hold reaches 10 s, still pressed | `key hold10` goes to the GUI, then an emergency reboot to the storage window via `/bin/reboot` (INFORM0 = 5). Works when the GUI is frozen. With USB on an unlocked Mac, INTERNAL appears; eject it to boot Linux. The release afterwards does nothing |

History: the first design rebooted at 5 s and powered off at 10 s, so every hold of 5 s or more rebooted and power-off was
unreachable (user-reported finding on the live unit). The order is now power off on release in 5-10 s, reboot at 10 s.

The daemon writes `key short`, `key long`, `key hold5` and `key hold10` to `/tmp/ui.in` (the GUI's injection pipe, created
by `homtom-ui-start`). A GUI that does not know `hold5` / `hold10` ignores them; the current GUI shows a hint card for them, also on a locked
screen, and swallows a `key short`/`key long` that follows (docs/UI_CONFIG.md, "Power key and screen lock"). When the GUI
is not running (or frozen with nobody reading) the lines are dropped silently: the write is non-blocking and the 10 s reboot
does not wait for it.

## Safety rules (all in `homtom-keyd-logic.h`, covered by the host test)

- Auto-repeat (value 2) and duplicate edges are ignored.
- A press that ends within 30 ms is contact noise: no action. A down edge within 30 ms of the previous release is ignored
  together with its release.
- A key found down when the daemon starts is ignored until it is released (so a key held through boot cannot trigger the
  hint, the power-off or the reboot).
- A key held 30 s is stuck: no further action, and the release does nothing.
- One release action per press (short, long or power off, never two). The hold marks each fire at most once per press: hint
  at 5 s, reboot at 10 s.
- A hold mark noticed more than 2 s after its deadline (daemon stalled, clock jump) is dropped and the press ignored, so a
  stalled daemon never powers off or reboots on stale timing.
- Times come from `CLOCK_MONOTONIC` in `u32` milliseconds and every difference is taken modulo 2^32: the 49-day wrap and
  `settimeofday` (the bridge sets the clock after a reboot) do not matter.

Log lines in `dmesg` (all start with `homtom-keyd:`): `watching /dev/input/eventN`, `short press`, `long press`,
`hold 5 s: release to power off`, `released after 5 s: power off`, `hold 10 s: emergency reboot to the storage window`,
`key down at start, ignored until released`, `no GUI pipe, dropped key ...`.

## Power-off: which INFORM0 value, and the evidence

`homtom-poweroff` writes **INFORM0 = 1**, then the same SWRST as `/bin/reboot` (`devmem 0x4C000044 0x533C2443`). It
deliberately does not use 0, which was suggested as the default, because the docs and the stock sources give 1 stronger
support:

| Evidence | Source |
| --- | --- |
| Stock names: `PWRST` 0, `NEUTRAL` 1, `OFFRST` 3, `WDTRST` 5, `SFTRST` 9 | `.runtime/kernel-build/include/asm-arm/arch-s3c2410/tomtomgo-wake.h:16-21` |
| Stock `arch_reset` (the reboot path) writes `NEUTRAL` = 1 then `s3c2443_cpu_reboot` (SWRST) | `.runtime/kernel-build/arch/arm/mach-s3c2410/system.c:68-82` |
| Stock suspend/power-off writes `OFFRST` = 3 and then sleeps (no SWRST). Value 3 is **never** written by this project | `tomtomgo-wake.c:215-227`; `docs/LESSONS.md:42` |
| Stock simulated wake and soft reset write `SFTRST` = 9 ("Reboot") | `tomtomgo-wake.c:185-199` |
| Measured: with INFORM0 = 1 left by the stock-style reset, TTBL took the "Power down" branch and the unit stayed off ("`reboot -f` currently powers the unit OFF"; "anything else (we left 1) is Power down") | `docs/LESSONS.md:30,41`; TTBL disassembly at 0x30b03888 / 0x30b0acf4 |
| Measured: TTBL itself resets INFORM0 to 1 on every boot, so 1 is the value it leaves behind and the one every observed "Power down" used | `docs/LESSONS.md:57` |
| Measured: a watchdog reset with INFORM0 != 5 (a hung test image) left the unit dark for 15+ minutes until the power key was pressed | `docs/LESSONS.md:97` |

Value 0 (`PWRST`) is only a name in the stock header; no run here has used it, and TTBL's handling of it has not been
observed. 1 has been observed to give "Power down" twice and is TTBL's own resting value. The value is a single constant in
`userland/homtom-poweroff`; changing it needs a bootloader-side observation first, and it must never become 3, 5 or 9.

### After a power-off: what wakes the unit

- Documented: TTBL's "Power down" branch sleeps **until the power key is held** (`docs/LESSONS.md:41`, `kernel/homtom/mach-homtom-go630.c:351`).
- The unit is normally on USB power, and the evidence says USB power does not restart it: the project was on USB when `reboot -f`
  left INFORM0 = 1 and the unit stayed off, and the hung-image test above sat dark with the Mac attached for 15+ minutes.
  So the 5-10 s release is a real power-off even on USB, not a disguised reboot. Do not expect the screen to light or storage mode
  to appear after it.
- Not verified: whether a fresh USB plug-in wakes the unit from this state. The stock wake code recognises ACPWR/ONOFF/USB/CDSD as
  wake sources in its own sleep path (`tomtomgo-wake.c:93`), but TTBL after SWRST is a different path and nothing here
  has plugged a cable into a powered-down unit. Test it (below) before telling anyone either way.
- Wake with the power key (press and hold until the screen lights). The recovery tools need the same key press.

### Why the release

TTBL's power-down branch "sleeps unless the power key is held" (`docs/LESSONS.md:41`). A 5-10 s hold is still pressing the
key when the script finishes `sync` and `umount` and fires SWRST, so a reset at that moment would see the key held and
boot on. The daemon therefore runs `homtom-poweroff` on the release. Holding on to 10 s switches to the emergency reboot
instead, so the key cannot be held "too long" for the power-off to matter: the reboot path has INFORM0 = 5 and does not
care whether the key is down.

## Integration (not applied: `kernel/scripts/build-ttsystem-6.1.sh` is yours)

1. Add `homtom-keyd` to the devtools copy list at line 31 (`for t in ... gpsaid homtom-keyd; do cp ...`) and to the `cp
   /out/... "$ROOTFS/bin/"` list at line 64.
2. Add `/out/homtom-keyd-start /out/homtom-poweroff` to the userland copy list at line 63 (the scripts must be mode 755).
3. In `init.sh`, next to the other starters (after line 195, `( sleep 15; homtom-ui-start ) ...`):

```sh
( sleep 14; homtom-keyd-start ) > /dev/null 2>&1 < /dev/null &
```

   14 s puts it just before the GUI starter, so `/tmp/ui.in` usually exists when the first key event arrives; the daemon does
   not care about order (a missing pipe drops the line). Every applet it needs (`sync`, `umount`, `sleep`, `touch`) is already
   in `kernel/scripts/busybox-applets.txt`; `devmem` and `homtom-lock` are in `/bin`.
4. `/bin/reboot` stays the devmem wrapper; `/bin/poweroff` and `/bin/halt` remain symlinks to it (they reboot). The new
   `/bin/homtom-poweroff` is the only real power-off. Do not point `poweroff` at it unless you want `poweroff` typed over SSH to
   switch the unit off with nothing to wake it remotely.

## Build and tests

- Device binary: `tools/devtools/build.sh homtom-keyd` (`readelf -A`: `Tag_CPU_arch: v4T`; the disassembly has no
  instruction above ARMv4T and no libgcc call; the link would fail on a missing `__aeabi_*`).
- Host test: `cc -O1 -Wall -Wextra -o /tmp/keyd_test tools/devtools/tests/keyd_test.c && /tmp/keyd_test`. 252 cases:
  short (100, 799 ms), the 800 and 1499 ms dead zone, long (1500, 4999 ms), 20 ms noise, the hint once at 5 s, power off on
  release at 5000, 7000 and 9999 ms, reboot once at 10 s, release after the reboot (10.4 s and 15 s) does nothing, no power
  off from a release at 10 s or later, repeats (value 2), duplicate and orphan edges, 30 ms bounce (including a bounced press
  that must not grow into a hold), key down at start, stuck key, stalled daemon (late hint, late reboot), fresh state after
  each action, `kd_timeout`. Every scenario runs at six clock bases, three of them straddling the 2^32 wrap.
- Not covered by the host test: `/proc/bus/input/devices` parsing, the syscalls, fork/exec and the FIFO write (device only).

## What to test on the device

User present, USB attached, Mac unlocked (the reboot step lands in storage mode). Stage the image with the integration lines.

1. `pidof homtom-keyd`; `dmesg | grep homtom-keyd` shows `watching /dev/input/event0` (or the number in `/proc/bus/input/devices`).
2. Tap: the `on key=short` action (screen lock/unlock) runs. Hold 2 s and release: the `on key=long` action (System hub). Hold
   1 s and release: nothing.
3. Hold to 5 s while watching `dmesg` / `/tmp/ui.in` consumers: `hold 5 s: release to power off` and the GUI receives
   `key hold5` (the GUI shows `Release to power off`). Release at 6-8 s: `released after 5 s: power off`, `/mnt/internal`
   unmounts, the unit goes dark and stays dark with the key up. Then: does it stay dark on USB; does the power key wake it;
   does re-plugging USB wake it. Record all three in `docs/LESSONS.md`. (This is also the first live test of INFORM0 = 1.)
4. Hold to 10 s without releasing: `key hold10`, `hold 10 s: emergency reboot ...`, the unit resets into the storage window;
   eject INTERNAL to boot Linux. Repeat with the GUI frozen (`kill -STOP` the GUI) to confirm the reboot needs no GUI.
5. Hold the key while booting, release after the GUI is up: nothing happens (`key down at start, ignored until released`).
6. Without the GUI (`touch /mnt/internal/homtom/gui.disable`, reboot): a tap logs `short press` and `no GUI pipe, dropped
   key short`; the daemon stays alive; a 5-10 s hold still powers off.
7. Stuck-key behaviour is host-tested only; do not hold the key for 30 s on the device without a reason.

Never write INFORM0 = 3 by hand while testing; `devmem 0x4C000070` reads are safe.

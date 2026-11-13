# Kernel k3: stock reset, power key input, panic on the LCD

Status: built, **not booted**. Nothing below has run on the device. k3 sits on top of the k2 patches (0001-0003), which
are not booted either, so the first boot of ladder-k3 also tests k2.

| Item | Path |
| --- | --- |
| Patches (apply with `-p1` in order after 0001-0003) | `kernel/patches/0004-s3c2443-stock-reset.patch`, `0005-power-key-input.patch`, `0006-panic-lcd.patch` |
| Kernel tree | `.runtime/linux-trim-k3` (APFS clone of `.runtime/linux-trim-k2` plus the three patches) |
| Kernel build | `artifacts/kernel-builds/homtom-6.1-k3/` (`zImage`, `System.map`, `.config`, `SHA256SUMS`, `kernel/build.log`) |
| Modules | `artifacts/modules-k3/` (built against the k3 tree; `CONFIG_SHA` matches the k3 `.config`) |
| Image | `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.ladder-k3` + `.sha256` (userland from the current working tree) |

Image SHA-256: `c8221e4ed74f3e0d850a819ebe8399e3938d94a53f00e7a47ac92a6a49d25b0f`.
zImage SHA-256: `9bf63bd0e2cec4c23ae6b21793f21250b16378da16ae67356cc713d548d62713` (3,127,064 bytes; k2 was 3,109,632).

Config delta against k2: `KEYBOARD_GPIO_POLLED=y`, `PSTORE=y`, `PSTORE_RAM=y`, `PSTORE_CONSOLE=y`, all pstore
compressors off, `REED_SOLOMON*` (selected by ramoops), `PANIC_TIMEOUT=10` (was 0). Nothing else changed.

Hard rules held: INFORM0 is only ever written as 5; GPJ10/GPJ14 and GPL0-9 are not touched by the new code; EPLLCON is
not written and the EPLL clock selection is not changed.

## 0004: stock S3C2443 reset sequence

`homtom_restart` (machine `.restart`, reached by `reboot(2)`, panic reboot and `pm_power_off`'s fallback) now does what
the stock 2.6.13 `s3c2443_cpu_reboot` does, in `arch/arm/mach-s3c/homtom-reset.S`:

1. IRQ and FIQ off; INFORM0 (0x4C000070) = 5.
2. Watchdog armed as a backup: prescaler 255, /128, count 0x800 (about 1 s at full PCLK; 10-90 s once the clock is down).
3. `setup_mm_for_reboot()` switches to the identity map.
4. Clean and invalidate the caches (`arm920_flush_kern_cache_all`), SDRAM REFRESH (0x48000010) = 0x2e, CLKSRC bit 4
   cleared (MSYSCLK from the 12 MHz crystal), drain the write buffer, invalidate TLBs and caches.
5. Call the `.idmap.text` routine `homtom_do_reboot` at its physical address twice: once with Z clear (only fetches its
   32-byte cache line), then with Z set: MMU off, SWRST (0x4C000044 = 0x533c2443), spin.

The routine is 24 bytes inside one cache line at 0xc00084a0 (PA 0x300084a0); disassembly of the built vmlinux matches
the stock code instruction for instruction except for the trial-run flag (`teq r8, #0` instead of `teq pc, #0`, which
current binutils rejects).

Deviations from stock: CLKSRC is read-modify-written (bit 4 only) instead of written as 0, so the EPLL selection stays as
it is; INFORM0 is 5 instead of stock's 1, because this TTBL treats anything but 5/9 as "Power down".

`/sys/kernel/homtom_reboot_mode` (write-only) selects variants for A/B testing; all write INFORM0=5:

| Mode | Path |
| --- | --- |
| 0 (default) | stock sequence above |
| 1 | watchdog only (about 8 ms) |
| 2 | direct SWRST, then watchdog: the k2 behaviour |

## 0005: power key as an input device

- The 2 s hold → `kernel_power_off()` code is gone. The kernel takes no action on the key. Userland is meant to decide
  (short press = wake/UI, 5 s = reboot to storage, 10 s = power off); **no userland handler exists yet**, so until one
  ships the key does nothing in Linux.
- New platform device `gpio-keys-polled` named `homtom-keys`: one button, `KEY_POWER` (116), GPF0, active low, polled
  every 50 ms, 50 ms debounce. Events on `/dev/input/eventN`.
- Polled on purpose: an interrupt-driven gpio-keys would switch GPF0 to EINT0 function mode, where the S3C24xx manual calls
  GPFDAT reads undefined, and statusd reads `power_key` from GPFDAT. As a plain input GPF0 stays readable.
- The 1 Hz liveness beacon ("homtom: alive jiffies ...") keeps its cadence.

## 0006: oops/panic on the LCD and a RAM last-log

- Muted console (GUI running): the first console write during an oops clears the screen, starts at the top and turns the
  panel on (TPAL off, GPB0 output high = backlight full, LCDCON1 ENVID on).
- A kmsg dumper (oops and panic) copies the newest 16 KiB of the log into a static buffer, finds the first line of the
  latest incident (`Unable to handle kernel`, `Internal error:`, `kernel BUG at`, `BUG:`; else the `Kernel panic` line),
  redraws the 60x34 screen from there without timestamps, pins the `Kernel panic` line to the bottom row and freezes the
  console. No allocation, no locks.
- `CONFIG_PANIC_TIMEOUT=10`: a panic now restarts after 10 s through the 0004 path (INFORM0=5, so the storage window
  follows). Before, a panic hung until the watchdog (tmr_atboot) fired.
- pstore/ramoops at PA 0x33E00000, 64 KiB, reserved at boot: two 16 KiB oops/panic records plus a 16 KiB console ring,
  uncached, no compression, no ECC.

## Risks

- **The cause of the k2 in-kernel reboot failure was never found.** 0004 addresses one theory (reset while SDRAM traffic is
  in flight). If the cause is a `device_shutdown` hook (sdhci, hsudc, wdt), 0004 does not fix it; modes 1 and 2 help to
  tell the cases apart. The userland `/bin/reboot` script (devmem) stays the default reboot and is unaffected.
- A wrong step after the clock drop leaves the CPU spinning until the backup watchdog fires, which can take up to about
  90 s at the slow clock. Both end in INFORM0=5, so the result should still be "Reboot".
- Kernel change: a k3 image that fails before userspace can only come back through the watchdog path (INFORM0=5 is
  written in `map_io`) or the power key. Stage only with the user present and an unlocked Mac (storage mode); keep
  `ttsystem.lab2613` and the previous image on INTERNAL.
- Panics now reboot after 10 s; the LCD text is visible only for those 10 s. Photograph quickly or read pstore afterwards.
- After a non-fatal oops the LCD console stays frozen on the oops (as before when the screen filled), and the GUI keeps
  drawing over it because userspace keeps running.
- At an oops/panic the backlight is forced to full and the PWM backlight setting is lost until `homtom-bl` runs again.
- pstore survives a reset only if TTBL and the zImage decompressor leave 0x33E00000-0x33E0FFFF alone; this is untested.
  ramoops rejects a region whose headers do not check out, so the failure mode is "no records", not garbage.
- 64 KiB less RAM; 50 ms key polling (20 wakeups/s; the removed poller ran at 10/s, the beacon now at 1/s).
- With the in-kernel 2 s power-off removed, `poweroff` from userland and the bootloader are the only ways to switch off
  until a userland key handler exists.

## Device test steps

Prerequisites: user present, Mac unlocked, USB attached. Stage with `tools/homtom-stage artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.ladder-k3`
(keeps `ttsystem.prev`; retry once on "Permission denied"), then reboot to it with the userland `reboot`. Verify the
SHA-256 against `ttsystem.ladder-k3.sha256` on INTERNAL before ejecting.

### 1. Boot

- `cat /proc/version`, `dmesg | grep -e homtom -e ramoops -e pstore -e input:`: expect `input: homtom-keys as ...`,
  `ramoops: using 0x10000@0x33e00000`, the alive beacon, and the k2 lines (RTC, I2C, hsudc).
- `cat /proc/bus/input/devices`: a `homtom-keys` block with `H: Handlers=kbd eventN`.

### 2. Reboot through the kernel (0004)

`/bin/reboot` in the initramfs is the devmem script and ignores `-f`; to reach `machine_restart` call BusyBox directly:

1. `cat /sys/kernel/homtom_reboot_mode` is not readable; the default after boot is mode 0.
2. `sync; umount /mnt/internal; busybox reboot -f`
3. Expect the unit back in storage mode (INTERNAL/NO NAME on the Mac) within a few seconds; eject INTERNAL to boot Linux.
   A return after 10-90 s means the backup watchdog fired, not SWRST: record which.
4. Repeat 3 times. If mode 0 fails, try `echo 2 > /sys/kernel/homtom_reboot_mode; busybox reboot -f` (k2 path) and
   `echo 1 > ...` (watchdog only) and record each result; that separates "SWRST timing" from "shutdown hooks".
5. If the unit stays dark, hold the power key; record it in LESSONS.md as a failure of the given mode.

### 3. Power key events (0005)

1. Find N: `grep -A4 homtom-keys /proc/bus/input/devices`.
2. `cat /dev/input/eventN > /tmp/ev & sleep 1; wc -c /tmp/ev` (0 bytes).
3. Press and release the key once, then `wc -c /tmp/ev`: expect 64 bytes (each event is 16 bytes: key down + SYN,
   key up + SYN). `kill %1`.
4. Hold the key for 3 s while watching uptime: the unit must not power off or reboot (the old 2 s kernel action is gone).
5. While holding, `curl http://192.168.1.10:8080/` from the Mac: `power_key` must read 1 (GPF0 is still a plain input).

### 4. Forced panic (0006)

SysRq is built in (`MAGIC_SYSRQ=y`, all functions enabled); `/proc/sysrq-trigger` works regardless of the mask.

1. With the GUI running (console muted), `sync; umount /mnt/internal`, then `echo c > /proc/sysrq-trigger`.
2. Expect within a second: backlight full, screen cleared, white text starting at `Kernel panic - not syncing: sysrq
   triggered crash` followed by the stack trace. Photograph it (`tools/homtom-screen` will not work: userspace is stopped).
3. After 10 s the unit restarts through the 0004 path into storage mode; eject INTERNAL to boot Linux.
4. After boot: `mkdir -p /sys/fs/pstore; mount -t pstore pstore /sys/fs/pstore; ls /sys/fs/pstore`. Expect
   `dmesg-ramoops-0` (the panic) and `console-ramoops-0` (the previous boot's console). No files means the region did not
   survive TTBL; record that. Copy them to `/mnt/internal/homtom/logs/` and delete them from pstore to free the record.
5. An oops (rather than a direct panic) has no safe trigger in this image (no LKDTM); the oops path is exercised only by a
   real fault.

Record every result in `docs/LESSONS.md` with the image name `ladder-k3`.

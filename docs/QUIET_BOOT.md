# Quiet boot: splash from the bootloader to the UI

Status: kernel side built, **not booted**. The init.sh and GUI parts below are proposals; they are not in any image yet.
Nothing in this document has run on the device.

Goal: after power-on the screen shows the bootloader's FAT splash (`SPLASHW.BMP`) without a break until the UI draws
its first frame. No coloured noise, no scrolling console. On any failure the log appears on the LCD.

| Item | Path / value |
| --- | --- |
| Kernel patch (apply with `-p1` after 0001-0006) | `kernel/patches/0007-quiet-adopt-framebuffer.patch` |
| Kernel tree | `.runtime/linux-trim-k4` (APFS clone of `.runtime/linux-trim-k3` plus 0007) |
| Kernel build | `artifacts/kernel-builds/homtom-6.1-k4/` (`zImage`, `System.map`, `.config`, `SHA256SUMS`, `kernel/*.log`) |
| Modules | `artifacts/modules-k4/` (`CONFIG_SHA` = `61983a8b...`, identical `.config` to k3) |
| Test image | `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.ladder-k4` + `.sha256` (userland from the current working tree, init.sh unchanged) |

- Image SHA-256: `825a8a61de0cd77751b211130883783887a7f1102d66c328c6d35dab0c742028`
- zImage SHA-256: `540478f2c43fe51084e641bb50e65324116375aea2a3d88742b920ac570467da` (3,127,864 bytes; k3 3,127,064)
- `.config` is byte-identical to k3. ladder-k4 contains k2 and k3, which have not been booted either (`docs/KERNEL_K3.md`).

Hard rules held: INFORM0 is only written as 5 (unchanged, in `homtom_map_io`); no INFORM0=3; GPJ10, GPJ14, GPL0-9 and
EPLL are not touched; nothing is added to `head.S` (neither the decompressor's nor the kernel's); no colour ladder.

## 1. Root cause of the noise burst

Tags: [V] verified from sources, the build outputs or the bootloader disassembly; [H] hypothesis, not yet measured on the
device.

Correction to the brief: the bootloader does **not** scan out of 0x33F00000. TTBL's buffer is at 0x30400000; 0x33F00000 is
where our kernel moves the LCD DMA.

1. [V] TTBL draws the splash at physical 0x30400000-0x3043FBFF (480x272 RGB565, 261,120 bytes). Evidence: the
   bootloader's data holds 0x30400000 twice next to its default geometry (`workspace/bootgfx/bootloader.dis`, words at
   0x30b354cc/0x30b354d0 after 320/240) and as `lfb_base` of its ATAG_VIDEOLFB template (0x30b350f0, tag 0x54410008);
   the 2026-11-06 crash photos put the end of the buffer at 0xC043FC00 (`kernel/homtom/COLOR_LADDER.txt`, root cause entry).
2. [V] The decompressor inflates the kernel Image to 0x30008000. The k4 Image is 6,587,540 bytes, so it covers
   0x30008000-0x30650494 and bss runs to 0x30674048 (`System.map`: `_edata` c0650494, `_end` c0674048). The bootloader
   buffer lies inside `.text`: VA 0xC0400000-0xC043FBFF holds `udp4_hwcsum` ... `memcpy`. While the LCD keeps scanning
   0x30400000, gunzip writes ARM machine code into it: that is the coloured noise. It starts when the output reaches
   about 62 % of the Image (0x3F8000 of 0x648494 bytes) and stays until the kernel moves the DMA.
3. [V] No other code touches the LCD before that: the k3 decompressor has no LCD register literal at all (disassembly of
   `arch/arm/boot/compressed/vmlinux`, no 0x4D0000xx/0x4C8000xx), and the colour-ladder macros in both `head.S` files are
   `.ifdef HOMTOM_COLOR_LADDER` (not defined). The kernel's `__mmap_switched` clears bss only (0x30650494-0x30674048).
   Init memory is never freed or poisoned (homtom change in `arch/arm/mm/init.c`), and the debug drawing in `setup.c`
   is off (`HOMTOM_DBG_DRAW 0`).
4. [V] `homtom_reserve()` (machine `.reserve`, from `arm_memblock_init()` in `setup_arch`) rewrote LCDSADDR1/2 to
   0x33F00000 unconditionally. That buffer was never filled first, so the panel showed stale RAM (random after a cold
   start, the last GUI frame after a warm reset) until `homtom_fbtext_init()` ran after `paging_init()`.
5. [V] `homtom_fbtext_init()` was verbose by default (the forced `CONFIG_CMDLINE` has no `homtom.quiet`): it cleared the
   screen to black and replayed the whole log (`CON_PRINTBUFFER`), then scrolled every printk until the GUI muted it.
6. [H] Visible duration: the noise of step 2 lasts for the last ~38 % of the gunzip plus cache flush and early
   `setup_arch`, a few hundred milliseconds on this CPU; step 4 lasts a few milliseconds (one LCD frame at most). A
   slow-motion video will tell.

Minimum change, chosen because the frame cannot survive step 2 otherwise: the frame must leave 0x30400000 **before**
gunzip writes there, and only the decompressor runs then. Anything later (board code, fbtext) can only redraw a frame that
is already destroyed.

## 2. What 0007 changes

Decompressor, `arch/arm/boot/compressed/misc.c` (C, `CONFIG_MACH_TOMTOMGO` only), at the top of `decompress_kernel()`:

- Reads LCDCON1/LCDSADDR1/LCDSADDR2. If the LCD is on (ENVID) and scans a valid buffer elsewhere, copies that frame to
  0x33F00000, cleans those D-cache lines by address (`mcr p15,0,Rn,c7,c10,1`; caches are on, VA = PA) and drains the
  write buffer, then writes LCDSADDR1/2 to the copy. LCDSADDR3 (stride) is not touched. Both buffers hold the same
  pixels, so the switch cannot be seen even if it lands mid-frame.
- Skipped (nothing written) when ENVID is 0, the LCD already scans 0x33F00000, the size is 0 or over 256 KiB, the source
  is outside 0x30000000-0x33FFFFFF or overlaps the target, or the decompressor's heap end reaches 0x33F00000.
- Verified in the built decompressor: constants 0x33F00000, 0x19F80000, mask 0x3FFFFE, bound 0x33F3FFFF, the
  clean loop and the two register stores, all inside `decompress_kernel`.

Board file, `arch/arm/mach-s3c/mach-homtom-go630.c`:

- `homtom_reserve()`: if LCDSADDR1 already equals 0x33F00000 >> 1, sets `homtom_fb_adopted` and writes no LCD register.
  Fallback (decompressor skipped): a black TPAL overlay masks the stale buffer, then the DMA moves as before.
- `homtom_map_io()` no longer clears TPAL (only ladder builds do); `homtom_fbtext_init()` clears it after drawing.
- `/sys/kernel/homtom_splash` is now readable: `1` muted, `0` text.

Console, `arch/arm/kernel/homtom-fbtext.c`:

- Quiet by default. The console is muted before `register_console()`, so the replay of the early log draws nothing.
  With an adopted frame not one pixel is written. Without (fallback), the built-in splash is drawn; it is
  pixel-identical to `SPLASHW.BMP` after RGB565 truncation (checked: 0 of 130,560 pixels differ).
- `homtom.verbose=1` on the kernel command line restores the old verbose console. `homtom.quiet` is no longer parsed.
- `echo 1 > /sys/kernel/homtom_splash` while muted draws nothing, so the GUI's mute at start no longer repaints the
  splash. `echo 0` unmutes under `console_lock`, clears the screen, draws the newest log lines that fit (timestamps
  stripped) and continues with live text.
- Boot guard: if nothing has written `homtom_splash` 60 s after boot, the console unmutes itself and prints
  `homtom: boot incomplete: no userspace after 60 s, console unmuted`. A hard hang (interrupts off) still ends only in
  the hardware watchdog reset (`tmr_atboot`, `open_timeout=90`).
- Boot log line for the device check: `homtom: LCD console muted, bootloader frame adopted` (or `not adopted`, or
  `verbose`).

Expected boot sequence with ladder-k4:

| Phase | Screen |
| --- | --- |
| TTBL | black NOR logo (R1), then the FAT splash |
| Decompressor | frame copied, DMA moved; gunzip overwrites 0x30400000 unseen |
| Kernel | nothing drawn; at `machine_init` GPB0 becomes output high (backlight full) |
| init, GUI start | GUI writes `1` (no change), then blits its first frame |

## 3. Failure paths

### a. Oops and panic (kernel, unchanged by 0007)

Order, from `kernel/panic.c` and `arch/arm/kernel/traps.c`: `console_verbose()`, `bust_spinlocks(1)`
(`oops_in_progress` > 0), then the first console line. `fbt_write()` sees muted + `oops_in_progress`: clears the screen,
TPAL off, backlight full, ENVID on, draws. Then `kmsg_dump(KMSG_DUMP_PANIC)` (or `_OOPS` from `oops_exit()`) redraws from
the first line of the incident and freezes the console. The quiet default only changes how `fbt_quiet` gets set, not
these paths, so they behave as in k3. A panic restarts after 10 s (`CONFIG_PANIC_TIMEOUT=10`, INFORM0=5). A panic before
`homtom_fbtext_init()` (inside `setup_arch`) still draws nothing, as before.

### b. init supervision (proposal for `kernel/scripts/build-ttsystem-6.1.sh`)

The init.sh heredoc sits inside `bash -c '...'`: none of these lines may contain a single quote (they do not). Tested
under BusyBox ash (Debian busybox): `boot_fail` prints the banner and the last 10 lines of `/tmp/gui.log`, the
watchdog stays silent with `/tmp/gui.ready`, reports a missing GUI, and unmutes with a message for `gui.disable`.

**1. After** the line `k() { echo "homtom-init: $*" > /dev/kmsg 2>/dev/null; }` add:

```sh
# Quiet boot (docs/QUIET_BOOT.md): the kernel keeps the bootloader splash and mutes the LCD console.
T0=$(cut -d. -f1 /proc/uptime)
# unmute REASON: say why, then show the log on the LCD (the kernel redraws the newest lines, then prints live).
unmute() { echo on 2>/dev/null > /proc/sys/kernel/printk_devkmsg; k "$*"; echo 0 2>/dev/null > /sys/kernel/homtom_splash; }
# boot_fail REASON: banner and the tail of the GUI log on the LCD, full dmesg kept on INTERNAL when mounted.
boot_fail() {
	echo on 2>/dev/null > /proc/sys/kernel/printk_devkmsg
	k "==== boot incomplete: $* ===="
	if [ -f /tmp/gui.log ]; then
		n=$(wc -l < /tmp/gui.log); s=$((n - 9)); [ $s -lt 1 ] && s=1
		sed -n "$s,\$p" /tmp/gui.log | while read -r l; do k "gui: $l"; done
	fi
	{ echo "---- $(cat /proc/uptime) boot incomplete: $*"; dmesg; } > /tmp/boot-incomplete.log 2>&1
	[ -d /mnt/internal/homtom/logs ] && cp /tmp/boot-incomplete.log /mnt/internal/homtom/logs/boot-incomplete.log 2>/dev/null && sync
	echo 0 2>/dev/null > /sys/kernel/homtom_splash
}
# gui_up: the GUI wrote /tmp/gui.ready after its first frame, or (GUI without the marker) the same GUI process is still
# alive 10 s later.
gui_up() {
	[ -f /tmp/gui.ready ] && return 0
	p=$(pidof homtom-gui-cfg homtom-gui)
	[ -n "$p" ] || return 1
	sleep 10
	[ "$(pidof homtom-gui-cfg homtom-gui)" = "$p" ]
}
# Boot watchdog: 45 s after init started, but never sooner than 30 s after the UI launcher is scheduled (the network and
# INTERNAL waits before it can take up to ~40 s).
boot_watchdog() {
	ui0=$(cut -d. -f1 /proc/uptime)
	w=$((T0 + 45)); [ $((ui0 + 30)) -gt $w ] && w=$((ui0 + 30))
	now=$(cut -d. -f1 /proc/uptime); [ $w -gt $now ] && sleep $((w - now))
	if [ -f /mnt/internal/homtom/gui.disable ]; then
		unmute "GUI disabled (INTERNAL:/homtom/gui.disable)"
	elif ! gui_up; then
		boot_fail "GUI not running $((w - T0)) s after init start"
	fi
}
# Userspace is alive: this write stands the kernel's 60 s guard down and draws nothing while muted. Skipped on a verbose
# kernel (homtom.verbose=1), where writing 1 would replace the boot text with the splash.
VERBOSE=0
case " $(cat /proc/cmdline) " in *" homtom.verbose=1 "*) VERBOSE=1 ;; esac
[ $VERBOSE = 1 ] || echo 1 2>/dev/null > /sys/kernel/homtom_splash
```

**2. After** the line `k "logdir=$LOGDIR blockdevs: $(ls /dev/mmcblk* 2>/dev/null)"` add:

```sh
[ -n "$LOGDIR" ] || boot_fail "INTERNAL not mounted (no FAT on /dev/mmcblk*)"
if [ -n "$LOGDIR" ] && [ -f "$LOGDIR/homtom/verbose" ]; then VERBOSE=1; unmute "verbose boot (INTERNAL:/homtom/verbose)"; fi
```

**3. Replace** the line `( sleep 15; homtom-ui-start ) > /dev/null 2>&1 < /dev/null &` with:

```sh
[ -x /bin/homtom-gui-cfg ] || boot_fail "missing /bin/homtom-gui-cfg"
( sleep 15; homtom-ui-start ) > /dev/null 2>&1 < /dev/null &
( boot_watchdog ) > /dev/null 2>&1 < /dev/null &
```

Notes: `tail` is not in the project BusyBox, hence `wc`/`sed`. `/dev/kmsg` writes are rate-limited to 10 lines per 5 s by
default; `printk_devkmsg=on` lifts that before the banner. The early `echo 1` runs right after `/proc` and `/sys` are
mounted. If `LOGDIR` is the SD card (`docs/LESSONS.md`), the `verbose` file is looked up there.

### c. GUI (proposal for `workspace/gui`, not edited)

The GUI already mutes at start (`hw_cfg.c`, `write_file("/sys/kernel/homtom_splash", "1\n")`); with 0007 that write no
longer redraws anything. Two small changes make the watchdog exact and keep text off the screen in normal use:

```c
/* hw_cfg.c: next to write_file() */
static void touch_file(const char *path)
{
	int fd = sys_open(path, 01 | 0100 | 01000, 0644);	/* O_WRONLY | O_CREAT | O_TRUNC */

	if (fd >= 0)
		sys_close(fd);
}
static int ready;	/* zero-initialised, like stdin_eof */

/* main loop, after the first frame reaches the LCD */
			if (redraw && !x.locked) {
				cfg_draw(&x);
				blit();
				if (!ready) {
					ready = 1;
					touch_file("/tmp/gui.ready");
				}
			}

/* at exit: drop the unmute, so a respawn does not flash the log for 2 s */
-	if (mute)
-		write_file("/sys/kernel/homtom_splash", "0\n");
```

`hw_dev.c` (legacy dashboard, `gui.legacy`) needs the same two changes. In `userland/homtom-ui-start`, clear the marker
before each launch and unmute after a crash loop:

```sh
fails=0
while :; do
	rm -f /tmp/gui.ready
	t0=$(cut -d. -f1 /proc/uptime)
	# ... existing pipeline ...
	[ $(($(cut -d. -f1 /proc/uptime) - t0)) -lt 20 ] && fails=$((fails + 1)) || fails=0
	[ $fails -ge 3 ] && { echo "homtom-ui: GUI exited 3 times within 20 s, console unmuted" > /dev/kmsg; echo 0 > /sys/kernel/homtom_splash; }
	sleep 2
done
```

Without these GUI changes the proposal still works: `gui_up` falls back to "the same GUI pid for 10 s", and a GUI exit
still unmutes for the 2 s respawn gap (as today).

### d. Forcing a verbose boot

| How | Needs | Effect |
| --- | --- | --- |
| `homtom.verbose=1` in `CONFIG_CMDLINE` (`kernel/homtom/homtom_defconfig`) | kernel rebuild (`CONFIG_CMDLINE_FORCE=y`: TTBL's command line is ignored) | text from the first console line, as in k3 |
| Empty file `INTERNAL:/homtom/verbose` (storage mode: folder `homtom`, file `verbose`; or `touch /mnt/internal/homtom/verbose` from Linux) | init proposal b | init unmutes once INTERNAL is mounted; the redraw shows the newest log lines, so the earlier boot is visible too |
| `INTERNAL:/homtom/gui.disable` | nothing (works in ladder-k4) | no GUI; ladder-k4: the kernel guard unmutes 60 s after boot; with proposal b: the init watchdog unmutes ("GUI disabled") |
| `echo 0 > /sys/kernel/homtom_splash` on a running unit | shell | log tail now, then live text (a running GUI repaints over it within about a second) |

`cat /sys/kernel/homtom_splash` reads `1` while muted. The boot log is always in `dmesg` and in
`INTERNAL:/homtom/logs/boot.dmesg`.

## 4. Risks

- **Blank or wrong frame if the adopted buffer is wrong.** If LCDSADDR1/2 do not describe the frame on screen (another
  geometry, a virtual screen via OFFSIZE), the copy shows part of the splash, a shifted image or garbage, and it stays
  there until the GUI draws. The decompressor copies exactly the LCDSADDR1-2 span (bounded to 256 KiB) and leaves
  LCDSADDR3 as it is; the kernel always relied on the same registers.
- **Stripes or a band of noise in the splash** would mean the D-cache clean missed lines [H, unlikely: ARM920T is read
  allocate, so the copy's writes mostly bypass the cache, and every line is cleaned anyway].
- **Black flash.** Only in the fallback path (decompressor skipped): TPAL black from `homtom_reserve()` to
  `homtom_fbtext_init()`, a few ms, then the built-in splash, which looks the same as the FAT splash. A custom
  `SPLASHW.BMP` other than ours would then visibly change to the built-in one.
- **Brightness step.** `homtom_lcd_gpio()` (unchanged) drives GPB0 high at `machine_init`: backlight full. If TTBL
  dims the panel with PWM, the splash gets brighter at that moment [H]. Not changed here.
- **Decompressor runs before the kernel's safety net.** A hang in the new code happens before `map_io` writes INFORM0=5
  and before the kernel stops TTBL's watchdog. TTBL's watchdog then resets with INFORM0 as TTBL left it ("Power down"):
  the unit stays dark until the power key is pressed, and the same image runs again. Recovery: power key with USB on an
  unlocked Mac, storage mode, restore `ttsystem.prev`. The code is a bounded copy plus register writes, checked in
  the disassembly, but it is new code before any recovery path.
- **Boot text is invisible by default.** Failures that are not panics and leave the GUI running (a missing daemon) no
  longer show on the LCD; read `dmesg` or `INTERNAL:/homtom/logs/boot.dmesg`.
- **Kernel guard false positive.** In ladder-k4 (init.sh unchanged) the first `homtom_splash` write comes from the GUI
  at roughly init + 15 s. If boot is that slow that it passes 60 s, the log appears until the GUI mutes. Proposal b
  writes `1` at init start and removes this case.
- **Duplicate lines.** At unmute, records not yet emitted to consoles appear in the redraw and again live (a few lines
  at most).
- **k2 and k3 are untested.** ladder-k4 is also the first boot of the k2/k3 changes (reset path, power key input,
  pstore). See `docs/KERNEL_K3.md` for their risks.
- `clk_ignore_unused` must stay on the command line: without it the unused LCD clock gate would be switched off at
  late init and the panel would go blank.

## 5. Device test (user; nothing here was run by the project)

Prerequisites: user present, Mac unlocked, USB attached, `ttsystem.lab2613` and a good `ttsystem.prev` on INTERNAL.
Stage with `tools/homtom-stage artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.ladder-k4` (retry once on "Permission
denied"); compare the SHA-256 on INTERNAL with `ttsystem.ladder-k4.sha256` before ejecting.

Filming: phone in slow motion (120 or 240 fps), fixed on a stand, screen filling the frame, exposure locked on the
splash (tap and hold), room light constant. Start recording before pressing the power key; stop when the UI is up.
Also take one normal-speed video for timing. Step through the slow-motion clip frame by frame from the splash to the UI.

### 1. Cold boot (filmed)

Power off (hold the key 10 s with keyd, or let the battery path do it), then press the power key once.

Expected: black (NOR logo) -> splash -> UI. In the slow-motion clip: no frame with coloured noise, no white text on
black, no black frame between splash and UI. A single brightness step on the splash is possible (risk list). The splash
stays for roughly 15-25 s (kernel plus init plus the UI's 15 s delay).

Record: time from splash to UI; any noise, text, black or brightness frames, with their frame numbers.

### 2. Warm reboot (filmed)

`reboot` from the shell (the wrapper), then eject INTERNAL on the Mac (`tools/homtom-to-linux`). Expect the same as
test 1 after the storage window. After a warm reset 0x33F00000 still holds the last GUI frame; before 0007 it could
flash up at the DMA switch [H]. It must not appear now.

### 3. State checks over the link

```sh
dmesg | grep -e "LCD console" -e "boot incomplete"    # expect: homtom: LCD console muted, bootloader frame adopted
cat /sys/kernel/homtom_splash                          # expect: 1
devmem 0x4D000014                                      # expect: 0x19F80000 (LCDSADDR1 -> 0x33F00000)
devmem 0x4D000050                                      # expect: 0x00000000 (TPAL off)
```

`not adopted` in the first line means the decompressor skipped the copy: then test 1 still shows the old noise. Record
the LCDCON1 value (`devmem 0x4D000000`) for the analysis.

### 4. Unmute and mute live

1. `echo 0 > /sys/kernel/homtom_splash`: the screen shows the last ~33 log lines (no timestamps); the GUI repaints over
   them within about a second. `cat /sys/kernel/homtom_splash` reads 0.
2. Restart the GUI or wait for its next mute (`killall homtom-gui-cfg`; the launcher restarts it after 2 s): the UI
   returns, the file reads 1.

### 5. Kernel guard (filmed at normal speed)

1. Create `INTERNAL:/homtom/gui.disable` (storage mode or `touch /mnt/internal/homtom/gui.disable`), reboot.
2. Expect the splash until about 60 s after power-on, then the log tail with
   `homtom: boot incomplete: no userspace after 60 s, console unmuted` and live heartbeat lines every 30 s
   (`homtom-init: up=...`). With proposal b integrated the message is `GUI disabled (INTERNAL:/homtom/gui.disable)`
   at about 45 s instead.
3. Delete `gui.disable` and reboot; test 1 behaviour must return.

### 6. Panic with the quiet default

As `docs/KERNEL_K3.md` test 4, with the GUI running: `sync; umount /mnt/internal; echo c > /proc/sysrq-trigger`.
Expected within a second: backlight full, cleared screen, `Kernel panic - not syncing: sysrq triggered crash` and the
stack, frozen; restart after 10 s into the storage window. Photograph it.

### 7. Verbose (only after proposal b is integrated)

Create `INTERNAL:/homtom/verbose`, reboot: splash first, then, once INTERNAL is mounted, the log tail and live text
until the GUI starts. Delete the file afterwards.

Record every result in `docs/LESSONS.md` with the image name `ladder-k4`. If ladder-k4 does not boot (dark unit, no USB),
use the recovery steps in `docs/KERNEL_K3.md` (power key, storage mode, `ttsystem.prev`).

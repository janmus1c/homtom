# Performance review of the homtom image (read-only, 2026-11-07)

All numbers come from the live device (uptime about 2100 s, build #93) through `tools/homtom-sh`, read-only. Anything marked **estimate** is not measured. dmesg has no timestamps (`CONFIG_PRINTK_TIME` is off), so the boot timeline uses `/proc/PID/stat` start times (HZ=100) and the `homtom: stage ... jiffies` lines. Repeat it with `workspace/perf/boot-marks.sh`.

## 1. Boot timeline (uptime seconds; the clock starts when the kernel timer starts, so bootloader and zImage decompression are not included)

| Uptime | Event | Source |
|---|---|---|
| 0.11 | kernel_init (PID 1) created | PID 1 start time |
| 1.92 | late initcalls done (`stage 0x101`) | jiffies 4294937488 minus initial jiffies |
| 1.96 | exec /init (the ARMv4T probe), then busybox init.sh | stage 0x10b |
| 2.86 | first init.sh child (watchdog feeder, PID 55): about 0.9 s of mounts and setup forks | PID 55 start |
| 3.25-3.36 | usb0 configured; telnetd 23, tcpsvd 2323/21/8080, gpsd, udhcpd started | PIDs 77-89 |
| 4.66 | mount/log copy/sync done, main loop starts (first heartbeat `up=4.66`) | PID 121 start, dmesg |
| 8-12 | usb pull-up toggle completes (sleep 6, 3) so the Mac sees the interface | init.sh |
| 44.66 | homtom-bt-start begins after the fixed `sleep 40` | init.sh |
| 46-47 | Bluetooth modules loaded | dmesg `Bluetooth: Core` between heartbeats at 34.9 s and 65.1 s |
| about 69-80 | "homtom-bt: up, discoverable" | dmesg order between kernel marks at 68.9 s and 79.9 s |

A second boot (the unit was rebooted by someone else during this review) reproduced the marks within 0.05 s: PID 55 at 2.87, listeners 3.28-3.38, main loop 4.65.

Status endpoint answers from about 3.3 s (listener up), but each request takes 2.2 s (section 5).

Biggest sinks, in order:
1. Fixed `sleep 40` before Bluetooth: 40 s. Bring-up itself then adds 25-35 s, of which at least 10 s are fixed sleeps in `homtom-bt-up` (1+1+4x1 s) and `homtom-bt-start` (2 s). The BT bring-up has no dependency on anything started after init: start it at once in the background. Expected: BT ready at about 30 s instead of about 75 s (saves about 40 s); shrinking the sleeps in `homtom-bt-up` (the BCSP handshake frames at 1 s spacing) may save another 3-5 s (estimate; needs a chip test).
2. Kernel initcalls: 1.85 s from kernel_init to exec /init, all with `ignore_loglevel` printing about 330 lines to the LCD. Not separable without timestamps.
3. init.sh prelude: 0.9 s for about 12 forks (about 70 ms each, versus about 10 ms each later at 3.3 s). The first forks print "homtom: user fault" lines (60 in dmesg, `user_debug=31`) and each goes to the LCD console. Suspect console cost; verify by building with `user_debug=0` and comparing PID 55 start time.
4. FAT mount plus `sync` of the sync-mounted volume: 1.3 s (3.36 to 4.66). The listeners are already up by then, so it does not delay the network; it only delays the heartbeat.
5. Bootloader load and zImage decompress: not measurable here.

Safe changes, with expected savings (estimates unless stated):
- Start Bluetooth bring-up immediately instead of after 40 s: **-40 s to BT ready** (measured sleep).
- `CONFIG_PRINTK_TIME=y` plus a build with `initcall_debug` once: no saving, but gives real numbers (cost: zero).
- Add `lpj=985088` to the cmdline (value from this boot's "Calibrating delay loop"): saves the calibration, about 0.1-0.2 s at HZ=100 (estimate).
- Cmdline `quiet` removes `ignore_loglevel` console output (about 330 kernel lines plus user-fault lines): 0.3-1 s (estimate). Keep `ignore_loglevel` only for debug images; `user_debug=31` and `CONFIG_DEBUG_USER` can go in a release build.
- zImage compressor: `CONFIG_KERNEL_GZIP=y` now; zImage 4.43 MB, so the Image is about 8.8 MB. On ARM9 at 396 MHz, gzip inflate runs about 20-30 MB/s (estimate, caches on), so decompression is about 0.3-0.45 s. LZ4/LZO decompress 2-3x faster but compress about 35 % worse, which adds load time from storage. XZ/LZMA are slower to decompress. Verdict: trim the kernel first (below); switching the compressor is worth at most 0.1-0.2 s and is not recommended until the load path is measured.
- Initramfs: gz is 1.18 MB, unpack under 0.2 s (estimate). Content is 2.9 MB unpacked plus a **7 MB `/initrd.image`** (see section 2). Stripping unused modules from `/lib/modules` (bnep, rfcomm, uinput are about 90 KB) saves memory only.
- Parallelise init.sh: start the FAT mount, log copy and `sync` as one background job after the listeners (they already follow the network). Saves about 1.3 s before the main loop (and before the first heartbeat); the network does not wait on it today.
- The 30 s heartbeat loop is not on the boot path; the 30 s `sleep` only paces heartbeats. Not a boot cost. Reducing its forks is a power matter (section 4).
- Defconfig trimming (kernel code is 6571 KB, rodata 1684 KB): the config enables NFS client and root NFS, NFSD, CIFS, JFFS2, CRAMFS, ROMFS, ISO9660, UDF, ATA/SCSI sr, NAND S3C2410 and CFI MTD, IPv6, netfilter, joystick/touchscreen/sound, `USB_MON`, FTDI/PL2303 serial and `LEGACY_PTYS`. Turning off the ones the project does not use should remove 2-4 MB of kernel and cut decompression and initcall time in proportion (estimate 0.2-0.5 s). Do it in steps with a boot test each; do not drop MMC, VFAT, USB gadget ECM, BT UART, framebuffer or the hsudc driver.

## 2. Memory (64 MB board, `mem=64M`)

| Item | Value |
|---|---|
| MemTotal | 55444 kB (10.1 MB reserved: kernel image about 8.8 MB, 256 KB DMA pool and others) |
| MemFree / MemAvailable | 35136 / 34092 kB |
| Shmem (rootfs files) | 10032 kB, of which **7168 kB is `/initrd.image`** (7340032 bytes in /) |
| Slab | 4748 kB (SUnreclaim 4352) |
| KernelStack / PageTables | 416 / 272 kB |

Findings:
- `/initrd.image` is the bootloader initrd region (7 MiB) that the kernel could not parse as an initramfs ("looks like an initrd") and copied into tmpfs. It is not used at run time: `rm -f /initrd.image` early in init.sh returns 7 MB, about 13 % of RAM (MemFree 35 to 42 MB). Low risk; verify nothing opens it (`ls /proc/*/fd` shows none now, to be rechecked in the new build).
- Slab: `kernfs_node_cache` has 13000 objects (1.1 MB) and `vmap_area` 2480, `kmalloc-32` 4648, `kmalloc-2k` 426 (852 KB), `shmem_inode_cache` 810. The 13000 sysfs nodes come mostly from tty devices; `CONFIG_LEGACY_PTYS=y` and `CONFIG_VT` with 63 consoles are the likely sources. Setting `LEGACY_PTYS=n` and a small `MAX_NR_CONSOLES` should free 0.5-1 MB (estimate; confirm which driver creates them first). The slab allocator is SLAB; SLUB is slightly smaller for small systems (estimate 100-300 KB), not worth the risk alone.
- `mem auto-init: stack:all(zero)` and `STACKPROTECTOR_STRONG` cost CPU for no lab benefit; turning off `INIT_STACK_ALL_ZERO` and using `STACKPROTECTOR_REGULAR` is a small speed gain (estimate 1-3 %).
- Plenty of memory is free (34 MB); memory trimming is for headroom, not for need.

## 3. CPU clock

Read-only register reads: MPLLCON 0x4C000010 = 0x805B0300, CLKDIV0 0x4C000024 = 0x0000102D, CLKSRC 0x4C000020 = 0xD0.
- Kernel evidence: `mmc_busclk.0` = 132 MHz (HCLK), `sched_clock 16 bits at 1320 kHz` (PCLK 66 MHz / 50), BogoMIPS 197.01 (lpj 985088; ARM920T gives about BogoMIPS = ARMCLK/2).
- Result: HCLK 132 MHz, PCLK 66 MHz, ARMCLK about 396 MHz (792 MHz PLL, divide by 2). The register decode of MPLLCON was not confirmed against the datasheet; the three kernel facts above agree, so the CPU runs at full speed (about 400 MHz). Nothing to gain.
- cpufreq: `CONFIG_CPU_FREQ` is off and mainline 6.1 has no S3C2443 cpufreq driver; `/sys/devices/system/cpu/cpu0` has no cpufreq directory. Lowering ARMCLK would need CLKDIV0/MPLL writes and a driver; not recommended.

## 4. Power (idle behaviour, no power meter available)

Measured over 30 s with nothing running from outside:
- CPU idle 81 % (idle 24.42 s, system 4.83 s, user 0.78 s). Load average 0.77/0.59/0.41.
- Timer interrupts 100 per s (`CONFIG_HZ_PERIODIC`, `NO_HZ_IDLE` and `HIGH_RES_TIMERS` off); total interrupts about 152 per s; UART2 (GPS) about 12 per s; context switches 145 per s; **11 forks per s** (336 processes in 30 s).
- Per-process CPU since boot: homtom-gpsd 30 s of CPU (the largest user: 1.4 % of wall time, one subshell and two `mv` forks per NMEA sentence), watchdog feeder 7.8 s, init 2.7 s.
- ARM920T has a VIVT cache: every process switch and exec flushes caches, so the fork churn is much more expensive than its CPU share suggests. The 16 % system time with about 2 % of user work points at it.
- Processes: 24 kernel threads, busybox init + 6 subshells, telnetd, 3 tcpsvd, udhcpd, gpsd, hciattach, plus an experiment binary `/tmp/tsk`. Nothing is busy-looping.

Ideas, largest first (estimates; unmeasured):
1. Replace `homtom-gpsd` and the watchdog feeder/heartbeat subshells with one C daemon that does blocking reads on ttySAC2 and nanosleep (no forks): removes about 9 of the 11 forks per s and most of the 16 % system time.
2. `CONFIG_NO_HZ_IDLE=y` plus `HIGH_RES_TIMERS=y`: removes about 100 wakeups per s while idle. Needs a boot test (the samsung PWM timer supports oneshot upstream; unverified here).
3. Gate unused peripheral clocks and drop `clk_ignore_unused`. Current gate registers: HCLKCON 0x4C000030 = 0xFFFEFFFF, PCLKCON 0x4C000034 = 0xFFFFFFBE, SCLKCON 0x4C000038 = 0xFFFF8FFF, so almost every clock is on (left on by the bootloader). Candidates: USB host, camera, I2S, I2C, SPI1, AC97, PWM when the backlight does not use it. The mapping of bits to blocks must be confirmed per the datasheet (docs/HARDWARE_MAP.md lists only some bits) and each gate tested; note the table says bit 16 of HCLKCON is HSMMC1 although it reads 0 while mmc0 works, so that note needs checking first.
4. Power-gate GPS (GPJ6 low via `homtom-hw gps-off`) and Bluetooth (BT_RESET low) when not in use: GPS is on permanently now. Needs a trigger (a flag file or an HTTP command) and a decision that no fix is required at idle.
5. Remove per-sentence console prints and `ignore_loglevel`; every kernel print is drawn on the LCD.
6. `CONFIG_DEBUG_USER` and `user_debug=31` add exception-path overhead and prints; remove in release.
7. Bluetooth health check `hcictl` every 60 s: fine at that rate, but make it one fork.

## 5. Status endpoint cost (port 8080)

Five requests from the Mac, 2 s apart (`curl -w %{time_total}`): 2.149, 2.159, 2.263, 2.146, 2.273 s; mean 2.20 s, all HTTP 200.

Device-side timing (uptime deltas, 10 ms resolution, same commands as the script): one `devmem` read plus cut 0.04 s; `homtom-hw adc 2` 0.18 s; `homtom-hw battery` 1.41 s (includes a fixed `sleep 1` for the reference to settle, two ADC reads and GPL14 toggles); the script also does 2 more devmem reads with cut, 2 light-sensor writes and a second ADC. The sum fits the 2.2 s observed: about 1.0 s fixed sleep, 0.4 s ADC reads, the rest forks (about 45 per request).

Also: every request writes to hardware registers (GPL14 reference power, LX_EN), which is a side effect of a GET; two overlapping requests race on the ADC mux.

Lighter design (single freestanding C daemon, static, `-nostdlib` or musl, about 10 KB):
- One process listening on 8080 (`poll` over listen socket and ttySAC2); no tcpsvd, no per-request fork.
- Read GPIO and ADC registers via one `mmap` of `/dev/mem` (pages 0x56000000 and 0x58000000, 0x4C000000), no devmem forks.
- Background sampler: switch the battery reference on every 30-60 s, wait 20 ms with `nanosleep` (not 1 s; to be validated on the hardware), read ADC 0/4/2, switch it off, and cache the result. Requests then read the cache and answer in under 5 ms (estimate), with no hardware writes on the request path.
- Parse NMEA GGA/RMC in the same loop (replaces homtom-gpsd), keep the last sentence and its uptime stamp.
- Own the watchdog: write `/dev/watchdog` from a timerfd/poll timeout and keep the "stop feeding if the main loop stalls" rule inside the daemon (the shell feeder can be removed).
- Expected: 2.2 s to under 0.01 s per request; 9-10 fewer forks per s; one fewer long-lived shell (about 1.9 MB virtual, small resident).
- Build with the existing arm-linux-gnueabi toolchain used for `homtom-ts`; keep the shell scripts as fallback behind a flag file until the daemon is tested.

## Not done

No change was made to the device or the repository besides this file and `workspace/perf/`. No power measurement, no zImage load timing and no decompress timing exist; the numbers above for those are estimates.

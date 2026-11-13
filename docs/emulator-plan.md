# Emulator plan: the whole GO 630 on the Mac

Goal: run the real NOR bootloader, the real 2.6.13 kernel and the OpenTom/homtom userspace on the Mac, with a 480x272 screen, touch, power/reset/USB buttons, a USB device side and the boot storage. Use it to test firmware, bootloader graphics flashes and the USB gadget driver before touching hardware (see `docs/flash-safety.md`).

Status: research done, phase 0 prototyped and working (section 6). Nothing touched the device.

## 1. Corrections to the brief

These came out of the sources and logs and change the plan:

| Brief said | Evidence says |
|---|---|
| moviNAND is mmcblk0 on s3c-hsmmc | The other way round. The boot log shows `s3c_hsmmc_probe: mmc1 ... irq 36` and `mmcblk1: mmc1:0001 ... 2007040KiB` with no partition table (the 2 GB INTERNAL FAT, holds `ttsystem` and `/mnt/sdcard/opentom`). `mmcblk0: mmc0:59b4 SQ08G` (8 GB SD card, `mmcblk0p1` = `/mnt/movinand`) is on `s3c-sdi`. |
| NOR is CFI | The kernel finds it with JEDEC probe, not CFI: `jedec_probe_chip(): man=0x1c id=0x22ba`, `Found: EON EN29LV400BB`, AMD command set (`cfi_cmdset_0002`). Bottom boot layout from `drivers/mtd/chips/jedec_probe.c`: 1x16 KiB, 2x8 KiB, 1x32 KiB, 7x64 KiB; x16 bus, unlock addresses 0x555/0x2AA. |
| The kernel needs an ARM920T | `.config` has both `CONFIG_CPU_ARM920T=y` and `CONFIG_CPU_ARM926T=y` (the image also supports the ARM926-based S3C2412). The kernel picks the proc entry by MIDR, so QEMU's stock `arm926` CPU should boot it. Not yet tried. |
| OpenTom dist in `artifacts/opentom-dist-snapshot` | It holds only `share/` and `start.sh`. `nano-X`, `nanowm`, `nxmenu`, tslib and their libraries live in `/opentom/bin` and `/opentom/lib` on the device and are not on the Mac. The `first-INTERNAL` backup has only `start.sh` and logs. |

Kernel facts used below (from the boot log in a device boot log captured over the USB link and a local `.runtime/kernel-build`):

- `Machine: TomTom GO` = `MACH_TYPE_TOMTOMGO` 605 (`include/asm-arm/mach-types.h`).
- `CPU S3C2443 (id 0x32443000)` is read from `GSTATUS1` (0x560000B0) by `IO_DetectCpu()` in `arch/arm/mach-s3c2410/tomtomgo-type.c`.
- `Detected type 25, name TomTom GO 630`. `IO_Init()` takes the board from `system_rev` (`ATAG_REVISION`): low 16 bits = main id (`GOBOARD_MODENA` = 10), high 16 = sub id (`JA/JB/JC` = 0xa/0xb/0xc for 1/2/4 GB, all named "GO 630"). With no tag it reads GPIO straps instead. Expected value for a 2 GB unit: `0x000b000a` (to confirm from `/proc/cpuinfo` "Revision").
- TomTom ATAGs (`arch/arm/kernel/setup.c`): `ATAG_VIDEOLFB` (the bootloader's framebuffer; `ttgfb: Using LFB from bootloader`), `ATAG_RESET_STATE` 0x5441000a, `ATAG_BOOT_DEVICE` 0x5441000b, `ATAG_TFT_TYPE` 0x5441000c, `ATAG_FEATURE` 0x5441000d. Without them the drivers fall back (dma_alloc framebuffer, TFT hardware detection).
- `ttgfb: Using s3c2410 lcd controller`. The GO 630 drives the legacy LCD block at 0x4D000000, not the S3C2443 TFT controller at 0x4C800000 (that one is used only on S3C2450).
- Userspace is OABI (ELF flags 0x602, EABI version 0, soft-float), glibc 2.3.2, BusyBox 1.22.1.

## 2. Findings: existing work

| Project | Base | What it models | State | Licence | Builds? |
|---|---|---|---|---|---|
| OpenMoko qemu-neo1973 (Andrzej Zaborowski, 2007-2008) | QEMU 0.9 | S3C2410 (GTA01), partial S3C2442 (GTA02). INTC, DMA, PWM timers, UARTs, GPIO, RTC, ADC/touch, SPI, I2C, IIS, WDT, legacy LCD, SDI/MMC, NAND, OHCI host, full-speed UDC (s3c24xx_udc.c) | Dead upstream. git.openmoko.org does not serve (TLS name mismatch); the wiki page is behind a bot wall. Code survives in the mini2440 fork, which imported it in Feb 2009 ("[S3C] Import S3C support from OpenMoko"). | GPLv2 | Via mini2440 (below) |
| mini2440 QEMU (Michel Pollet, repo.or.cz/qemu/mini2440.git) | QEMU 0.10.50, last commit 2011-10-13 | The OpenMoko S3C files plus S3C2440: `s3c2410.c` (INTC, DMA, PWM, UART, ADC/touch, I2C, SPI, IIS, WDT), `s3c24xx_gpio.c`, `s3c24xx_lcd.c` (legacy LCD), `s3c24xx_mmci.c` (SDI), `s3c24xx_rtc.c`, `s3c24xx_udc.c` (full-speed UDC, only a stub hookup: `// TODO import usb gadget code from openmoko`), `s3c2440_nand.c`, DM9000 Ethernet, an `arm920t` CPU entry. No HSMMC, no HS UDC, no S3C2443. Needs u-boot; no `-kernel`. | Unmaintained | GPLv2 | Yes. I built `qemu-system-arm` with machine `mini2440` and CPU `arm920t` in an amd64 `ubuntu:14.04` container (needs `--disable-gfx-check` and `-lrt`). Script: `.runtime/emu/src/build-mini2440.sh`. |
| qemuPrime (github.com/opcod3/qemuPrime, branch `s3c2416`) | QEMU 2.10.1, last commit 2021-08-17 | S3C2416 for the HP Prime G1: INTC, UART, GPIO, SYSC (clocks), MEMC, PWM (reuses Exynos model), LCD (reuses Exynos FIMD at 0x4C800000), NAND, WDT, ADC (wired out), RTC/I2C (Exynos). Uses CPU `arm926`. No SDHCI or HS UDC. | Hobby, stalled | GPLv2 | Not tried |
| Other S3C2443/2450 models | none found | - | - | - | - |
| Mainline QEMU (11.1.50, cloned to `.runtime/emu/src/qemu-mainline`) | - | CPUs `arm926`, `arm946`, `arm1026`, `ti925t` (ARMv4T), `sa1100/1110`; no `arm920t`. Reusable devices: `pflash_cfi02` (AMD command set, JEDEC IDs, up to 4 erase regions, configurable unlock addresses), `s3c-sdhci` (SDHCI with Samsung CONTROL2/3/4, i.e. the S3C2443 HSMMC layout), `emmc` card model, Exynos PWM/RTC/I2C, `unimplemented-device`, VNC with websocket, cocoa display on macOS, QMP. No S3C24xx SoC. PXA/OMAP2 boards removed in 9.2; nothing ARMv4/5 we need is scheduled for removal. | Active | GPLv2 | Yes |
| qemu-user (Debian trixie, 10.0.13) | - | Runs OABI binaries (`linux-user/arm/cpu_loop.c` decodes OABI syscall numbers) | Works today, but **OABI and NWFPE support are deprecated and "will be removed in a future QEMU release"** (QEMU deprecated-features page). Pin the version. | GPLv2 | Packaged |

What the existing models give us: the S3C2443 keeps the S3C2410 register layout for INTC (plus more sub-sources), PWM timers, UARTs (64-byte FIFOs), WDT, RTC, ADC (plus `ADCMUX` at 0x58000018), SDI and the legacy LCD. It differs in clocks/power (0x4C000000 layout), GPIO (ports A-M, pull registers), HSMMC (SDHCI) and the HS USB device controller. So the plan is a new S3C2443 machine on **mainline QEMU**, porting the OpenMoko/mini2440 code for the shared blocks to modern QOM, and using mainline `s3c-sdhci`, `emmc` and `pflash_cfi02` as they are. Porting the old fork forward instead would mean rebasing 0.10-era code onto 11.x; that costs more than a port.

## 3. Minimal device set

Method: the machine and board files (`mach-tomtomgo.c`, `tomtomgo-devs.c`, `s3c2443*.c`, `time.c`, `tomtomgo-iomodena.h`), the driver sources and the boot log. Anything else is mapped as `unimplemented-device` with `-d unimp` logging, so the first boots list the rest.

### 3a. Fast path: kernel loaded directly

A machine-specific loader parses `ttsystem` (TTBL records: size, address, payload, 16-byte signature; see `firmware/ttimage.py`), copies initramfs to 0x31000000 and the zImage to 0x31700000, writes an ATAG list at 0x30000100 (CORE, MEM 64 MiB @ 0x30000000, INITRD2, CMDLINE `root=/dev/mmcblk0p2`, REVISION 0x000b000a, optionally VIDEOLFB and TFT_TYPE) and starts at 0x31700000 with r0=0, r1=605, r2=0x30000100. QEMU's generic `arm_load_kernel` cannot write the TomTom tags, so this loader is ours.

| Block | Address | Needed for | Source to reuse |
|---|---|---|---|
| CPU `arm926` | - | everything | mainline |
| SDRAM 64 MiB | 0x30000000 | everything | mainline |
| UART0-3 (S3C2443 variant, 64-byte FIFO) | 0x50000000 | decompressor and console (`CONFIG_S3C2410_LOWLEVEL_UART_PORT=0`, IRQs 70/73/76/94) | port `s3c2410.c` UART |
| INTC with S3C2443 sub-sources (SUBSRCPND/INTSUBMSK, WDT/AC97, CAM, DMA, LCD, UART3 groups) | 0x4A000000 | IRQs | port, extend from `s3c2443-irq.c` |
| PWM timer 4 (system tick), TCFG0/1, TCON | 0x51000000 | jiffies (`time.c`) | port, or mainline Exynos PWM adapted |
| Clock/power (MPLLCON, EPLLCON, CLKSRC, CLKDIV0, HCLKCON, PCLKCON, SCLKCON, PWRCFG, RSTCON, PHYCTRL/PHYPWR/URSTCON/UCLKCON) | 0x4C000000 | clock tree (must give 396/132/66 MHz) and USB PHY bits | new, register file with reset values |
| GPIO A-M, GSTATUS1 = 0x32443000, EXTINT, EINTMASK/PEND | 0x56000000 | CPU detect, IO_Init pins (straps, ON_OFF GPF0, USB_HOST_DETECT GPF2, CD_SD GPF1, LCD/backlight pins), EINT interrupts | port `s3c24xx_gpio.c`, extend |
| WDT, RTC | 0x53000000, 0x57000000 | registered devices, `hwclock` | port |
| HSMMC1 (SDHCI + Samsung CONTROL2-4, SDMA via `HM_SYSAD`) + `emmc` card holding the INTERNAL FAT image | 0x4A800000, IRQ 20 | `/mnt/sdcard` (OpenTom, homtom) | mainline `s3c-sdhci` + `emmc` |
| Everything else (DMA 0x4B000000, I2C 0x54000000, IIS 0x55000000, SPI, SDI 0x5A000000, LCD, ADC, HSUDC 0x49800000, OHCI 0x49000000, memory/matrix controllers) | - | not needed to reach a shell | `unimplemented-device` first; I2C must NAK cleanly so the LTC3455 PMIC and FM drivers give up |

Phase 2 adds the legacy LCD (0x4D000000), ADC with `ADCMUX` and touch, and GPIO keys. Phase 4 adds HSUDC. SDI with an SD card image (`/mnt/movinand`) is optional.

### 3b. Full path: real NOR bootloader

All of 3a, plus:

| Block | Why |
|---|---|
| EN29LV400BB at 0, via `pflash_cfi02`: 512 KiB, width 2, `id0=0x1c id1=0x22ba` (verify the exact ID word layout on the model), 4 erase regions as above, unlock 0x555/0x2AA, backed by a copy of `mtd0.bin` | Reset vector, stage 1 at 0x8000, gzip stream, image table at 0x28000, ngffs at 0x40000 |
| Memory/static memory controllers (0x48000000 DRAMC, 0x4F000000 SSMC) as accept-and-read-back register files | Stage 1 configures them before it gunzips to 0x30B00000 |
| HSUDC (0x49800000) | Storage mode ("TomTom Disk") |
| HSMMC1 SDMA, SDI | FAT reader for `ttsystem` and `SPLASHW.BMP`, SD card checks |
| Legacy LCD and backlight PWM | Boot screens, then handed to the kernel via ATAG_VIDEOLFB |
| ADC | Battery voltage ("battery boot voltage" is printed); a low reading may make it show `lowbatteryw.bmp` and stop |
| I2C (PMIC), IIS (ADPCM boot sound), DMA, RTC, WDT | Probably touched; behaviour unknown |

Evidence for the bootloader list: a literal-pool scan of `artifacts/nor-analysis/bootloader.out` finds exact base addresses for 0x4A800000 (HSMMC1, 13 times), 0x49800000 (HSUDC, 5), 0x4B000000 (DMA, 5), 0x56000000 (GPIO, 6), 0x4A000000 (INTC), 0x52000000 (the old full-speed UDC: the bootloader is built for several S3C24xx parts), 0x49000000 (USB host) and 0x55000000 (IIS). Stage 1 in NOR (0x8000-0xB293) references 0x48000000, 0x4C000000 (13), 0x4F000000 (12), 0x53000000, 0x56000000 (25) and 0x4A000000. This scan is approximate: it misses addresses built at run time, which is why the first full-path runs use `-d unimp` and an MMIO trace. The bootloader is closed code. It may poll for hardware states (PLL lock, I2C ACK, PHY status) that the models must answer correctly. This is the largest risk in the plan.

## 4. Screen, touch and buttons on macOS

Constraint: no global installs. Homebrew QEMU (cocoa display) is therefore out unless the user chooses to install it. QEMU therefore runs inside a Docker container (arm64 Linux, native speed for TCG) and the Mac uses a browser.

- **Display.** The LCD model is a QEMU graphic console. QEMU serves it with `-vnc :0,websocket=on` (built in; a websocket port at 5700+display). A small web page in the container embeds noVNC (pinned version), scaled 2x, next to the buttons. macOS Screen Sharing (`vnc://127.0.0.1:15900`) also works for a quick look. If the user later installs QEMU via Homebrew, `-display cocoa,zoom-to-fit=on` gives a native window with the same machine.
- **Touch.** The touchscreen model registers a QEMU absolute-pointer input handler (`qemu_input_handler_register` with `INPUT_EVENT_MASK_ABS`), so VNC, noVNC and cocoa mouse clicks become touches. Inside the model, a touch sets what the ADC returns: on the GO 630 the driver (`drivers/barcelona/adc/adc.c`) cycles channels through `ADCMUX`, drives the panel planes through GPIOs (XMON/XPON GPG13/12, YMON/YPON GPG15/14, TSDOWN) and reads X on channel 5 and Y on channel 9 (`tsxchannel`/`tsychannel` in `tomtomgo-iomodena.h`). The model watches those GPIO outputs and returns plate voltages for the selected plane, or "pen up" levels. Calibration: ship a `pointercal` that matches the model's linear mapping.
- **Buttons** (web page buttons → a small HTTP backend in the container → QMP on a unix socket):
  - Power: a `homtom-board` QOM object with a `power-pressed` property drives ON_OFF (GPF0, active low, EINT0). `qom-set` press, wait, release. A long press while "off" starts the machine.
  - Reset: QMP `system_reset` (CPU restarts at 0; NOR and eMMC images keep their contents).
  - USB plugged: property `usb-connected` drives USB_HOST_DETECT and ACPWR (both GPF2, active low, see `tomtomgo-iomodena.h`) and the HSUDC VBUS/`UCLKCON_DETECT_VBUS` status, and attaches or detaches the emulated host on the HSUDC.
- **"USB connected at boot means storage mode"**: on hardware the bootloader reads the USB detect line after reset and, if a host is present, becomes the "TomTom Disk" mass-storage device (OpenTom's "reboot with cable connected" relies on this). Wiring:
  - Full path: no special logic. The board model keeps GPF2 low while `usb-connected` is on, the real bootloader sees it and runs its own storage mode on the HSUDC model. The emulated host on the HSUDC enumerates it as mass storage (phase 4).
  - Fast path (no bootloader): the launcher reproduces the decision. On power-on or reset with `usb-connected` on, it does not start the kernel. It shows a "storage mode" screen and offers the eMMC image to the Mac (stop the guest, `hdiutil attach` the raw FAT image). With it off, it loads `ttsystem` from the eMMC image's FAT, so a `ttsystem` written in storage mode boots next time, as on hardware.

## 5. USB device side

The Mac cannot see an emulated USB device as a real one. Software can present a USB device to macOS only through a host-controller driver on the Mac side (DriverKit/kext; nothing suitable is installed, and the rules forbid installing one). usbredir and USB/IP both need a client host stack on the side that "owns" the device. macOS has neither built in. So:

**Recommended: the emulated host lives inside QEMU.** The HSUDC model contains a small built-in USB host. When `usb-connected` is on, it:

1. Drives the device side through bus reset, SET_ADDRESS, GET_DESCRIPTOR (device, configuration with the 9-byte then full-length reads, strings), SET_CONFIGURATION and SET_INTERFACE (CDC ECM data alt 1). It replays the Mac's real request order and lengths, taken from `artifacts/trial-v3-gadget-service.txt` and the trial logs (for example the 80-byte configuration GET_DESCRIPTOR at which the v2 log stopped), and uses 64-byte EP0 max packet at high speed.
2. Then moves bulk transfers: each ECM OUT/IN transfer is one Ethernet frame, passed to a QEMU netdev. `-netdev user,net=192.168.1.0/24,host=192.168.1.1,hostfwd=tcp:0.0.0.0:2323-192.168.1.10:23,...` plus Docker `-p 127.0.0.1:2323:2323` gives the Mac `telnet 127.0.0.1 2323` to the guest's real `usb0`. (`-netdev vmnet-*` would give a real Mac interface, but only for QEMU running natively on macOS, which needs Homebrew, and vmnet needs root or Apple's networking entitlement; not verified here.)
3. For storage mode, it speaks Bulk-Only Transport and SCSI (INQUIRY, READ CAPACITY, READ/WRITE(10)) and checks the data against the eMMC backing image. That tests the bootloader's mass-storage path. For browsing files, use `hdiutil` on the image.

This runs the real guest stack: the real `s3c-udc-hs.c` (or a candidate build of it), `g_ether`, ECM framing. Cheaper "network only" designs (a virtio NIC posing as `usb0`) would skip exactly the code under test.

Optional later: speak the usbredir protocol from the HSUDC model and attach it to `usb-redir` in a second QEMU running Linux, so a real Linux host stack (`cdc_ether`, `usb-storage`) enumerates the gadget as an independent check.

**What this can and cannot reproduce of the USB receive bug** (see `docs/ROBUSTNESS.md`):

| Failure seen on hardware | Reproducible? |
|---|---|
| v1: Mac saw no device at all (PHY clock and test-mode settings wrong) | No. PHY analog behaviour, clocks and the test register are silicon details. The model can at most flag "PHY not powered/clocked per datasheet sequence" if we write those rules in, which is only as good as our reading of the datasheet. |
| v2: enumeration stopped at an 80-byte configuration GET_DESCRIPTOR (no control-IN continuation after the first packet) | Yes, if the model enforces 64-byte EP0 packets and raises the TX-complete interrupt only after the FIFO is drained, so the driver must refill. This class of bug (EP0 state machine) is what the emulator catches best. |
| v3: Mac enumerates at 480 Mbps, `AppleUSBECMControl` attaches, no network interface appears | Partly. The model can check status-stage ACKs, SET_INTERFACE alt switching, the interrupt-endpoint notification (EP3) and the data path with Linux-like or Mac-like request sequences. It cannot run Apple's driver, so a macOS-specific expectation (for example an NCM preference or ECM notification timing) stays a hardware-only test. Mitigation: record the real Mac's control traffic once with a USB analyser or IORegistry logs and add that exact sequence to the replay. |
| Timing, signal integrity, suspend/resume of the PHY | No. |

Fidelity limit: the model is written from the same Samsung documentation and driver source that the driver itself came from. Where both are wrong in the same way, the emulator passes and the hardware fails. Treat the emulator as a regression and logic test, not as proof that a driver will enumerate on the Mac.

## 6. Phase 0 prototype (done)

The phase 0 sources are not part of this repository; the current emulator is in `emulator/` (`emulator/run`).

What runs: the device's own initramfs (`build/initramfs.cpio.gz`: BusyBox 1.22.1, glibc 2.3.2, OABI) inside an arm64 Debian trixie container, with `x11vnc` serving a 480x272 RGB565 framebuffer and turning VNC clicks into evdev touch events.

- `fbshim.c` (built with the TomTom gcc 3.3.4 toolchain from `artifacts/source/toolchain`, `LD_PRELOAD`): makes a plain file `/dev/homtom-fb` behave like `/dev/fb0` (FBIOGET_VSCREENINFO/FSCREENINFO, 480x272, 16 bpp, RGB565, mmap) and a FIFO `/dev/homtom-ts` behave like `/dev/input/event0` (EVIOCGVERSION/NAME/BIT/ABS). Other ioctls pass through.
- `ts-bridge.py`: x11vnc `-pipeinput` → OABI `struct input_event` (16 bytes) into the FIFO.
- `fbdemo.c`: a test client that draws and plots touches.
- `rfbcheck.py`: a dependency-free RFB client on the Mac that grabs the screen and clicks.

Verified on 2026-11-02:

- `qemu-arm` 10.0.13 runs the OABI BusyBox directly (`qemu-arm -L rootfs`) and under `chroot` through Docker Desktop's own binfmt `qemu-arm` (flags POCF). The chroot form is needed for shell scripts, because a child `execve` under plain `qemu-arm -L` does not re-enter QEMU.
- The device's own `fbset -s` reports `geometry 480 272 480 272 16`, `rgba 5/11,6/5,5/0`.
- Running the device's `/etc/rc` step `cat /etc/homtom/splash.fb > /dev/fb` shows the homtom splash in VNC.
- Clicks at (100,100), (240,136), (400,220) from the Mac arrive as touches at the same coordinates in the ARM process.
- Pitfall found: old glibc `readdir` returns nothing on the Docker overlay filesystem (64-bit ext4 directory offsets do not fit the 32-bit `getdents` conversion), so `ls` printed nothing. The rootfs must sit on tmpfs (`--tmpfs /run:exec`); then `ls` works.
- Pitfall: x11vnc's rawfb polling shows changes 0.5-1.5 s late. That is acceptable for previews; phase 2 replaces it with QEMU's own VNC.

Historical run commands (the phase 0 build script is not published):

```sh
docker run -d --name homtom-phase0 --tmpfs /run:exec,size=128m -p 127.0.0.1:15900:5900 homtom-emu-phase0
open vnc://127.0.0.1:15900
docker exec homtom-phase0 chroot /run/rootfs /bin/busybox sh -c 'cat /etc/homtom/splash.fb > /dev/fb'
```

Not done: `nano-X` + `nxmenu`. Their binaries and libraries are not on the Mac (section 1). `run.sh` already copies a directory mounted at `/opentom` to `/mnt/sdcard/opentom`, sets `LD_PRELOAD`, and the device's `start.sh` paths (`/dev/fb`, `/dev/input/event0`, `TSLIB_*`) resolve to the shim. Next storage-mode session, copy `/Volumes/INTERNAL/opentom/{bin,lib,etc}` read-only into `artifacts/opentom-dist-snapshot/`, then run with `-v .../opentom-dist-snapshot:/opentom:ro` and `start.sh` as the command. Alternatively build the OpenTom dist from LeddaZ/OpenTom in the existing i386 build container (1-2 days, many upstream tarball URLs may be dead).

## 7. Recommended architecture

```
Mac browser ──http:8080──► container: web UI (noVNC + Power/Reset/USB buttons)
                 │                    │ QMP (unix socket)
                 └──ws:5700──► qemu-system-arm -M homtom-go630 (mainline QEMU 11.x + our S3C2443 SoC)
                                       ├─ arm926 CPU, 64 MiB SDRAM
                                       ├─ pflash_cfi02 EN29LV400BB ← nor.bin (copy of mtd0.bin)
                                       ├─ s3c-sdhci HSMMC1 + emmc ← internal.img (FAT: ttsystem, opentom/)
                                       ├─ SDI + sd ← sdcard.img (optional)
                                       ├─ legacy LCD → VNC console; ADC+touch ← VNC pointer
                                       ├─ GPIO board object (power, USB detect, straps)
                                       └─ HSUDC + built-in host ─► netdev user (hostfwd 2323→usb0:23)
Mac terminal ──telnet 127.0.0.1:2323──┘     serial0 → telnet 127.0.0.1:4555 (console)
```

- Two boot modes, same machine: `-kernel build/ttsystem` (fast path, our TTBL loader and ATAGs) or no `-kernel` (full path, CPU starts at NOR 0).
- Code lives in a QEMU fork as `hw/arm/s3c2443.c` (SoC), `hw/arm/homtom.c` (board), `hw/*/s3c24xx_*.c` (ported blocks), `hw/usb/s3c-hsudc.c`. Built in a Docker image from a pinned QEMU commit; sources cloned under `.runtime/emu/src/`, our patches kept in the repo under `emulator/qemu-patches/`.
- Disk images live in `.runtime/emu/images/`, always copies. Every run that writes NOR writes to a copy; a test compares it with the expected result.
- Tests (pytest, in the repo): boot to shell on serial within N seconds; `/proc/mtd` content; screenshot of the splash via VNC; touch round trip; NOR write/readback; enumeration replay; telnet over the gadget.

## 8. Phases

Estimates are working days for one person who knows QEMU internals, including tests. Ranges reflect the unknowns noted.

### Phase 0: userspace preview in qemu-user (prototype done)

- Done: device initramfs runs; framebuffer and touch over VNC; device `fbset` and splash work.
- Remaining: get the OpenTom `bin/lib` (one read-only copy from the device, or an OpenTom build), run `start.sh` with `nano-X`/`nxmenu`, ship a `pointercal` for the 1:1 touch mapping, and add a homtom UI launch target.
- Success test: `nxmenu` visible in VNC; a click on a menu entry starts the app (log line in `nxmenu.log`).
- Effort: 0.5 day with a device copy; 1-2 days via an OpenTom build. Caveat: OABI support in qemu-user is deprecated, so the image pins Debian trixie's QEMU 10.0.

### Phase 1: kernel to a shell on serial

- Work: machine skeleton with `unimplemented-device` everywhere; TTBL loader + ATAGs; UART; INTC (S3C2443 sub-sources); PWM timer 4; clock/power register file; GPIO with GSTATUS1 and the GO 630 strap/pin levels; WDT/RTC; I2C that NAKs; HSMMC1 via `s3c-sdhci` + `emmc` with an INTERNAL image built by mtools from `build/ttsystem` + the OpenTom tree; `pflash_cfi02` mapped at 0 (so `/proc/mtd` and ngffs work).
- Success test: serial shows `Detected type 25, name TomTom GO 630`, `mmcblk1: mmc1:0001 ...`, `Found: EON EN29LV400BB`, and a BusyBox prompt; `cat /proc/mtd` lists `phys_mapped_flash`; `ls /mnt/sdcard/opentom` works. Automated: pytest waits for the prompt on the serial socket.
- Early dividend: with pflash in place, `firmware/tools/norflash` can be run against the model in this phase. Add a `write-protect` mode to the model (erase/program ignored, status bits per datasheet) to test hypothesis 1 of `docs/flash-safety.md` and the tool's timeouts. Hypothesis 2 (ngffs mounted) reproduces as is.
- Effort: 8-12 days (CPU choice untested; IRQ/timer bring-up is usually where the time goes).

### Phase 2: LCD and touch in a window

- Work: port the legacy LCD controller (LCDCON1-5, LCDSADDR1-3, TFT 16 bpp RGB565, LCD interrupts) to a QEMU graphic console; ADC with `ADCMUX`, interrupts and the GPIO-driven touch planes; QEMU VNC with websocket; the web UI with noVNC and Power/Reset/USB buttons over QMP; `homtom-board` GPIO object.
- Success test: OpenTom `start.sh` brings up `nano-X`/`nxmenu` on the emulated LCD in the browser; a click on a menu item opens it; Power produces a key event seen by `power_button`; Reset reboots to the prompt. Automated: VNC screenshot hash of the splash and a scripted tap.
- Effort: 7-10 days.

### Phase 3: real NOR bootloader

- Work: start from NOR at reset; DRAMC/SSMC register files; trace-driven completion of whatever stage 1 and the bootloader poll (PLL lock, PMIC on I2C, battery ADC, SD/eMMC, LCD); HSMMC SDMA reads for the FAT; ATAGs from the bootloader instead of our loader.
- Success test: from cold reset the emulator shows the stock TomTom boot screen (or homtom images after a test flash), loads `ttsystem` from the emulated eMMC and reaches the phase 1 prompt, with the bootloader's ATAGs (`Detected type 25` without our loader). Then the graphics test: build a NOR image with `scripts/build-homtom-nor.py`, write it with the real `norflash` under the emulated kernel, reset, and compare the boot screen to the expected render.
- Effort: 10-25 days. Wide range: closed code, unknown polling loops. Mitigation: gdbstub (`-s`), MMIO trace; disassembly notes are kept out of the repository.

### Phase 4: storage mode and USB

- Work: HSUDC register model (EP0-EP8 FIFOs, EP_INT/SYS_STATUS, byte counts, max packet, DMA registers unused at first); built-in host with the enumeration replay; ECM data path to netdev user with hostfwd; BOT/SCSI client for storage mode; `usb-connected` wiring.
- Success tests: (a) with the stock module set the replay reproduces the v2 stall at the 80-byte descriptor read; with the v3 driver it completes enumeration; (b) `telnet 127.0.0.1 2323` from the Mac reaches the guest's telnetd over `usb0` (192.168.1.10); (c) cold boot with USB plugged enters the bootloader storage mode and the host reads sector 0 of the eMMC image identical to the file.
- Effort: 12-20 days (optional usbredir bridge to a Linux guest: +5-10 days).

Total to a full-device emulator: about 8-14 weeks of focused work. Phases 0-2 alone (about 4 weeks) already cover the UI, kernel and NOR write-path tests.

## 9. First steps (exact)

1. User action, next time the device is in storage mode anyway (read-only, no device change): copy `/Volumes/INTERNAL/opentom/bin`, `lib` and `etc` into `artifacts/opentom-dist-snapshot/`. Over Telnet when it works: save `/proc/cpuinfo` (Revision = `system_rev` for the ATAG), `/proc/iomem`, `/proc/interrupts` and full `dmesg`.
2. Finish phase 0: `docker run ... -v $PWD/artifacts/opentom-dist-snapshot:/opentom:ro homtom-emu-phase0 /bin/sh /mnt/sdcard/opentom/start.sh`; fix whatever `nano-X` asks of the framebuffer that the shim lacks.
3. Create `emulator/qemu/Dockerfile`: Debian trixie arm64, build deps, mainline QEMU pinned to commit `56afd82` (the clone in `.runtime/emu/src/qemu-mainline`), `--target-list=arm-softmmu --enable-vnc --disable-docs`.
4. Add `hw/arm/homtom.c` with: `arm926`, 64 MiB at 0x30000000, a 1 MiB `unimplemented-device` over each S3C peripheral window, and the TTBL loader with ATAGs. Boot `build/ttsystem` with `-d unimp,guest_errors -serial stdio`. Even before any UART model, `-d unimp` shows whether the decompressor runs (writes to 0x50000020 UTXH0).
5. Port the UART from `qemu-mini2440/hw/s3c2410.c` until `Uncompressing Linux... done, booting the kernel.` appears; then INTC, timer 4, clocks and GPIO, following the unimplemented-access log line by line.
6. Build `internal.img` (2,007,040 KiB FAT, no partition table, like mmcblk1) with mtools in Docker, holding `ttsystem` and `opentom/`; attach via `s3c-sdhci` at 0x4A800000 (IRQ 20).
7. Map `pflash_cfi02` with EN29LV400BB IDs and geometry over a copy of the newest `artifacts/bootloader-dump-*/mtd0.bin`; check that the guest's `/proc/mtd` and ngffs mount match the device.

## Sources

- QEMU removed features: <https://www.qemu.org/docs/master/about/removed-features.html> (PXA/OMAP2/cheetah removed in 9.2)
- QEMU deprecated features, OABI and NWFPE in linux-user: <https://www.qemu.org/docs/master/about/deprecated.html>
- QEMU invocation (VNC websocket, cocoa display): <https://www.qemu.org/docs/master/system/invocation.html>
- Mainline QEMU CPU list: <https://gitlab.com/qemu-project/qemu/-/blob/master/target/arm/tcg/cpu32.c>; `s3c-sdhci` in `hw/sd/sdhci.c`, `emmc` in `hw/sd/sd.c`, `vmnet-*` in `qemu-options.hx` (local clone, commit 56afd82, version 11.1.50)
- mini2440 QEMU: <https://repo.or.cz/qemu/mini2440.git> (local clone `.runtime/emu/src/qemu-mini2440`, commit 18b91f22)
- OpenMoko under QEMU (status page, now behind a bot wall): <https://wiki.openmoko.org/wiki/Openmoko_under_QEMU>; pkgsrc note on qemu-neo1973: <https://pkgsrc.se/wip/qemu-neo1973>
- qemuPrime S3C2416: <https://github.com/opcod3/qemuPrime> (branch `s3c2416`, local clone `.runtime/emu/src/qemu-prime-s3c2416`); HP Prime emulator discussion: <https://www.hpmuseum.org/forum/post-221060.html>
- OpenTom distribution sources: <https://github.com/LeddaZ/opentom>
- Kernel: `.runtime/kernel-build` (LeddaZ/opentom-kernel cc70c3e): `arch/arm/mach-s3c2410/mach-tomtomgo.c`, `tomtomgo-type.c`, `tomtomgo-iomodena.h`, `s3c2443*.c`, `time.c`, `devs.c`; `drivers/barcelona/adc/adc.c`, `drivers/barcelona/usbmode/s3c24xx_usbmode.c`, `drivers/video/tomtom/ttgfb.c`, `drivers/mmc/host/s3c-hsmmc.c`, `drivers/usb/gadget/s3c-udc-hs.c`, `drivers/mtd/chips/jedec_probe.c`; `include/asm-arm/arch-s3c2410/map.h`, `regs-hsudc.h`, `regs-hsmmc.h`, `regs-clock.h`
- Device evidence: local device boot logs and a local NOR dump layout (kept out of the repository)

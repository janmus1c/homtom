# Expansion: free signals, Wi-Fi and an ESP32 coprocessor

Status: research output, 2026-11-07. Desk study only. Nothing was run on the device for this document. It extends `docs/HARDWARE_MAP.md` and `docs/PERIPHERALS.md` and does not repeat their register recipes.

Tags: **[V]** verified, with the kind of proof in the citation (stock source, stock boot log, 6.1 tree, or a lab measurement). **[H]** hypothesis: plausible but not proven. Every [H] row names the measurement that settles it.

Abbreviations as in HARDWARE_MAP: `S` = `.runtime/kernel-build/` (stock 2.6.13), `M` = `S/arch/arm/mach-s3c2410/`, `B` = `S/drivers/barcelona/`, `L` = `.runtime/linux-6.1/`, `iomodena.h` = `M/tomtomgo-iomodena.h`, `H` = `kernel/homtom/mach-homtom-go630.c`, `SNAP` = `.runtime/ro.out` (lab read-only register snapshot, 2026-11-07).

## 0. What the project does not have

There is no PCB photo, no schematic, no connector pinout and no teardown note in the repository. The PCB is `DA0VF8MB8C0 REV.C` (user-reported, `docs/HARDWARE.md`). As a result:

- No pad, test point or connector contact on this board has a known location. Every "where is it physically" question below is open.
- The "30-pin dock connector" is not documented anywhere in the project. Its contact count, and whether this unit has a separate mount connector next to the USB socket, are unconfirmed. The stock source describes what the dock signals are (section 1.3), not where they are.
- Pad voltage is not measured. The SD slot runs at 3.2-3.4 V (`ocr_avail` in `L/drivers/mmc/host/s3cmci.c:1101`), which suggests that the GPE bank uses 3.3 V I/O. **[H]** The GPH (UART) bank is also 3.3 V. To settle it, measure a pin the snapshot shows driven high (GPH2 TXD1 idle, or GPB0 backlight) against GND with a multimeter.

Photos of both PCB sides and of every external connector are the cheapest next step for everything in this file.

## 1. Signal inventory

### 1.1 Pin table (expansion candidates)

"State now" is the register state in `SNAP` under the running 6.1 image (GPHCON `0x11a5aaa5`, GPHDAT `0x107c`, GPJDAT `0xac2`, PCLKCON `0xffffffbe`).

| Signal | Pin | Use on GO 630 (JB) | State now | Pad location | Tag / evidence |
|---|---|---|---|---|---|
| TXD0 | GPH0 | Unused on JB (GPS TX on the J8x boards) | Output, driven **low** | Unknown | [V] `iomodena.h:224-227` (J8x), absent from the JB table; SNAP |
| RXD0 | GPH1 | Unused on JB | Output, driven **low** | Unknown | [V] same |
| nCTS0 | GPH8 | Unused on JB (GPS CTS on J8x) | Output, low | Unknown | [V] `iomodena.h:227`; `L/arch/arm/mach-s3c/regs-gpio-s3c24xx.h:404` |
| nRTS0 | GPH9 | Unused on JB (GPS RTS on J8x) | Output, low | Unknown | [V] `iomodena.h:226`; `regs-gpio-s3c24xx.h:408` |
| TXD1/RXD1/CTS1/RTS1 | GPH2/3/10/11 | Bluetooth BC4 (BCSP) | Function 2, in use | On board | [V] HARDWARE_MAP 3.1; LESSONS (BT works) |
| TXD2/RXD2 | GPH4/5 | SiRF III GPS (NMEA 4800) | Function 2, in use | On board | [V] HARDWARE_MAP 3.2 |
| TXD3 | GPH6 | Dock UART (`dockdev=ttySAC3`) | **Function 2 already** | Dock connector [H] | [V] `iomodena.h:55,83`; SNAP |
| RXD3 | GPH7 | Dock UART, break-detected dock presence | **Function 2 already** | Dock connector [H] | [V] `iomodena.h:84`; `B/gpio/gpio.c:315-375` |
| RXD_DOCK_INT | GPG2 | Parallel input on RXD3 for dock detection | Input | Dock connector [H] | [V] `iomodena.h:85` |
| DOCK_PWREN | GPJ12, active low | Switches a supply to the dock | GPJ12 = 0, so the dock supply is **on** | Dock connector [H] | [V] `iomodena.h:82`; SNAP |
| SPICLK0/MOSI0/MISO0 | GPE13/12/11 | Bit-banged SPI to the dead-reckoning chips. Also the HS-SPI0 pins | Function 2 or GPIO, not used by us | DR sensor footprint [H] | [V] `iomodena.h:68-70`; `regs-gpio-s3c24xx.h:251-255` |
| ACC_SPI_CSB / CMP_SPI_CSB | GPL12 / GPL11, active low | KXP74 / AK8970N chip selects (not fitted on the 630 [H]) | Output | DR footprint [H] | [V] `iomodena.h:117-118` |
| EN_DR_PWR | GPJ0 | Supply switch for the DR plane | 0 (off) | DR footprint [H] | [V] `iomodena.h:120`; SNAP |
| DR_CMP_RST | GPJ2, active low | Compass reset | Output | DR footprint [H] | [V] `iomodena.h:121` |
| I2C SDA/SCL | GPE15/GPE14 | Hardware I2C, bus registered by stock at 128 kHz; no confirmed client | Not registered in 6.1 | Unknown | [V] `iomodena.h:113-114`; LOG 106-108 |
| SD slot CLK/CMD/DAT0-3 | GPE5/6/7-10 | NO NAME card, `s3cmci` via `homtom-sdi.ko` | In use (mmcblk1) | Slot contacts (no soldering) | [V] LESSONS; `docs/STATUS.md` |
| SD_PWR_ON / CD / WP | GPJ11 high / GPF1 low / GPF7 | Slot power, card detect, write protect | GPJ11 = 1, card present | Slot | [V] SNAP; HARDWARE_MAP 3.10 |
| FACTORY_TEST_POINT | GPB9 | Factory test strap, use unknown | GPBDAT bit 9 = 1 | A test pad [H] | [V] `iomodena.h:115` |
| TYPE_SUB_ID0/1 | GPB7/GPB8 | Board sub-ID straps (read only, never drive) | Input | Resistor straps [H] | [V] `iomodena.h:133-134` |
| CDCLK_12MHZ | GPH13 | Left as input by stock; CLKOUT0 on the 2443 | Input | Unknown | [V] `iomodena.h:75`; `M/tomtomgo-type.c:2308-2311` |
| Spare pins | GPB5, GPB6, GPF5, GPG9, GPG10, GPJ3, GPJ13, GPL10 | No stock function | Stock drives unused pins to a fixed level | Unknown; may not be bonded out | [V] PERIPHERALS U12 |
| Headphone / line-in detect | GPG3 / GPF3, active low | Jack detects | GPG3 reads 0 with nothing plugged in | Jacks (not confirmed on the case) | [V] SNAP; [H] "all external dock hardware has a HEADPHONE_DETECT pullup" (`B/dock/dock_cagliari.c`) may explain the 0 |
| ACPWR / IGNITION / USB_HOST_DETECT | GPF2, active low | External power present (one pin, three names) | 0 (plugged) | USB VBUS and dock power [H] | [V] `iomodena.h:99-100,106` |
| LOW_DC_VCC | GPF6, active low | Low battery or DC lost interrupt | 1 | — | [V] `iomodena.h:119` |
| Battery sense | ADC mux 0 | Battery voltage through a divider | about 656 raw | — | [V] PERIPHERALS live probe; divider ratio [H] |
| JTAG TCK/TMS/TDI/TDO/nTRST | Dedicated SoC balls | Debug | — | **Unknown** | [H] `docs/NOR_PLAN.md:59` |

### 1.2 UARTs

| Port | Device in 6.1 | Users | Free? | Tag |
|---|---|---|---|---|
| UART0 | `/dev/ttySAC0` registered (`H:104-108`). Pins not muxed (GPH0/1 are GPIO outputs) and clock gated (PCLKCON bit 0 = 0) until the port is opened | None. No console: `console=ttySAC0` stalls (LESSONS) | **Yes, if the pads can be reached.** It also has RTS/CTS on GPH8/9 | [V] |
| UART1 | `/dev/ttySAC1` | Bluetooth BC4 | No | [V] |
| UART2 | `/dev/ttySAC2` | SiRF GPS | No | [V] |
| UART3 | **Not available.** The 6.1 config has `CONFIG_SERIAL_SAMSUNG_UARTS=3` because `CPU_S3C2410=y` is set (`L/drivers/tty/serial/Kconfig:250-261`; `.runtime/linux-trim/.config` is the same), and the board passes only 3 uartcfgs. The resources exist (`L/arch/arm/mach-s3c/s3c24xx.c:269-293`, IRQ `IRQ_S3C2443_RX3`) | Dock UART in stock | Free, but needs a kernel image change | [V] |

GPH6/7 are already in UART function in the snapshot even though 6.1 does not drive UART3. **[H]** The bootloader sets them, which could mean the bootloader uses UART3 for a log or a dock protocol. To check (read-only): read ULCON3 `0x5000C000`, UCON3 `0x5000C004` and UBRDIV3 `0x5000C028` with `devmem`. A non-zero UBRDIV3 means that something set a baud rate.

### 1.3 Dock / mount connector (stock-source view only)

On Modena the stock source shows the following, all [V] from source:

- The dock UART is `ttySAC3` on GPH6/7. Dock presence is detected by watching RXD3 for breaks and for a held-high level, with a parallel input on GPG2. The code comment says it "NEEDS PULLDOWN" (`B/gpio/gpio.c:315-375`).
- `DOCK_PWREN` (GPJ12) switches a supply out to the dock.
- `ACPWR` and `IGNITION` are the same pin, GPF2, so car-ignition sensing and USB/DC presence are not distinguished.
- Neither stock dock driver binds on Modena. `dock.c` needs `DOCK_INT` and `dock_cagliari.c` needs `DOCK_SENSE1`, and `iomodena.h` sets neither (`M/mach-tomtomgo.c:528-549`). The boot-log banners "Valencia/Murcia dock driver" and "Cagliari dock driver" (LOG 83-84) are drivers loading without a device.
- No TMC receiver (`RDS_RST` absent, `gopins.h:721`), no line-out or external-mic pins (`EXTMIC_DETECT` and `MIC_SW` are absent), and no `DOCK_I2CEN`.

**[H]** The mount contacts carry DC in, GND, TXD3/RXD3 and the DOCK_PWREN supply. To settle it, power the unit off and check continuity from each mount contact to GND, to the GPF2 net and to the GPH6/GPH7 balls. Do this only after the photo has located them.

### 1.4 Other items asked about

| Item | Finding | Tag |
|---|---|---|
| SD slot as SDIO | `s3cmci` implements SDIO interrupts (`enable_sdio_irq`, `SDIIMSK_SDIOIRQ`; `L/drivers/mmc/host/s3cmci.c:289-306, 611-616, 1243`). The build uses `MMC_S3C_PIO=y`, so every byte goes through the CPU | [V] source; SDIO on this host has never been run [H] |
| External GPS antenna connector | No antenna sense (`GPS_ANT_OPEN/SHORT` unset), so the software cannot see one. Whether the case has a connector is unknown | [V] `gopins.h:93-94`; physical [H] |
| Headphone/mic jacks | Detect pins exist. The internal mic works (LESSONS, select 1 right channel). A 3.5 mm jack on this case is unconfirmed | [V] pins; [H] jack |
| FM transmitter Si4710 | Rails GPJ4/GPJ5/GPL13 are defined, but the 630 clears the feature flag (`iomodena.h:200`). The part may still be soldered | [H] — read-only I2C probe at 0x11/0x63 (PERIPHERALS U9) |
| FM2010 echo DSP | `aectype=GOAEC_FM`, I2C 0x60, driver not built | [H] presence — same I2C scan |
| TMC receiver | Not on Modena | [V] `gopins.h:721` |
| SCP1000 barometer | Driver exists (`B/baro/scp1000_baro.c`) but needs `BARO_SPI_CSB`, which Modena does not set. This is the only "EEPROM" reference in the tree | [V] `gopins.h:353,638` |
| EEPROMs | None defined on Modena. `EEPROM_AT24` was dropped in the trim as having no hardware path | [V] `workspace/kernel-trim/defconfig.diff` |
| Dead-reckoning plane | Powering GPJ0 changed nothing in ADC mux 1/3. The KXP74 and AK8970N are probably not fitted. If their footprints are empty, they are ready-made SPI0 pads | [V] lab probe (PERIPHERALS); footprints [H] — photo |
| ADC spare channels | Mux 1 about 220-380, mux 3 about 220-250, mux 8 about 150-200 (wobbling, probably floating). Mux 7 equals mux 9 (75), so they may be the same net or an alias. These are usable as 0-3.3 V analog inputs **if** their pads can be found | [V] lab probe; usable [H] |
| Light sensor | ADC mux 2, enabled by LX_EN GPJ1 | [V] |
| USB host | OHCI exists (`s3c_device_ohci`, `L/arch/arm/mach-s3c/devs.c:970`; gate HCLKCON bit 11). Not built (`USB_OHCI_HCD` off; the trim removed the USB host stack). Shares the one PHY and connector with the gadget link; the board cannot supply VBUS | [V] PERIPHERALS U8 |
| Power budget | The LTC3455 (GPIO-controlled) makes the rails. Which buck feeds which rail, and how much headroom is left, is not in the source | [H] — LTC3455 datasheet plus measuring the 3.3 V rail current, or skip it (section 2.3) |

### 1.5 Undefined or untested items worth probing (all read-only first)

1. UART3 registers ULCON3/UCON3/UBRDIV3 (does the bootloader use the dock UART?).
2. GPH0/GPH1/GPH8/GPH9 pad location, by slow GPIO toggling (section 3, step 2).
3. ADC mux 1, 3, 7 and 8 with a known voltage applied once the pads are found (resolves floating versus a fixed net).
4. I2C read-only scan with the FM rails powered (Si4710, FM2010, or anything unexpected). Needs `s3c_device_i2c0` registered; `I2C=y` is already in the trim config.
5. GPB9 factory test point: what level the bootloader expects (read only, never drive).
6. GPH13 (CLKOUT0): whether any clock is present (a frequency counter or logic analyser on the pad).
7. HEADPHONE_DETECT reading 0 with nothing plugged in: measure the GPG3 net with the codec on and off.

## 2. Adding Wi-Fi and processing

### 2.1 Options

| # | Option | Kernel/driver needs | TomTom-side userland | RAM/CPU on the S3C2443 | Board work | Effort | Risk |
|---|---|---|---|---|---|---|---|
| A | **ESP32 on UART0, application protocol.** The ESP32 owns Wi-Fi, TLS and MQTT/HTTP to HA. The TomTom writes JSON lines (statusd output) and reads commands | None. `ttySAC0` exists; only the GPH0/1 (+8/9) mux is needed (masked GPHCON write now, board-file `cfg_gpio` later) | `stty` (in BusyBox) plus a small freestanding reader/writer, or statusd writing to the tty | Negligible | 3-5 fine wires to unknown pads | S | Low-medium (pad access) |
| B | **ESP32 on UART0, SLIP (full IP).** The ESP32 routes with lwIP NAPT. Existing HTTP statusd, FTP and shell work over Wi-Fi unchanged | `CONFIG_SLIP=m` (currently not set in either config). It changes `.config`, so a new matched image plus modules is needed (LESSONS) | A tiny freestanding tool to set the N_SLIP line discipline (same pattern as `tools/devtools/hciattach.c`), then `ifconfig sl0` / `route` (both in BusyBox) | Small: 921600 baud is about 90 KB/s, plus interrupt load at that rate | Same as A, plus RTS/CTS strongly advised | M | Low-medium |
| C | Same link, but **UART3 through the dock connector** | `SERIAL_SAMSUNG_UARTS` to 4 (drop `CPU_S3C2410` or patch the Kconfig) and a 4th uartcfg in `H` | As A or B | As A/B | Possibly **none**, if the mount carries TXD3/RXD3 [H] | M | Medium (kernel change; dock pinout unknown) |
| D | **ESP-Hosted over SPI** (HS-SPI0 on GPE11-13, a CS line, plus handshake and data-ready GPIOs, ideally EINT-capable) | Out-of-tree `esp_hosted` module built for 6.1 ARMv4T [H: never tested on ARMv4T]; `SPI_S3C64XX` (handles `s3c2443-spi`, currently not set) or `spi-gpio`; for -NG also `cfg80211` (trimmed out) | -NG: wpa_supplicant (no ARMv4T build exists in the project; the userland is BusyBox 1.22.1 on glibc 2.3.2). -FG: an Ethernet-style netdev, but its control library needs a libc build | cfg80211 plus driver about 0.5-1 MB [H estimate]; PIO SPI is CPU-bound, a few Mbit/s at best [H] | 5-7 wires, possibly on empty DR footprints [H] | L | Medium |
| E | **SDIO on the SD slot** (ESP32 or ESP32-C6 as SDIO slave with ESP-Hosted, or a Broadcom/Marvell SDIO module with brcmfmac/mwifiex/libertas) | SDIO function driver plus cfg80211 (mac80211 too for some). Firmware blobs are held in RAM (hundreds of KB) [H]. PIO-only host | wpa_supplicant (except ESP-Hosted-FG) | About 1-2 MB for the stack and firmware [H estimate]; 34 MB free (`docs/PERF.md`) | **No soldering on the TomTom**: a microSD extender/breakout cable into the slot. The SD card is lost (logs move to INTERNAL) | L | Medium (s3cmci SDIO untested; driver bloat) |
| F | **USB host (OHCI, 12 Mbit/s)**: Wi-Fi dongle (rt2800usb, ath9k_htc) or an ESP32-S3 acting as a USB CDC-ECM/NCM device | USB core + OHCI + board PHY switch; for a dongle also mac80211/cfg80211 and firmware. ECM to an ESP32-S3 needs only `usbnet`/`cdc_ether` | wpa_supplicant for a dongle; none for ECM | Dongle stack about 1.5-2 MB [H] | Host cable with external VBUS (the unit cannot supply it) | L | **High**: same connector and PHY as the dev link and charging; host mode drops the USB network |

On SDIO, the ESP32-C3 and ESP32-S3 have no SDIO slave. Use the classic ESP32 or an ESP32-C6 for option E [V Espressif datasheets; not lab-tested].

### 2.2 What an ESP32 coprocessor can take over (options A-C)

- **Wi-Fi association, WPA2/3, TLS, MQTT.** The ARM920T would need a modern TLS stack built for ARMv4T; the ESP32 has crypto acceleration. This removes the wpa_supplicant and libc problem entirely.
- **BLE.** The BC4 is Bluetooth 1.2 Classic only (LESSONS). The ESP32 adds BLE presence scanning, or an HA Bluetooth proxy (ESPHome `bluetooth_proxy`).
- **802.15.4 (ESP32-C6 only).** Thread or Zigbee radio for HA [H: firmware choice, not evaluated here].
- **Sensors.** Put I2C sensors (temperature, pressure, motion) on the ESP32 instead of enabling the S3C I2C bus.
- **GPS assistance.** Fetch time and an approximate position over Wi-Fi and pass them to the TomTom, which sends them to the SiRF in binary mode [H: message format not verified; PERIPHERALS U6].
- **Watchdog and out-of-band console.** The ESP32 can log the TomTom's UART during boot and, later, pulse the pinhole reset line if that is wired. This could help with the recovery gaps in LESSONS [H].

### 2.3 Power and logic levels

- Logic: **[H]** both sides are 3.3 V CMOS, so no level shifter is needed, but measure first (section 0). Put a **1 kΩ series resistor** in every signal line. It limits current if a pin is driven both ways (GPH0/1 are outputs driven low until re-muxed, and the bootloader re-drives them on every reboot) and when one side is unpowered (back-powering through ESD diodes).
- Never connect the ESP32's 3V3 to a TomTom rail, and do not power the ESP32 from the TomTom's 3.3 V. ESP32 Wi-Fi transmit peaks are a few hundred mA (Espressif datasheet figures), and the LTC3455 rail headroom is unknown. Power the ESP32 from its own supply: the enclosure's 5 V source if the node is always plugged in, or a separate buck-boost from the battery pack if battery operation matters. **Connect GND only.**
- Battery: 1320 mAh Li-ion (`iomodena.h:17`). An always-associated ESP32 averages tens of mA, which halves battery runtime if both run from the pack [H estimate]. Fine for a mains-powered HA node with the battery acting as a UPS.
- DOCK_PWREN is currently on (GPJ12 = 0). Do not load that supply until its voltage and current limit are measured.

### 2.4 Ranked recommendation

1. **A, then B: ESP32-C3 or ESP32-C6 on UART0 with RTS/CTS.** No kernel change for A, and a small module plus one tool for B. This removes the whole Linux Wi-Fi stack from the 64 MB ARMv4T side, keeps the USB and Bluetooth links untouched, and is the smallest change to a working system. ESP firmware: start with an ESP-IDF UART-to-MQTT/HTTP bridge or ESPHome with a UART component (A). Then use ESP-IDF with SLIP netif and `LWIP_IPV4_NAPT` (B) [H: check the exact Kconfig names in the IDF version used]. Choose the C6 if Thread/Zigbee is wanted. **Blocker:** finding the GPH0/1/8/9 pads.
2. **C, the same link through the dock connector**, if photos and continuity show TXD3/RXD3 on the mount. It costs one kernel image change (UART count) and would give a no-solder, enclosure-friendly link. Before that, settle the bootloader's use of UART3 (section 1.5, item 1).
3. **E, SDIO on the SD slot with an ESP32-C6 running ESP-Hosted**, only if the UART throughput (about 90 KB/s) turns out too low. It needs no board soldering, but brings a cfg80211 or Ethernet-style host stack and a libc problem.
4. **D, SPI ESP-Hosted.** Same software burden as E, plus fine soldering, and it is CPU-bound on bit-banged or PIO SPI.
5. **F, USB host.** Conflicts with the dev link and charging, has no VBUS, and needs the largest stack. Last.

## 3. First experiment (safe, about one evening)

Goal: prove a bidirectional serial link on a free UART between the TomTom and the Mac, then swap the Mac for an ESP32. Nothing in this experiment touches GPJ10/GPJ14, the NOR or HSMMC.

**Buy:** a 3.3 V USB-UART adapter with a FT232R or CP2102N (it must be 3.3 V I/O, not 5 V); 0.1 mm enamelled or 30 AWG Kynar wire; flux; 1 kΩ 0603 or through-hole resistors; Kapton tape; an ESP32-C3 or ESP32-C6 dev board. Optional: an 8-channel 24 MHz logic analyser (sigrok-compatible) and a microSD extender cable for option E later.

1. **Photograph and read (no power changes).** Take photos of both PCB sides and every connector. Then run a read-only `devmem` of UART3 at `0x5000C000`, `0x5000C004` and `0x5000C028`, and confirm `/dev/ttySAC0` exists (`tools/homtom-sh 'ls -l /dev/ttySAC0; devmem 0x5000C028'`). Record the result as an artifact.
2. **Find the UART0 pads by toggling** (user at the device, case open, USB attached). GPH0 is unused on JB and already an output, so toggling its data bit is a masked write that changes nothing else: `devmem 0x56000074 1 1`, wait 2 s, `devmem 0x56000074 0 1`, repeated about 20 times in a loop. While the loop runs, probe candidate pads and vias near the SoC with a multimeter against GND, looking for a 0 ↔ 3.3 V square wave. The high level also measures the I/O voltage. Repeat with mask `2` (GPH1), `0x100` (GPH8) and `0x200` (GPH9). Stop if any other pin or function changes. If the mount contacts are exposed, check their continuity to the found pads, and to GPH6/7 (dock UART), with the unit unpowered.
3. **Mux UART0.** Set GPH0/1 to function 2 with a masked write: `devmem 0x56000070 0xA 0xF`. Do this **before** connecting anything to RXD0, because GPH1 is currently driving low. For flow control later: `devmem 0x56000070 0xA0000 0xF0000` for GPH8/9.
4. **Wire through 1 kΩ:** TomTom GPH0 (TXD0) → adapter RX, adapter TX → GPH1 (RXD0), GND ↔ GND. Leave the adapter's VCC unconnected.
5. **Test.** On the device: `stty -F /dev/ttySAC0 115200 raw -echo; echo hello-homtom > /dev/ttySAC0; cat /dev/ttySAC0 > /tmp/rx &`. On the Mac: `screen /dev/tty.usbserial-* 115200`, then type a line and read `/tmp/rx` back with `homtom-sh`. Repeat at 921600 (the BT UART already runs at that rate on the same clock). Pass: a 4 KB block in each direction with matching hashes at both speeds, stored as a hashed artifact.
6. **Swap in the ESP32** (separately powered, GND common, same 1 kΩ resistors): flash a UART echo, then a UART-to-MQTT bridge. Check that an HA entity updates from a line the TomTom writes. Only after that, build the SLIP image (option B) as its own ladder image with matching modules.
7. **Make it permanent:** add GPH0/1 (+8/9) function 2 to the board file so a reboot keeps the mux. Until then, the bootloader puts the pins back to output-low on every boot; the series resistors make that harmless.

Bonus from step 4: leave the adapter attached across one reboot. If the bootloader prints on UART0 (or, with the adapter moved to GPH6, on UART3), that log helps the NOR plan (`docs/NOR_PLAN.md`, "UART" item).

## Top three to probe or buy

1. **PCB photos plus the GPH0/1/8/9 toggle hunt** (and continuity from the mount contacts to GPH6/7). Locating a reachable UART decides between options A/B and C.
2. **A read-only look at UART3 (`UBRDIV3` at `0x5000C028`)** to learn whether the bootloader uses the dock UART. It costs one command.
3. **Buy a 3.3 V FT232R/CP2102N adapter, fine wire, 1 kΩ resistors and an ESP32-C6 (or C3) dev board**, and run section 3 steps 3-6.

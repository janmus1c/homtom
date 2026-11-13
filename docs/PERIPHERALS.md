# GO 630 peripheral inventory: what we do not use yet

Status: research output, 2026-11-07. Built from the stock TomTom 2.6.13 source, the stock boot log, the OpenTom userland files and the 6.1 tree. Nothing was run on the device. Extends `docs/HARDWARE_MAP.md` (pin and register facts there are not repeated unless they matter here) and does not replace it.

Tags: **SRC** read from stock source, **LOG** stock boot log, **6.1** read from `.runtime/linux-6.1` or our board file, **OT** OpenTom userland under `.runtime/emu/opentom`, **INF** inference, **LAB** measured by this project (see `docs/LESSONS.md`).
Abbreviations: `S` = `.runtime/kernel-build/`, `M` = `S/arch/arm/mach-s3c2410/`, `B` = `S/drivers/barcelona/`, `L` = `.runtime/linux-6.1/`, `H` = `kernel/homtom/mach-homtom-go630.c`, `LOG` = `artifacts/trial-v5-device-logs-20261102T083925Z/INTERNAL/lab-usb-followup-dmesg.log`. `iomodena.h` = `M/tomtomgo-iomodena.h`.
Polarity: stock `PIN_INVERTED` = active low. GPIO base `0x56000000` (CON +0, DAT +4). Ports: B `0x10`, C `0x20`, E `0x40`, F `0x50`, G `0x60`, H `0x70`, J `0xD0`, L `0xF0`.

Hard rules for every recipe here: GPJDAT (`0x560000D4`) writes are masked so bit 10 (`PWR_RST`, high = power off) and bit 14 (moviNAND power, low = on) keep their value; never touch GPJ10/GPJ14 or HSMMC1 (`0x4A800000`); no write to the bootloader NOR; ADC has one owner at a time (stop the status endpoint's ADC polling before a sweep, see LESSONS "Opus advisor items").

## 1. What this board does NOT have (settled by source, so stop looking)

| Feature | Evidence |
|---|---|
| Buzzer | `IO_HaveBuzzer()` needs `BUZZER_EN`; Modena never sets it (`S/include/barcelona/gopins.h:693`; `iomodena.h:57-134`). The speaker replaces it |
| Camera | `IO_HaveCamera()` needs `CAMDATA0` (`gopins.h:692`); no `CAM*` pin in `iomodena.h`. GPJ0-15 are all assigned except GPJ3 and GPJ13 |
| Barometer | `IO_HasBaro()` needs `BARO_SPI_CSB` (`gopins.h:638`); not set on Modena |
| LEDs, vibration motor, IR transceiver | No pin for them in `iomodena.h`; the `LED0-3` pins are SMDK-board debug LEDs (`gopins.h` struct, "Debug LED's on SMDK boards"). The charge LED, if any, is charger-hardware driven (INF) |
| GPS antenna sense, GPS EEPROM | `GPS_ANT_OPEN/SHORT` exist in the struct (`gopins.h:93-94`) but Modena sets neither; the only "EEPROM" hit in the tree is the SCP1000 barometer. Antenna state cannot be read |
| Remote (FSK), touchpad, GSM, external UARTs, RDS/TMC, hard disk | `IO_HaveRemote/TP/GprsModem/ExternalUart/TMCReceiver` all key on pins Modena lacks (`gopins.h:691-701,721`). The "Touchpad driver" banner in the boot log (LOG, grep `Touchpad`) is a driver loading without hardware |
| Separate VBUS source | No `USB_PWR_EN`/`USB_VBUS` pin set: the unit cannot power a USB device (matters for host mode, item U8) |
| Hardware crypto, TRNG, JPEG, 2D engine | Stock `config-default:1152` has `CRYPTO` off and no driver; the S3C2443 gate list in 6.1 has no crypto or JPEG block, the `2d` gate belongs to S3C2450 (`L/drivers/clk/samsung/clk-s3c2443.c:216`). INF: none usable |
| NAND controller | No raw NAND: storage is NOR (EN29LV400BB 512 KiB, LOG grep `EN29LV400BB`) plus moviNAND. Controller `0x4E000000` (`L/arch/arm/mach-s3c/map-s3c24xx.h:70`) is irrelevant. NOR already has backups (`docs/flash-safety.md:46-47`) |
| Accelerometer, gyro, compass | Probably not fitted (HARDWARE_MAP 3.3), but see U10: stock still powers their plane |

## 2. Inventory of unused, partly used or undiscovered items

Status: **used** (by us), **partly**, **unused**, **unknown** (presence not proven). Risk is for the probe/enable step described, not for reading.

### U1. Backlight PWM and panel power (status: unused)
- Pins/regs: `BACKLIGHT_PWM` GPB0 = TOUT0 function 2, not inverted (`iomodena.h:97`; `S/drivers/video/backlight/s3c_bl.c:106-137,196-212`). PWM timer block `0x51000000`: TCFG0 +0, TCFG1 +4, TCON +8, TCNTB0 +0xC, TCMPB0 +0x10. Stock: 1000 Hz range 160 (Sharp) or 1839 Hz range 100 (Samsung) (`M/tomtomgo-type.c:1159-1170`). Panel power `LCD_VCC_PWREN` GPC6 active low, `LCD_RESET` GPC0 (`iomodena.h:94-95`). Today GPB0 is only held high, i.e. 100 % brightness (`H:123-126`).
- Use: devmem recipe in HARDWARE_MAP section 5 item 3 (touch only TCON bits 0-3, TCFG0[7:0], TCFG1[3:0]; timers 3/4 are the clocksource, `L/drivers/clocksource/samsung_pwm_timer.c:81-95`). Later `pwm-samsung`+`pwm_backlight` (`PWM_SAMSUNG=y`, `BACKLIGHT_PWM=m` in `L/.config`).
- Risk: low. A wrong prescaler only flickers the backlight. Never write TCON bits 16-22.
- HA value: high. Screen is the largest load and LED wear on a 24/7 node: dim/off entity, night mode, light-sensor auto-dim (stock did exactly this with the photosensor: `B/pwm/pwm.c:78-92`).

### U2. Speaker, codec WM8750/WM8971, mic, line-in and headphone detect (status: unused)
- Pins/regs: codec control is 3-wire write-only bit-bang `L3MODE` GPB3, `L3CLOCK` GPB4, `L3DATA` GPB2, words `(reg<<9)|data` (`iomodena.h:79-81`; `B/sound/spi.c:9-37`). I2S: LRCK GPE0, SCLK GPE1, MCLK GPE2, SDI GPE3, SDO GPE4 (`iomodena.h:73-78`); IIS block `0x55000000`; clock gate PCLKCON bit 9. Power `DAC_PWR_ON` GPJ15, amp `AMP_ON` GPG11, differential OUT2 (`iomodena.h:39,71-72`; `B/sound/driver.c:1411-1418,1930-1950`). Detects: `HEADPHONE_DETECT` GPG3 low = plugged, `LINEIN_DETECT` GPF3 low = plugged (`iomodena.h:104-105`). Capture: `canrecordaudio=1` (`iomodena.h:36`), codec reg R25 enables MICBIAS, R33 selects MIC path (`B/sound/wm8971.c:142,150`); no `MIC_SW`/`EXTMIC_DETECT` on Modena, so the mic is the fixed internal one (INF); line-in vs mic select `wm8971_switch_line_in` (`wm8971.c:468-477`).
- Use: (a) cheapest: userland chime. Bit-bang the stock register init (`wm8971.c:22-150`), start MCLK/IIS, push PCM into the IIS FIFO by polling (PIO, no DMA, INF), short beeps only. (b) proper: `snd-soc-wm8750` SPI codec on `spi-gpio` plus a `s3c2412-iis` device at `0x55000000` and a machine driver (HARDWARE_MAP 3.9), needs a kernel image. Mic: same driver, capture stream.
- Risk: low to moderate. The L3 bus is write-only so success is only audible. Do not run the amp while the battery is low (stock shuts it on `LOW_DC_VCC`, `gopins.h:595`) and never drive full-scale into the amp.
- HA value: very high. Local chime/TTS/alert, doorbell tone, and with the mic a sound-level/clap/alarm-sound sensor. Headphone/line detect are free binary sensors (GPG3/GPF3 reads, zero risk).

### U3. Battery and charge control (status: partly: charging, mains, raw ADC read)
- Pins/regs: charger LTC3455, GPIO only. `BATT_TEMP_OVER` GPF4 active low: input = charging allowed, driven active = charging disabled (`B/bat/bat.c:127-133`; `iomodena.h:66`). `CHARGING` GPG1 low = charging (`bat.c:150-160`). `PWR_MODE` GPG6 high = burst, low = PWM (`B/pmic/ltc3455-pmic.c:44-90`). `USB_PWR_BYPASS` GPG5 high = high-current wall mode (`iomodena.h:109`); our board file already drives it high (`H:128-136`). `LOW_DC_VCC` GPF6 (EINT6) low-battery/DC-lost interrupt (`iomodena.h:119`; `bat.c:60-71`). `ACPWR` GPF2. ADC mux 6 = charge current, 0 = battery, 4 = reference (HARDWARE_MAP 3.4). Battery 1320 mAh Li-ion (`iomodena.h:17`). Calibrated mV/percent is still an estimate (`docs/STATUS.md` phase 5).
- Use: userland devmem. Read-only: GPFDAT bit 4, GPGDAT bits 1/5/6, ADC mux 6 over a charge cycle. Active: set GPF4 as output low (disable charge) / back to input (enable), masked write to GPFCON bits 9:8 and GPFDAT bit 4.
- Risk: moderate for the write step. Unproven whether "charge disabled" also removes the VBUS-to-system path of the LTC3455; if it does the unit falls back to battery (no damage, but it could drain). Test only with a charged battery and watch `external_power`/battery for a minute. GPG5 stays high only on a wall adapter or a port that supplies 1 A (INF: from a 500 mA host port it can brown out the host).
- HA value: high. Charge limiter (stop at about 80 % for a permanently plugged unit, battery longevity), mains-loss interrupt = poor man's UPS sensor, charge-current graph, over-temp flag (GPF4 read) as a battery-hot alarm.

### U4. RTC and alarm (status: unused in 6.1)
- Pins/regs: RTC `0x57000000` (`L/arch/arm/mach-s3c/map-s3c24xx.h:79`): INTP +0x30, RTCCON +0x40 (bit 0 enable), TICNT +0x44, RTCALM +0x50, ALMSEC.. +0x54, BCDSEC +0x70 up to BCDYEAR +0x88 (`L/drivers/rtc/rtc-s3c.h:12-65`). `keeprtcpowered=1` (`iomodena.h:35`). Stock boot log: "rtc disabled, re-enabling" (LOG, grep `RTC`), so at that boot the counter was not running. `RTC_DRV_S3C=y` is built but the board registers no RTC (`H:191-196`), so no `/dev/rtc0` and the clock restarts at 1970 every boot (INF).
- Stock uses the alarm to wake from sleep (`M/tomtomgo-wake.c:91-158,456-537`) with sources `ON_OFF`, `ACPWR`, `CD_SD`, RTC.
- Use: reboot-free: a small module registering `s3c_device_rtc` (same pattern as `homtom-sdi.ko`), then `hwclock`. First read RTCCON and the BCD registers with devmem; only meaningful if RTCCON bit 0 is set.
- Risk: low (RTC registers only). Whether the RTC survives battery removal or a drained battery is unknown (no coin cell known).
- HA value: medium. Correct timestamps when HA is unreachable, scheduled tasks, and the building block for timed wake later (see U11, which is not safe yet).

### U5. Bluetooth (status: partly: chip alive, BCSP and HCI by hand, discoverable)
- Pins/regs: see HARDWARE_MAP 3.1 and `docs/LESSONS.md` "Bluetooth chip alive". CSR BC4 (LMP 3 = Bluetooth 1.2, so Classic only, no BLE: LESSONS). `handsfree/headsetgw/a2dp` all 1 (`iomodena.h:13-15`); no BT PCM pin exists, so voice would be SCO over HCI (INF). OpenTom shipped `bnep`/`rfcomm`/`hciattach`/PAN scripts (`.runtime/emu/opentom/bin/start_bt*`, `etc/init_bluetooth`).
- Use: userland only. Presence tracker: periodic page/name request or inquiry for known phone BD_ADDRs (HA `bluetooth_tracker` style); RFCOMM/SPP serial to a phone or HA host; BNEP PAN as a network link that frees the single USB connector; A2DP source to a speaker (SBC on ARM920T, heavy).
- Risk: none to hardware. Keep the crash-loop breaker (`homtom/bt.trying`).
- HA value: high (presence and a wireless link are the product's field transport).

### U6. GPS extras (status: partly: NMEA at 4800 baud works, no fix indoors)
- Pins/regs: `GPS_1PPS` GPG0 (EINT8) (`iomodena.h:63`; `B/gps/gps.c:381-382`), `GPS_REPRO` GPJ8 must stay low, `GPS_RESET` GPJ7, `GPS_ON` GPJ6. `gpsephemeris=1` (`iomodena.h:42`): stock fed extended ephemeris through closed `clmapp` and saved `/var/run/clmdata` across suspend (`.runtime/emu/opentom/bin/rc.suspend`, `suspend_gps`). No antenna sense, no standby pin on JB (section 1).
- Use: (1) read GPG0 as PPS (GPGDAT bit 0, or `pps-gpio`) once a fix exists; (2) SiRF binary mode and aiding (send approximate position and time from HA to cut time to first fix, INF about message format); (3) trickle or power-cycle GPS with GPJ6 to save power; (4) persist almanac only if SiRF keeps it through power-off (battery-backed RAM unknown).
- Risk: low. Never raise GPJ8 (reflash mode) and do not send firmware-load sequences.
- HA value: medium-high: location entity, geofence, and a precise time source (PPS) for the RTC when outdoors/near a window.

### U7. Power key and wake sources (status: partly: polled for state only)
- Pins/regs: `ON_OFF` GPF0 (EINT0) active low; `ACPWR` GPF2 (EINT2); `CD_SD` GPF1 (EINT1) (`iomodena.h:60,98-100`). Stock wakes from suspend on ON_OFF, ACPWR, SD insert or RTC (`M/tomtomgo-wake.c:175,456-537`); the OpenTom `suspend` script does `echo mem > /sys/power/state` (`.runtime/emu/opentom/bin/suspend`). Short/long-press meaning in the closed TomTom app is INF. Our pwrkey poller exists (`H:437-472`) and status shows `power_key`.
- Use: userland: turn the poller into press/double/long events (uinput is `=m`, `L/.config`), publish to HA. SD insert/remove (GPF1) and write-protect (GPF7) as HA events.
- Risk: none for events. Do not map a long press to poweroff until `poweroff` semantics are fixed (LESSONS: `poweroff` reboots).
- HA value: high for cost: a physical button on the wall, plus tamper (SD removed, mains lost).

### U8. USB host (status: unused; hardware capable)
- Pins/regs: `ohciports=0x01`, `usbdevicehostcapable=1`, no `usbhost` flag (`iomodena.h:32-33`). Stock switches mode with `echo host > /sys/devices/platform/tomtomgo-usbmode/mode` (`.runtime/emu/opentom/bin/start_usbhost_mode:3`), OHCI port 0 via PHYCTRL `DOWNSTREAM_PORT` and PHYPWR (`S/drivers/usb/host/ohci-s3c2410.c:77-97`), HCLKCON bit 11 (USB host, `L/drivers/clk/samsung/clk-s3c2443.c:122`) and UCLKCON host clock (`B/usbmode/s3c24xx_usbmode.c:213-247`), PHY rail `USB_PHY_PWR_EN` GPG8 (`s3c24xx_usbmode.c:249-290`). OHCI is not built in 6.1 (`USB_OHCI_HCD` off, `L/.config`).
- Use: needs a kernel with `USB_OHCI_HCD` plus board code to set the PHY downstream; host mode and the device-mode ECM link share the same PHY and the same single connector, so entering host mode takes down the dev link.
- Risk: moderate. The unit cannot supply VBUS (section 1): a bus-powered dongle will not power up; use a powered hub/cable. A host cable also needs the D+/D- pull-downs (stock note, `S/drivers/barcelona/usbmode/usbmode.c:463-466`). Keep a watchdog-reset recovery path.
- HA value: high but late: USB serial/Zigbee dongle bridge (ser2net-like), USB sound card, USB Ethernet. Requires the network link to move to Bluetooth PAN first.

### U9. Hardware I2C bus, Si4710 FM transmitter, FM2010 DSP (status: unknown presence)
- Pins/regs: I2C `HW_IIC_SDA` GPE15, `HW_IIC_SCL` GPE14 (`iomodena.h:113-114`); IIC block `0x54000000`; stock registers the bus on Modena (`M/mach-tomtomgo.c:253-256`, LOG: "i2c-0: S3C I2C adapter", 128 kHz). FM: `EN_FM_PWR` GPJ4, `FM_RST` GPJ5 low, `EN_FM_RCLK` GPL13 low (`iomodena.h:123-125`); `fmtransmittertype=SI4710` stays set (`iomodena.h:28`) while the 630 clears the user flag (`iomodena.h:200`), so the software switch is off, the part may still be soldered (the 530/730 share the PCB family: `iomodena.h:138-180`). FM2010 echo canceller (I2C 0x60, needs 100 kHz, `mach-tomtomgo.c:245-251`) is not built (`S/config-default:692`). In 6.1 I2C is off: `homtom_defconfig:274-276` lists I2C options but there is no `CONFIG_I2C=y`, so `L/.config` shows `# CONFIG_I2C is not set` and those lines are dropped.
- Use: read-only presence test: power the FM rails as stock, release `FM_RST`, and scan the bus with read transactions only (Si4710 answers at 0x11 or 0x63, FM2010 at 0x60). Either via the IIC registers from devmem (small C helper) or after enabling `CONFIG_I2C` and registering `s3c_device_i2c`.
- Risk: low if read-only. Avoid quick-write scans (they write to devices). FM transmit has legal power limits (INF: use only the lowest power, for an on-desk receiver).
- HA value: low-medium: notification audio on any FM radio; an unexpected I2C device would be a win.

### U10. Dead-reckoning plane: KXP74 accelerometer, AK8970N compass, analog gyro (status: unknown, probably absent)
- Pins/regs: `EN_DR_PWR` GPJ0 (`iomodena.h:120`); stock turns this plane on at boot for every board that defines the pin, including this one (`M/mach-tomtomgo.c:396-404`). SPI is bit-banged: CLK GPE13, MOSI GPE12, MISO GPE11; `ACC_SPI_CSB` GPL12, `CMP_SPI_CSB` GPL11, `DR_CMP_RST` GPJ2 (`iomodena.h:68-70,117-121`). Gyro on ADC mux 1 and 3 (`iomodena.h:47-48`; `B/adc/adc.c:872-962`); ADC mux 7 and 8 are used by nothing. Only the 930 sets `deadreckoning=1`, `acctype=KXP74` (`iomodena.h:183-192`).
- Use: read-only: sample ADC mux 1, 3, 7, 8 with GPJ0 low and then high; floating inputs drift or sit at a rail, a gyro sits mid-scale and moves with rotation. Then, if anything looks real, one SPI register read from each chip with CS toggled.
- Risk: low. GPJ0 must be a masked write (GPJDAT keeps bits 10 and 14).
- HA value: if present, high (tilt, vibration, tamper, door/washer vibration). Otherwise close the item.

### U11. Sleep, wake, CPU clock scaling (status: unused; stock had it)
- Pins/regs: stock 2443 sleep path `M/sleep.S:117-145` (PWRMODE `0x4C000040`, PWRCFG `0x4C000060`, RTC wake bit via `S3C2443_PWRCFG_RTC_CFG`, `tomtomgo-wake.c:135-158`), resume re-enters the bootloader (`BOOTLOADER_START_ADDRESS 0x8024`, `tomtomgo-wake.c:36`). 6.1 has `CONFIG_SUSPEND=y` but no S3C2443 PM code (its only PM files are 2410/2412/2416, `L/arch/arm/mach-s3c/pm-*.c`). Clock: CLKDIV0 `0x4C000024` ARMDIV bits 12:9 (`L/drivers/clk/samsung/clk-s3c2443.c:238-250`); core voltage pin `LOW_CORE` GPG7 (`iomodena.h:102`; `ltc3455-pmic.c:44-52`) was only for stock cpufreq; `CONFIG_CPU_FREQ` is off in 6.1.
- Use: ARMDIV only (leave LOW_CORE alone), reduce CPU clock when idle. Sleep needs a port of the stock path: not a quick win.
- Risk: sleep: high (a failed entry or resume cannot be debugged with no console and may strand the unit until the pinhole reset; storage-mode is also not guaranteed, LESSONS "Storage mode needs an unlocked Mac"). Clock: medium (HCLK/PCLK divided clocks also feed UART baud and ADC timing; USB PHY clock is independent).
- HA value: medium (battery runtime in outages), but only after U3 and U4.

### U12. Dock UART and the unassigned pins (status: unknown routing)
- Pins/regs: `dockdev=ttySAC3`, TXD GPH6, RXD GPH7, `RXD_DOCK_INT` GPG2, `DOCK_PWREN` GPJ12 active low (`iomodena.h:55,82-85`; `M/tomtomgo-type.c:1439-1446`). UART0 TX/RX GPH0/1 unused on JB (HARDWARE_MAP 3.11). Spare pins no stock function uses: GPB5, GPB6, GPF5, GPG9, GPG10, GPH8, GPH9, GPJ3, GPJ13, GPL10. Stock drives all unused pins to a fixed level (`M/tomtomgo-type.c:1656-1695`), so a CON/DAT read does not show whether a pad exists.
- Use: only physical inspection (PCB photo, continuity) can show if any of these reach a connector or test pad. Do not reconfigure them blind.
- Risk: `DOCK_PWREN` drives a supply to the connector; driving it into an unknown load is the one real risk. Leave it deasserted.
- HA value: unknown; a free 3.3 V UART (GPH6/7) would take an external sensor bus.

### U13. Smaller SoC items
- ADC spare channels 1, 3, 7, 8 (see U10); LSB noise could seed `/dev/random` (INF, low value).
- Scratch registers INFORM0-3 `0x4C000070-7C` survive resets: INFORM0 is the boot handshake (LESSONS), INFORM1 may be a jump target (do not write); INFORM2/3 could carry a crash counter but their bootloader use is unverified.
- Free internal timers 1-2 have outputs on GPB1/GPB2 (BT_MODE, L3DATA), so only TOUT0 is usable externally.
- Light sensor (used): lux calibration and auto-dim are still open. SD slot (used): store-and-forward log (GPS track, sensor history) while HA is away; SD power GPJ11 stays on while mounted.
- Touch (unresolved, `docs/STATUS.md`): OpenTom's calibration (`.runtime/emu/opentom/etc/pointercal`, coefficients `-34898 168 33229056 -253 -22440 19888250 65536`) implies stock raw X about 50-950 and raw Y about 100-890 (INF): compare with our mux 5/9 samples.

## 3. Top 10 opportunities (value to a Home Assistant edge node, risk and effort weighed)

| Rank | Item | Why it ranks here | Effort | Risk |
|---|---|---|---|---|
| 1 | U1 backlight dim/off + panel power | Cheapest big win: power, LED life, night mode, auto-dim | tiny | low |
| 2 | U2 speaker chime/TTS (mic later) | Only local output channel besides the screen; detects are free | medium (PIO beep) to large (ASoC) | low-mod |
| 3 | U3 charge limit, mains-loss IRQ, charge current | Battery longevity and UPS-like signals on an always-plugged unit | small | moderate on the write |
| 4 | U4 RTC module | Correct time offline, prerequisite for scheduling | small | low |
| 5 | U5 BT presence + PAN/SPP | Presence is a headline HA feature and PAN frees the USB port | medium | none |
| 6 | U7 button/tamper events | One wall button, SD-removed and mains-lost events | small | none |
| 7 | U6 GPS 1PPS, aiding, trickle | Location, geofence, time source | medium | low |
| 8 | U10 DR plane probe | Zero cost to check; big payoff if a sensor exists | tiny | low |
| 9 | U9 I2C scan + FM probe | Same cheap probe style; may reveal FM or other parts | small | low |
| 10 | U8 USB host | Biggest hardware reach (dongles) but blocked by the shared port, no VBUS and kernel work | large | moderate |

Not in the ten: U11 sleep/clock (do after U3/U4; sleep is the highest-risk item here), U12 pads (needs a physical look), U13 extras.

## 4. Safe-probe plan (read-only first, in order)

Every step has a read stage with no state change, then at most one masked write. Run via `homtom-sh`, keep sessions short. Scripts: `workspace/peripherals/ro-snapshot.sh` (all register reads and the decoded input pins) and `workspace/peripherals/adc-sweep-ro.sh` (ADC mux 0-9). Both only read, apart from the ADC registers `homtom-hw adc` already uses.

0. Snapshot: run `ro-snapshot.sh` once idle and once per state change (cable in/out, SD in/out, headphone in, button held). It reads GPIO CON/DAT, HCLKCON/PCLKCON, RTC, INFORM and decodes GPC7, GPF0-4, GPF6/7, GPG0-3, GPG5/6 (settles panel type, SD present, write-protect, charging, mains, headphone, line-in, PPS). Zero risk.
1. U1: read TCFG0/TCFG1/TCON/TCNTB0/TCMPB0 and GPBCON[1:0]; then set GPB0 low for 1 s and high again (`devmem 0x56000014 0 1`, `... 1 1`); then the PWM setup of HARDWARE_MAP section 5 item 3 with TCON bits 0-3 only. Watch brightness.
2. U3: log GPG1, GPF2, GPF4, GPF6 and ADC mux 6 and 0 every 10 s through a charge/discharge cycle. Only then, with a full battery and mains present, drive GPF4 for 60 s and watch `external_power`, battery raw and charging; release immediately if the unit shows any dropout.
3. U4: read RTCCON, then BCDSEC..BCDYEAR. If RTCCON bit 0 is 0 the BCD values are not meaningful. Then register the platform device in a module and use `hwclock`.
4. U2 (detects first): read GPG3 and GPF3 with and without a plug. For output: read GPJ15 and GPG11 as left by the bootloader; only then the L3 init sequence with `AMP_ON` low, DAC power on, then a short low-volume tone, then amp on. Stop at the first pop or sag in battery raw.
5. U5: repeat the existing `homtom-bt-up` sequence and add inquiry/name requests for a known phone; nothing on the GPIO side.
6. U7: read GPF0 at 10 Hz for a minute while pressing; derive short/double/long thresholds in userland.
7. U6: read GPG0 (PPS) only after `gga` shows a fix; log edge timing against NMEA time.
8. U10: with GPJ0 low and high, ADC mux 1, 3, 7, 8 five samples each (`adc-sweep-ro.sh`); only if values are structured, one bit-banged SPI read per chip (CS low, one register read, CS high).
9. U9: read GPJ4/5, GPL13 states; power the FM rails as stock; scan 0x08-0x77 with read transactions only; power rails back down.
10. U8: do nothing on the device until a powered-hub cable exists and an OHCI-enabled image has been staged as its own ladder image; first read-only check: HCLKCON bit 11 and UCLKCON.

## Live probe results (2026-11-07, read-only snapshot plus one masked GPJ0 toggle)
- Panel: LCD_ID GPC7 = 0, so the Samsung LTE430WQ (backlight PWM about 1839 Hz).
- Backlight: PWM timer 0 is already running (TCFG0 0x1800, TCFG1 0, TCNTB0 0x4617, TCMPB0 0x0702, TCON 0x59; 66.5 MHz PCLK gives about 1853 Hz) but GPB0 is configured as a plain output held high (GPBCON 0x00115405), so brightness is 100 % and the timer output is unused. Switching GPBCON[1:0] to function 2 and writing TCMPB0 is the whole change (tools/devtools/homtom-bl does it).
- Inputs: external power GPF2 = 0 (plugged), SD card present, line-in unplugged, BATT_TEMP_OVER high (charging allowed), LOW_DC_VCC high, headphone GPG3 reads 0 while nothing is plugged in (sense likely inverted or floating: unverified).
- ADC sweep (quiet system): mux0 656 battery, mux2 2-5 light (LX_EN high), mux4 0 (reference unpowered unless GPL14), mux5 940, mux6 0, mux7 and mux9 both 75 (same net or alias), mux1 about 220-380, mux3 about 220-250, mux8 about 150-200 wobble (floating inputs?). Powering the dead-reckoning plane (GPJ0 high) changed nothing clear: no evidence of motion sensors on this unit.
- The ADC has one owner at a time: a leftover touch logger or the status endpoint polling corrupts a sweep (seen twice).
- RTC is present but not enabled (RTCCON 0); I2C is not built into the kernel (defconfig lacks CONFIG_I2C=y).

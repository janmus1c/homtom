# GO 630 hardware map (Roadmap phase 3)

Status: research output, 2026-11-06. Built entirely by reading the stock source, the stock boot log and the 6.1 tree, with nothing run on the device. Every row says where its fact comes from, so a later live measurement can confirm or overturn it.

## Sources and confidence tags

| Tag | Meaning |
|---|---|
| **SRC** | Read directly from the stock TomTom 2.6.13 GPL source (`.runtime/kernel-build`, byte-identical to `artifacts/source/opentom-kernel-cc70c3ea…` for `arch/arm/mach-s3c2410` and `drivers/barcelona`). |
| **LOG** | Read from the stock boot log `artifacts/trial-v5-device-logs-20261102T083925Z/INTERNAL/lab-usb-followup-dmesg.log` (device output captured by the project). |
| **OWN** | user-reported in `docs/HARDWARE.md` (the project has not re-measured it). |
| **INF** | Inference: plausible, not proven by source or measurement. |
| **6.1** | Read from `.runtime/linux-6.1` (6.1.191, the tree `kernel/scripts/docker-build.sh` builds) and its `.config`. |

Abbreviations: `S` = `.runtime/kernel-build/`, `M` = `S/arch/arm/mach-s3c2410/`, `B` = `S/drivers/barcelona/`, `L` = `.runtime/linux-6.1/`. Pin polarity: stock `PIN_INVERTED` means **active low**. `PIN_OPEN_COLLECTOR` = float when off, drive **low** when on. `PIN_OPEN_EMITTER` = float when off, drive **high** when on (`M/tomtomgo-iopins.h:42-43`).

`emulator/user/barcelona.txt` looks like a `/proc/barcelona` dump, but its header says it was synthesised from source. It is **not** device evidence.

## 1. Which board variant this is

| Fact | Evidence | Tag |
|---|---|---|
| Stock kernel sets `id = system_rev & 0xffff` (main) and `| (system_rev >> 16) << 16` (sub) | `M/tomtomgo-type.c:1366-1417` | SRC |
| `/proc/cpuinfo` revision `b000a` → main 0x000a = `GOBOARD_MODENA` (10), sub 0x0b = `GOBOARD_MODENA_JB` | `S/include/barcelona/goboard.h:79,140`; cpuinfo in `docs/BOARD_CAPABILITIES.md` | SRC + lab measurement |
| JB runs `IO_InitModena()` then `IO_InitModenaJA_B_C()`, **not** the `J8x` "ModenaBarracuda" path | `M/tomtomgo-type.c:496-498, 843-846`; `M/tomtomgo-iomodena.h:195-212` | SRC |
| Boot log: "Detected type 25, name TomTom GO 630" (`GOTYPE_MODENA` = 25) | LOG line 9; `S/include/barcelona/gotype.h:46` | LOG |
| Serial prefix `JB` matches the sub ID | `docs/HARDWARE.md` | OWN |

Consequence: the GPS is a **SiRF III on ttySAC2 (UART2)**, not the BCM4750 on UART0 that the `J8x` boards use. Everything below assumes the JB variant.

## 2. Register quick reference (S3C2443)

GPIO base `0x56000000`: each port has CON at +0, DAT at +4, UDP at +8. Port offsets: B `0x10`, C `0x20`, E `0x40`, F `0x50`, G `0x60`, H `0x70`, J `0xD0`, L `0xF0` (`L/arch/arm/mach-s3c/regs-gpio-s3c24xx.h:77-448`). Each pin uses 2 CON bits: 00 input, 01 output, 10 function 2.

Clock gates (`L/drivers/clk/samsung/clk-s3c2443.c:120-261`): HCLKCON `0x4C000030` (bit 9 LCD, bit 16 HSMMC1); PCLKCON `0x4C000034` (bits 0-3 UART0-3, 5 SDI, 6 SPI0, 7 ADC, 9 I2S0, 10 PWM, 13 GPIO). Common clk turns off unused gates at late init. The MMC lesson (registers read 0) applies to every block below: check the gate before trusting a 0 read.

Peripherals: UART `0x50000000 + n*0x4000`, PWM `0x51000000`, IIC `0x54000000`, IIS `0x55000000`, ADC `0x58000000` (ADCCON +0, ADCTSC +4, ADCDAT0 +0xC, ADCMUX +0x18), SDI `0x5A000000`, HSMMC `0x4A800000`, legacy LCD `0x4D000000`, TFT `0x4C800000`.

**Write hazards.** GPJDAT bit 10 is `PWR_RST` (high = power off) and bit 14 is moviNAND power (low = on). Any write to `0x560000D4` must be masked so those two bits keep their current values. Timer 3/4 is the 6.1 clocksource, and `TCON` bits 16-22 and `TCFG0[15:8]` belong to it (`L/drivers/clocksource/samsung_pwm_timer.c:81-95`).

## 3. Function map

### 3.1 Bluetooth

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Chip | CSR BlueCore4 (`GOBT_BC4`); the BC4-ROM datasheet is cited in the reset code | `M/tomtomgo-iomodena.h:9`; `S/include/barcelona/gopins.h:850`; `B/gpio/gpio.c:633` | SRC |
| Transport | **UART**, `btdev` = `ttySAC1` (UART1). Not USB: the `hci_usb`/`bfusb` modules on disk are generic and stock config builds `BT_HCIUART` with H4 and BCSP | `iomodena.h:53`; `S/config-default:327-330`; LOG lines 109-111 | SRC/LOG |
| Pins | TXD1 GPH2, RXD1 GPH3, CTS GPH10, RTS GPH11 (function 2) | `iomodena.h:88-91` | SRC |
| Flow control | Stock serial driver turns on AFC with RTS/CTS for the BT port; RTS trigger at 32 bytes | `S/drivers/serial/s3c2410.c:1120-1150` | SRC |
| Baud | `btspeed` 921600 | `iomodena.h:10`; exported at `B/hw/hwdrv.c:208` | SRC |
| Reference clock | `btclock` 16 MHz (PSKEY_ANA_FREQ for a ROM part) | `iomodena.h:11` | SRC (meaning INF) |
| Class of device | 0x280408 (hands-free/audio) | `iomodena.h:12` | SRC |
| Reset | `BT_RESET` GPJ9, **active low**. Stock pulses it low-high-low-high, 40 ms each, then waits 100 ms | `iomodena.h:92`; `B/gpio/gpio.c:621-640` | SRC |
| Mode | `BT_MODE` GPB1, active high, set from ioctl bit 0 before reset. Likely a BC4 PIO that selects boot or host-transport mode | `iomodena.h:93`; `gpio.c:626-629` | SRC (meaning INF) |
| Power enable | None defined for Modena. The radio is probably always powered and controlled only by reset | `iomodena.h` (no BT_PWR pin) | INF |
| Host protocol | Not in the kernel. TomTom userland ran `hciattach` (OpenTom's `start.log` shows `killall hciattach`). For BC4-ROM the usual default is **BCSP at 115200** until PS keys raise it to 921600 | `artifacts/.../start.log:3` | INF |
| Firmware | ROM part, so there is no firmware file. PS keys (baud, ANA_FREQ, BD address) must be pushed at every boot | — | INF |
| Mainline 6.1 | `hci_uart` with BCSP or H4 (`CONFIG_BT_HCIUART=m`, BCSP=y, H4=y in `.config`). Userspace `hciattach -s 115200 ttySAC1 bcsp` then `bccmd` (BlueZ) to set PS keys, or a serdev node. The board file needs GPH2/3/10/11 set to function 2 and GPJ9 released. `homtom_uartcfgs[1]` already registers ttySAC1 | `L/.config:1076-1101`; `kernel/homtom/mach-homtom-go630.c:104-108` | 6.1 |

### 3.2 GPS

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Chip | SiRF Star III (`GOGPS_SIRF3`) | `iomodena.h:27`; `gopins.h:840` | SRC |
| UART | `ttySAC2` (UART2): TXD2 GPH4, RXD2 GPH5. No RTS/CTS defined for GPS on JB | `iomodena.h:54, 86-87` | SRC |
| Power | `GPS_ON` GPJ6, active high (`gps_power()`; no `GPS_STANDBY` on JB) | `iomodena.h:62`; `B/gps/gps.c:122-132` | SRC |
| Reset | `GPS_RESET` GPJ7, **active low**, 50 ms pulse | `iomodena.h:61`; `gps.c:134-148` | SRC |
| Boot or reflash | `GPS_REPRO` GPJ8, active high. Keep it **low** for normal operation (driver deactivates it before power-on) | `iomodena.h:64`; `gps.c:150-158, 375-377` | SRC |
| 1PPS | `GPS_1PPS` GPG0 (EINT8), interrupt on activation | `iomodena.h:63`; `gps.c:381-382` | SRC |
| SiRF power-on side effect | The SiRF branch also sets RTS_BT/CTS_BT and TXD/RXD_BT to function, so the stock driver configures the BT UART pins | `gps.c:332-335, 370-385` | SRC |
| Baud and protocol | Not in the kernel (TomTom's closed `ttn` set it). SiRF III ROMs start in **NMEA 4800** or **SiRF binary 57600/115200**, depending on the firmware build | — | INF |
| Mainline 6.1 | No kernel driver needed: plain `ttySAC2` (registered by `homtom_uartcfgs[2]`) plus a userspace NMEA/SiRF parser (gpsd optional). GPIOs through gpio-cdev or a tiny board-file helper. Optional `pps-gpio` on GPG0 | `mach-homtom-go630.c:104-108` | 6.1 |

### 3.3 Accelerometer, gyro, compass (dead-reckoning parts)

| Item | Value | Evidence | Tag |
|---|---|---|---|
| GO 630 configuration | `acctype` stays `GOACC_NONE` and `deadreckoning` stays 0. Only `IO_InitModenaJ6` (GO 930) sets `deadreckoning=1, acctype=GOACC_KXP74` | `iomodena.h:183-192, 195-212`; `gopins.h:868-875` | SRC |
| Boot log | Shows "Accelerometer Driver" and "S3C24XX Generic ADC Driver" banners, but no KXP74, gyro or compass probe lines. The `acc` driver picks no read function when `acctype` is NONE | LOG 63-67; `B/acc/acc.c:242-259` | LOG/SRC |
| Accelerometer chip (if fitted) | Kionix KXP74 on GPIO SPI: CS `ACC_SPI_CSB` GPL12 active low, CLK GPE13, MOSI GPE12, MISO GPE11 | `iomodena.h:68-70, 117`; `M/mach-tomtomgo.c:462-489` | SRC |
| Compass chip (if fitted) | AKM AK8970N, CS `CMP_SPI_CSB` GPL11 active low, same SPI. Reset `DR_CMP_RST` GPJ2 active low | `iomodena.h:118, 121`; `mach-tomtomgo.c:476-477` | SRC |
| Gyro (if fitted) | Analog, ADC mux 1 (X) and 3 (Y) (`gyroxchannel`, `gyroychannel`). Registered only when `deadreckoning` is set | `iomodena.h:47-48`; `mach-tomtomgo.c:409-414`; `B/adc/adc.c:925-948` | SRC |
| Dead-reckoning power plane | `EN_DR_PWR` GPJ0, active high | `iomodena.h:120`; `mach-tomtomgo.c:396-404` | SRC |
| `/dev/acc`, `/dev/gyro`, `/dev/compass` on the stock rootfs | Static nodes from the initramfs list, not proof of hardware | `artifacts/source-configs_initramfs_prepend` | SRC |
| Conclusion | The GO 630 most likely has **no populated accelerometer, gyro or compass**. The pins are board-wide Modena definitions shared with the 930 | — | INF (a PCB photo or the SPI ID read in §5 settles it) |
| Mainline 6.1 | If present: `spi-gpio` master (GPE11-13, `CONFIG_SPI_GPIO=m`) plus `spidev` (=m) for raw reads. KXP74 and AK8970N have no 6.1 driver, so they would need userspace or a small IIO driver | `L/.config` | 6.1 |

### 3.4 ADC, battery, charger, USB power detect

ADC mux numbers are the values the stock driver writes to `ADCMUX` on the 2443 (`B/adc/adc.c:464-465`).

| Function | ADC mux / pin | Polarity / notes | Evidence | Tag |
|---|---|---|---|---|
| Battery voltage | mux **0** | Divided; scaled by the AIN4 reference: `bat * refraw / adc[REF]`. Calibration: about 0x319 raw at 3500 mV with a 3.3 V reference | `M/tomtomgo-type.c:360-368`; `adc.c:904-905`; `B/bat/bat.c:88-98, 182-201` | SRC |
| Charge current | mux **6** | | `type.c:367`; `adc.c:906-907` | SRC |
| Reference (AIN4) | mux **4**, enabled by `AIN4_PWR` GPL14 (active high) | Turned on only around reads | `iomodena.h:67`; `adc.c:591-607, 909-912` | SRC |
| Light sensor | mux **2**, enabled by `LX_EN` GPJ1 (active high; its presence enables `IO_HaveLightSensor`) | | `iomodena.h:103`; `gopins.h:688`; `adc.c:952-955` | SRC |
| Battery type | `GOBATTERY_1320` (about 1320 mAh Li-ion) | | `iomodena.h:17`; `gopins.h:915` | SRC |
| Charger / PMIC | Linear LTC3455, **GPIO-controlled only** (no I2C traffic despite the banner) | `PWR_MODE` GPG6: high = burst, low = PWM. `USB_PWR_BYPASS` GPG5: high = high-current (wall) mode. `LOW_CORE` GPG7 (active low) is unused | `iomodena.h:18-19, 102, 109, 111`; `B/pmic/ltc3455-pmic.c:44-90`; `mach-tomtomgo.c:294-307` | SRC |
| Charging status | `CHARGING` GPG1, **active low** (low = charging) | | `iomodena.h:65`; `bat.c:69, 150-160` | SRC |
| Charge disable / over-temp | `BATT_TEMP_OVER` GPF4, active low. Input = charging allowed; driven active = charging disabled | | `iomodena.h:66`; `bat.c:128-135` | SRC |
| External power / USB VBUS | `ACPWR` = `USB_HOST_DETECT` = `IGNITION` = GPF2 (EINT2), **active low** | Polled by buspower | `iomodena.h:99-100, 106`; `B/buspower/buspower.c:82-93` | SRC |
| Low battery / low DC | `LOW_DC_VCC` GPF6 (EINT6), active low, interrupt | | `iomodena.h:119`; `bat.c:60-71` | SRC |
| USB PHY | `USB_PHY_PWR_EN` GPG8, `USB_SUSPEND_OUT` GPG4, `USB_HP` GPH12 | Already driven by the homtom board file | `iomodena.h:107-110`; `mach-homtom-go630.c:128-136` | SRC |
| Mainline 6.1 | `arch/arm/mach-s3c/adc.c` client API supports the 2443 (`TYPE_ADCV11`, writes ADCMUX); register `s3c_device_adc` (renamed `s3c2443-adc`). Battery: a small homtom driver on `s3c_adc_read()`, or `generic-adc-battery` (needs IIO, which 6.1 lacks for S3C24xx). `gpio-charger` for GPG1/GPF2. `CONFIG_POWER_SUPPLY` is currently **not set** | `L/arch/arm/mach-s3c/adc.c:39-43`; `L/arch/arm/mach-s3c/s3c2443.c:69` | 6.1 |

### 3.5 Power key and power control

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Power key | `ON_OFF` GPF0 (EINT0), **active low** | `iomodena.h:98`; already used in `mach-homtom-go630.c:437-472`; idle 1 measured (LESSONS) | SRC + lab |
| Power off | `PWR_RST` GPJ10, high = off on battery | `iomodena.h:112`; LESSONS | SRC + lab |
| Mainline 6.1 | `gpio-keys` with `KEY_POWER` on GPF0 using its IRQ (`CONFIG_KEYBOARD_GPIO` is not set), or keep the current poller and add an input device | `L/.config:1751` | 6.1 |

### 3.6 Backlight

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Pin | `BACKLIGHT_PWM` GPB0 = TOUT0 (timer 0, function 2). Not inverted: high = brighter. No `BACKLIGHT_EN` on Modena | `iomodena.h:97`; `S/drivers/video/backlight/s3c_bl.c:106-137, 196-212` | SRC |
| LED driver | CAT3238TD (`GOBACKLIGHT_CH0_*_CAT3238TD_430`) | `gopins.h:825-826` | SRC |
| PWM frequency | **1000 Hz with range 160** for the Sharp LQ043T1. **1839 Hz with range 100** when the Samsung LTE430WQ is detected (see §3.8) | `M/tomtomgo-type.c:1159-1170`; `s3c_bl.c:287-293` | SRC |
| Stock driver | `CONFIG_BACKLIGHT_S3C` (`s3c_bl.c`); `BARCELONA_PWM` is not built | `S/config-default:125, 805` | SRC |
| Mainline 6.1 | `pwm-samsung` (=y) plus `pwm_backlight` (=m). Register `samsung_device_pwm` with timer 0 in the output mask, add a `pwm_lookup` and `platform_pwm_backlight_data`, and set GPB0 to function 2. Timer 0 uses prescaler0, which the clocksource (timers 3/4, prescaler1) leaves alone. Today the board file only drives GPB0 high (`homtom_lcd_gpio`) | `L/.config`; `mach-homtom-go630.c:123-126` | 6.1 |

### 3.7 Touchscreen

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Type | 4-wire resistive, read by the SoC ADC with **external FET switching on GPIOs**, not the internal ADCTSC switches | `adc.c:308-365` | SRC |
| Switch pins | `XPON` GPG12 (open-emitter, high = on), `XMON` GPG13 (open-collector, low = on), `YPON` GPG14 (open-emitter), `YMON` GPG15 (open-collector). `TSDOWN` is also GPG15 | `iomodena.h:127-131`; `tomtomgo-iopins.h:42-43` | SRC |
| ADC channels | X = mux **5** (`tsxchannel`), Y = mux **9** (`tsychannel`), pen-down = mux **9** with the ADCTSC pull-up enabled | `iomodena.h:45-46`; `adc.c:879-902` | SRC |
| Sequence | Each plane is switched one ADC period before it is sampled, because of filter capacitors. Pen-down: all FETs off, TSDOWN (GPG15) low, pull-up on, read mux 9. X: TSDOWN/YMON/YPON off, XMON low and XPON high, read mux 5. Y: XMON/XPON off, YMON low and YPON high, read mux 9 | `adc.c:308-365` | SRC |
| Pressure threshold | `TSINPUT_PRESSURE_TRIGGER` gates the X/Y reads; the input layer is `B/tsinput/tsinput.c` | `adc.c:329, 348` | SRC |
| Mainline 6.1 | `s3c2410_ts` **does not fit**: it drives the internal XP/XM/YP/YM switches and has no GPIO-FET mode or 2443 ID. Write a small homtom input driver on the `s3c_adc` client API (`s3c_adc_start` with channels 5/9 plus GPIO toggles), or prototype in userspace with devmem first | `L/drivers/input/touchscreen/s3c2410_ts.c:439-444` | 6.1 + INF |

### 3.8 LCD

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Panel selection | `LCD_ID` GPC7 read at boot: **high = Sharp LQ043T1**, **low = Samsung LTE430WQ** | `M/tomtomgo-type.c:1114-1170` | SRC |
| Fitted panel | The display board is labelled `LTE430W-C0B`, which points to **LTE430WQ** (GPC7 low). Not yet measured | `docs/HARDWARE.md` | OWN + INF |
| Controller | The stock kernel used the **legacy s3c2410-style LCD at 0x4D000000** and inherited the bootloader's framebuffer | LOG 54-55 ("Using s3c2410 lcd controller", "LFB from bootloader") | LOG |
| Common format | 480x272, 16 bpp RGB565, TFT, refresh 25-75 Hz, 95x54 mm. LCDCON1 MVAL=13; LCDCON5: FRM565, INVVLINE, INVVFRAME, INVLEND, PWREN, HWSWP | `S/drivers/video/tomtom/lte430wq.c:31-60, 95-100`; `lq043t1.c` | SRC |
| LTE430WQ timings (LCDCON register values) | VSPW 10, VBPD 0, VFPD 2, HSPW 41, HBPD 45, HFPD 7 | `lte430wq.c:36-48` | SRC |
| LQ043T1 timings (LCDCON register values) | VSPW 10, VBPD 1, VFPD 1, HSPW 41, HBPD 1, HFPD 1 | `lq043t1.c:35-45` | SRC |
| Converting for 6.1 | `s3c2410fb` writes `margin - 1` and `len - 1`, so the mainline `s3c2410fb_display` values are **register + 1**. For LTE430WQ: `vsync_len` 11, `upper_margin` 1, `lower_margin` 3, `hsync_len` 42, `left_margin` 46, `right_margin` 8 | `L/drivers/video/fbdev/s3c2410fb.c` | 6.1 + INF |
| Power, reset | `LCD_VCC_PWREN` GPC6 active low; `LCD_RESET` GPC0 active low (released 100 ms after the controller starts) | `iomodena.h:94-96`; `lte430wq.c:72-92` | SRC |
| Data and sync pins | VD on GPC/GPD (VD2-7 GPC10-15, VD10-23 GPD2-15), VCLK GPC1, VDEN GPC4, HSYNC GPC2, VSYNC GPC3 | `M/tomtomgo-type.c:700-728` | SRC |
| Mainline 6.1 | `s3c2410fb` (`CONFIG_FB_S3C2410=y`) with `s3c_device_lcd` and `s3c24xx_fb_set_platdata`. Keep the relocated buffer at 0x33F00000. The 2443 TFT block (`s3c2443-fb`, s3c-fb) is an alternative, but the stock kernel did not use it | `L/.config`; `L/arch/arm/mach-s3c/devs.c:625-640` | 6.1 |

### 3.9 Audio

| Item | Value | Evidence | Tag |
|---|---|---|---|
| Codec | Source says `GOCODEC_WM8971`; the visible chip is **WM8750BG**. WM8971 and WM8750 share a register map, so the stock WM8971 table drives it | `iomodena.h:29`; `docs/HARDWARE.md` | SRC + OWN |
| Control bus | **Not I2C.** 3-wire SPI, bit-banged: CSB = `L3MODE` GPB3 (active low during a word), SCLK = `L3CLOCK` GPB4, SDIN = `L3DATA` GPB2. 16-bit words `(reg<<9)|data`, MSB first, latched on CSB rising. Write-only | `iomodena.h:79-81`; `B/sound/spi.c:9-37`; `B/sound/wm8971.c:163-197` | SRC |
| I2S | LRCK GPE0, SCLK GPE1, CDCLK (MCLK out) GPE2, SDI GPE3, SDO GPE4. `CDCLK_12MHZ` GPH13 is left as input | `iomodena.h:73-78`; `B/sound/driver.c:1930-1950`; `B/sound/iis.c:116-123` | SRC |
| Clocking | Codec is I2S slave (`GOCODECCFG_SLAVE`); SoC IIS is master from EPLL (stock sets 32.769 MHz); 2412-style IIS registers | `iomodena.h:30`; `B/sound/iis-s3c2443.c:56-72` | SRC |
| Codec power | `DAC_PWR_ON` GPJ15, active high (50 ms settle) | `iomodena.h:72`; `driver.c:1930-1937` | SRC |
| Speaker amp | `AMP_ON` GPG11, active high. Differential (`GOCODECAMP_DIFFERENTIAL_HW`): codec OUT2 enabled through R26 bits 0x18 | `iomodena.h:39, 71`; `driver.c:1411-1418`; `wm8971.c:183-187` | SRC |
| Detects | `HEADPHONE_DETECT` GPG3 active low; `LINEIN_DETECT` GPF3 active low | `iomodena.h:104-105` | SRC |
| Echo canceller | `aectype` = `GOAEC_FM` (Fortemedia FM2010, I2C 0x60), but `CONFIG_FM2010_DSP` is not built and Modena defines no VP_* pins | `iomodena.h:40`; `S/drivers/char/fm2010/fm2010.c:28-29`; `config-default:692` | SRC (presence INF) |
| Mainline 6.1 | `snd-soc-wm8750` SPI driver (`spi_device_id "wm8750"`) on an `spi-gpio` master (sck GPB4, mosi GPB2, cs GPB3, no miso). CPU DAI is `s3c2412-i2s` (`SND_S3C2412_SOC_I2S`), but 6.1 has no 2443 IIS platform device or 2412-iis clock alias, so the board must add a `s3c2412-iis` device at 0x55000000 and a machine driver modelled on `jive_wm8750.c`. `SND_SOC_SAMSUNG` and `SND_SOC_WM8750` are off today | `L/sound/soc/codecs/wm8750.c:765-775`; `L/sound/soc/samsung/s3c2412-i2s.c:241`; `L/drivers/clk/samsung/clk-s3c2443.c:173` | 6.1 + INF |

### 3.10 SD slot (NO NAME) and moviNAND

| Item | Value | Evidence | Tag |
|---|---|---|---|
| SD controller | Legacy SDI at 0x5A000000. Stock probe: `irq=37` (IRQ(21) = `IRQ_SDI`), `irq_cd=17` (EINT1) | LOG 112-113; `L/arch/arm/mach-s3c/irqs-s3c24xx.h:44` | LOG |
| Card seen by stock kernel | `mmcblk0: mmc0:59b4 SQ08G 7703552KiB` on the SDI host | LOG 120-121 | LOG |
| Data pins | CLK GPE5, CMD GPE6, DAT0-3 GPE7-10 (function 2) | `M/tomtomgo-type.c:645-650` | SRC |
| Card detect | `CD_SD` GPF1 (EINT1), **active low** (low = card present) | `iomodena.h:60`; `S/drivers/mmc/host/s3cmci.c:679-688` | SRC |
| Write protect | `WP_SD` GPF7, not inverted (high = protected) | `iomodena.h:59` | SRC (sense INF) |
| Power | `SD_PWR_ON` GPJ11, **active high**. Stock order: power on, CMD/DAT to function, 1 ms wait, CLK to function with the SDI clock disabled, 10 ms wait | `iomodena.h:57`; `s3cmci.c:690-715` | SRC |
| Register layout | 2443 uses the 2440 SDI layout (`s3c24xx_sdidat = S3C2440_SDIDAT` by default; only 2410 overrides it) | `M/regs-dyn.c:22`; `M/s3c2410.c:168` | SRC |
| Clock gate | PCLKCON bit 5 | `L/drivers/clk/samsung/clk-s3c2443.c:261` | 6.1 |
| moviNAND (done) | HSMMC1 on port L (GPL0-7, GPL8 CMD, GPL9 CLK), `HS_MOVI_PWR_ON` GPJ14 active low | `iomodena.h:58`; `M/tomtomgo-type.c:611-620`; LESSONS | SRC + lab |
| Mainline 6.1 | `s3cmci` (`CONFIG_MMC_S3C=y`, already built). Register `s3c_device_sdi`, renamed to **`s3c2440-sdi`** (the default name `s3c2410-sdi` selects the wrong register layout). Platform data: `set_power` drives GPJ11, `ocr_avail` 3.2-3.4 V (`s3cmci.c:1101`). gpiod lookup `cd` = GPF1 `GPIO_ACTIVE_LOW`, `wp` = GPF7. Set GPE5-10 to function 2 | `L/drivers/mmc/host/s3cmci.c:1451-1476, 1745-1757`; `L/arch/arm/mach-s3c/devs.c:865-883` | 6.1 |

### 3.11 Other pins (for completeness)

| Function | Pin | Polarity | Evidence | Tag |
|---|---|---|---|---|
| HW I2C SDA / SCL | GPE15 / GPE14 | — | `iomodena.h:113-114`; LOG 106-108 (128 kHz) | SRC/LOG |
| I2C clients on a 630 | None confirmed: LTC3455 is GPIO-only, Si4710 FM is disabled (`fmtransmitter 0`), FM2010 is not built | §3.4, §3.9; `iomodena.h:200` | INF |
| FM transmitter Si4710 | `EN_FM_PWR` GPJ4, `FM_RST` GPJ5 (active low), `EN_FM_RCLK` GPL13 (active low) | `iomodena.h:123-125` | SRC (disabled on 630) |
| Dock UART3 | TXD GPH6, RXD GPH7, `RXD_DOCK_INT` GPG2, `DOCK_PWREN` GPJ12 active low; `dockdev` ttySAC3 | `iomodena.h:55, 82-85` | SRC |
| Board sub-ID straps | `TYPE_SUB_ID0/1` GPB7 / GPB8 | `iomodena.h:133-134` | SRC |
| Factory test point | GPB9 | `iomodena.h:115` | SRC |
| UART0 (GPH0/1) | Unused on JB; the `J8x` boards use it for GPS. No console is wired (LESSONS) | `iomodena.h:224-227` | SRC |
| Buzzer | None (no `BUZZER_EN` pin; `IO_HaveBuzzer()` false) | `gopins.h:693` | SRC |

## 4. Current 6.1 build state (relevant options)

From `.runtime/linux-6.1/.config`: `MMC_S3C=y`, `MMC_SDHCI_S3C=y`, `FB_S3C2410=y`, `PWM_SAMSUNG=y`, `BACKLIGHT_PWM=m`, `SPI_GPIO=m`, `SPI_SPIDEV=m`, `BT_HCIUART=m` (H4, BCSP, LL), `GPIO_CDEV=y`, `DEVMEM=y`, `MODULES=y`, `SND_SOC=y`. Not set: `I2C`, `POWER_SUPPLY`, `IIO`, `SND_SOC_SAMSUNG`, `SND_SOC_WM8750`, `KEYBOARD_GPIO`. Board file registers only WDT, HSMMC1, HSUDC and DMA. It does not register SDI, ADC, PWM, LCD, I2C or IIS.

## 5. Cheapest next live experiments (reboot-free), ranked

All of these use the `/bin/devmem` already in the initramfs, sysfs, or modules loaded over the live link. Start with reads only, and make every write masked (`devmem ADDR VALUE MASK`, the form used in LESSONS). Each experiment states its exit or decision signal.

1. **Read-only GPIO snapshot (phases 2, 4, 5, 6, 7; zero risk).** Read CON and DAT for B, C, E, F, G, H, J, L (`0x56000010…0x560000F8`) plus PCLKCON/HCLKCON. Decode against §3:
   - GPC7: panel ID (expect 0 = LTE430WQ).
   - GPF1: SD card present (expect 0 with a card inserted).
   - GPF7: write-protect sense.
   - GPF2: VBUS (expect 0 on USB).
   - GPG1: charging.
   - GPF0: power key.
   - GPG3: headphone.
   - GPJ6, GPJ7, GPJ9, GPJ11, GPJ15: GPS power/reset, BT reset, SD power, DAC power as the bootloader and 6.1 left them.
   - GPHCON: whether UART1/2 pins are already function 2.

   Record it as an artifact. This one capture settles the panel type and gives the SD-slot inputs.
2. **SD slot bring-up as a module (phase 2 exit).** First a precheck: GPF1 low, then set GPJ11 high (masked write to GPJDAT that keeps bits 10 and 14 unchanged). Set PCLKCON bit 5 and confirm SDICON (`0x5A000000`) reads non-zero. Then build a small out-of-tree module that sets GPE5-10 to function 2 and registers an `s3c2440-sdi` platform device (resources 0x5A000000 and IRQ_SDI) with the §3.10 pdata, then `insmod`. `MMC_S3C=y` is already built in, so no new kernel image is needed. Exit: `/dev/mmcblk1` reads the NO NAME VFAT.
3. **Backlight on/off, then PWM (phase 4).** Toggle GPBDAT bit 0 (`devmem 0x56000014 0 1`, then `1 1`). This proves GPB0 is the backlight. Next: GPBCON[1:0] = 10, set prescaler0 in TCFG0[7:0], set TCFG1[3:0], TCNTB0 (`0x5100000C`) and TCMPB0 (`0x51000010`) for 1839 Hz, and set TCON bits 0-3 only (manual update, then start with auto-reload). Sweep TCMPB0 and watch brightness. Leave TCON[16:22] and TCFG0[15:8] alone (they belong to the clocksource).
4. **Battery and charge ADC (phase 5).** Make sure PCLKCON bit 7 is set. Then `ADCMUX=0`, `ADCCON=0x7FC1` (prescaler on, 0xFF, start), poll ADCCON bit 15, and read ADCDAT0[9:0]. Repeat for mux 6 (charge current), mux 4 with GPL14 high (reference), and mux 2 with GPJ1 high (light sensor). Compare against roughly 0x319 raw at 3.5 V. Exit for the phase-5 values: a battery mV figure that moves when the charger is plugged in.
5. **Touch by hand (phase 4).** Using §3.7, set GPG12-15 to output/input as each step needs, and set the ADCTSC pull-up (`0x58000004`) for pen-down. Sample mux 9 (down), 5 (X) and 9 (Y) while pressing corners. A shell loop is enough to prove the coordinates before writing the input driver.
6. **GPS UART (phase 6).** Check that GPHCON sets GPH4/5 to function 2. Set GPJ8 low, GPJ6 high, then pulse GPJ7 low for 50 ms and release it high. Read `/dev/ttySAC2` at 4800, 9600, 57600 and 115200 (stty if available, or a raw read through a tiny freestanding helper) and look for `$GP` NMEA or the SiRF `A0 A2` sync. Exit: raw GPS sentences in userspace.
7. **Bluetooth HCI probe (phase 7).** Set GPH2/3/10/11 to function 2 and GPB1 low. Pulse GPJ9 as the stock code does (low and high, 40 ms each, twice). Try BCSP sync at 115200 (`hciattach ttySAC1 bcsp 115200` once an ARMv4T BlueZ build exists), or send a raw H4 `01 03 0C 00` at 115200 and 921600 and look for `04 0E 04 01 03 0C 00`. Repeat with GPB1 high. A reply identifies the transport and baud. PS keys (ANA_FREQ 16 MHz, baud 921600) come after that.
8. **Is dead-reckoning hardware fitted? (phase 6, low expectation).** Read ADC mux 1 and 3 with GPJ0 high, and look for a stable mid-scale value or floating noise. Optionally bit-bang one SPI ID read from the KXP74 (CS GPL12) and the AK8970N (CS GPL11). A PCB photo of the chip sites is cheaper still. If nothing is fitted, phase 6 shrinks to ADC, light sensor and GPS.
9. **Codec ping (beyond the listed phases).** Set GPJ15 high, then bit-bang one WM8750 reset write (R15 = 0) over GPB2/3/4. The bus is write-only, so a codec reply cannot confirm it. The proof comes with ASoC playback, which needs the IIS device work in §3.9, so this goes last.

Phases 1 and 8 need no new hardware facts from this map. Phase 9 should fold the confirmed pins from experiments 1-7 into the board file and replace devmem pokes with real drivers (`s3cmci`, `pwm_backlight`, the homtom ADC/touch driver, `gpio-keys`, `hci_uart`).

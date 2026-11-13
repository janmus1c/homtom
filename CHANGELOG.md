# Changelog

All notable changes to homtom are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow `MAJOR.MINOR.PATCH[-prerelease]`.

Evidence labels: "owner-observed" means a person at the device saw or heard it; "lab-measured" means a register read, log or test output backs it. Details and dates are in `docs/LESSONS.md`.

## [0.1.0-alpha.1] - 2026-11-07 (prepared, not published)

Alpha means: development-only install through the bootloader's storage mode, partial recovery, NOR flash untouched. Expect to need the power key and a Mac. Read `README.md` (Safety) and `docs/flash-safety.md` first.

### Added

- Linux 6.1 kernel with our own board file for the TomTom GO 630 (Samsung S3C2443), packed with a BusyBox initramfs as a bootloader-compatible `ttsystem` image (`kernel/`, `kernel/scripts/build-ttsystem-6.1.sh`). The builder refuses modules that do not match the zImage config.
- `/etc/homtom-release` in the initramfs with version, build date, git state and kernel version; top-level `VERSION` file.
- Trimmed kernel configuration (zImage 4.43 MB to about 3.10 MB). Boots with Bluetooth and SD once modules are rebuilt from the same kernel tree. NO_HZ and high-res timers are held back.
- Dev link: USB Ethernet gadget (ECM), root shell on telnet and a raw port, FTP, all unauthenticated and meant for a direct cable only. The raw shell drops a first line that looks like an HTTP request (lab-measured: a POST from curl runs nothing).
- Storage-mode install loop from the Mac: `homtom-to-storage`, `homtom-stage` (keeps `ttsystem.prev`), `homtom-to-linux`, `homtom-mark-good`, `homtom-recover`. The cycle LINUX, STORAGE, LINUX was run without human action.
- Both storage devices mount: INTERNAL (moviNAND, read-write) and the SD slot (through `homtom-sdi.ko`).
- Display: 480x272 frame push, text console, quiet boot (colour-ladder debug marks compiled out), homtom splash shown after the stock hand logo.
- Backlight PWM (`homtom-bl`): owner-observed bright, dim, dimmer, bright.
- Touch: 5-point calibration fits within 4 px; target test passed 9/9 with max error 8 px (tolerance 12); slide test passed 3/3; DOWN debounce (three consistent frames).
- Configurable on-device GUI (`homtom-gui-cfg`, `INTERNAL:/homtom/ui.conf`, `docs/UI_CONFIG.md`), started at boot with single-instance locks. `tools/homtom-screen` shows the real LCD on the Mac.
- `homtom-statusd` status daemon in C: JSON over HTTP in 8 to 40 ms (the shell version took about 2.2 s), with battery estimate, GPS, light, CPU, memory, Bluetooth and last-touch fields.
- Bluetooth Classic: BlueCore4 chip brought up over BCSP, hci0 UP RUNNING ISCAN PSCAN (lab-measured), visible on an iPhone and a MacBook (owner-observed) after the crystal-frequency fix. Legacy-PIN pairing, SDP and an RFCOMM serial channel verified from a Mac: `ping`, `help` and `status` returned the live JSON over Bluetooth. The bond survived a cold reboot. The PIN is stored on the device and is never part of the repository.
- Home Assistant link: Mac-side bridge (`homtom-ha-bridge.py`), webhook automation and helpers verified end to end over USB (`docs/HA_LINK.md`, `docs/HA_DASHBOARD.md`). You supply the webhook id and `HA_URL` yourself.
- Speaker and microphone paths: the Mac microphone measured a 1 kHz tone from the unit, and the unit's capture path showed a 1 kHz spike with the Mac speaker playing (input select 1, right channel).
- GPS: NMEA sentences arrive at 4800 baud. `gpsaid` aiding tool exists.
- Dev tools for the Mac: `homtom-state`, `homtom-sh`, `homtom-put`, `homtom-wait`, `homtom-tap` and `homtom-e2e` (touch injection and screenshot comparison, device side not yet run on hardware).

### Safety and resilience

- INFORM0=5 is written early (board file and init) so a reset takes the bootloader's "Reboot" path and its storage window. Never write 3.
- The kernel arms the watchdog at boot (WTCON 0x7639, lab-measured) and a heartbeat-gated feeder stops feeding a wedged userspace. A stopped feeder reset the unit within about 15 s into storage mode (verified).
- Listeners and daemons run under respawn loops. A Bluetooth crash-loop breaker skips Bluetooth after two failed attempts.
- `homtom-stage` replaces `ttsystem.prev` only when the running image was marked good, so two bad stagings in a row keep the last good image.

### Known limitations

- Audio: the tone is distorted. The I2S FIFO holds 16 frames and a CPU-fed beep underruns whenever another process runs; real-time priority cut the sidebands by about 20 dB. A DMA or kernel audio path is needed. Microphone gain and noise are not tuned for speech.
- USB link: a fresh link can stop answering while Linux keeps running; replugging the cable restores it (cause not found). Received packets reach Linux one packet late (`docs/ROBUSTNESS.md`). Storage mode needs an unlocked, awake Mac, and the first reboot request is sometimes lost (the tools retry once).
- GPS: no fix indoors. Aiding (`gpsaid`) is untested on the device.
- Light sensor: readings stay at 2 to 6 under room light, phone flash and full cover; the sensor is probably not fitted on this unit.
- Battery percent and millivolts are an unverified estimate (assumes a 1:2 divider and a 1.225 V reference). Motion sensors are probably not fitted.
- Recovery is proven for one failure mode only: a unit whose userspace wedges after init gets reset into storage mode by the watchdog. A staged image that hangs before the kernel writes INFORM0 left the unit powered down and needed the power key (recovery test 2026-11-07). The kernel fix (INFORM0=5 in the board file) is applied but no re-test is recorded. `tools/homtom-recover` restoring `ttsystem.prev` has no recorded successful run. Images that never start a kernel are the bootloader's business.
- NOR flash is untouched. The bootloader is never replaced. There is no tested recovery from a damaged bootloader (needs JTAG or serial). Do not write NOR.
- Boot logo unchanged: the stock hand logo and the storage-mode screens live in NOR and cannot be replaced by files on INTERNAL.
- Install path is a copy of `ttsystem` onto INTERNAL in storage mode, nothing else. `ttsystem.lab2613` must stay on INTERNAL: several tools use it to recognise the right card.
- Debug hooks are not cleaned up (`docs/ROADMAP.md` phase 9). Touch drawn lines sit slightly below the finger (not analysed). The Bluetooth link is not yet used by Home Assistant. No licence is chosen yet for own code.

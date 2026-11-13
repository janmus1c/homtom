# Status (2026-11-07)

Estimates, not measurements.

| Phase (docs/ROADMAP.md) | State | Est. |
|---|---|---|
| 0 Dev loop | Boot, USB net, root shell, reboot (userspace INFORM0 trick, retry once), storage-mode round trips, state tools, live staging | 95% |
| 1 Resilience | INFORM0=5, heartbeat-gated watchdog feeder (killed feeder verified), kernel arms WDT at boot (WTCON 0x7639, no early-hang test yet), respawning listeners, BT crash-loop breaker | 75% |
| 2 Storage | INTERNAL (mmcblk0) and SD (mmcblk1, via homtom-sdi.ko) both mount | 90% |
| 3 Hardware map | docs/HARDWARE_MAP.md from stock source, several items confirmed live | 80% |
| 4 Display/touch | LCD frame push, text console; touch calibrated and verified (5-point fit within 4 px, `targets` 9/9, slide 3/3); backlight PWM verified (user-reported steps); quiet boot verified (splash straight to UI) | 75% |
| 5 Power | charging, external power, battery raw and an unverified mV/percent estimate; power key verified on the unit (short press locks, hold opens the System hub, release at 5-10 s powers off, 10 s reboots); a powered-off unit does not wake on USB power (stock behaviour, user-reported) | 60% |
| 6 Sensors/GPS | GPS NMEA works (no fix indoors); light sensor via LX_EN; motion sensors probably not fitted | 60% |
| 7 Bluetooth | chip up, visible on iPhone and MacBook, kernel hci0 UP, legacy-PIN pairing and RFCOMM serial channel verified from the Mac (status JSON over Bluetooth); not yet used as the HA link | 85% |
| 8 HA link | USB status endpoint, Mac bridge, HA webhook automation and helpers (battery raw/percent est, light, charging, external power, last seen) verified end to end | 65% |
| 9 Cleanup | not started (debug hooks, in-kernel restart path) | 10% |
| Boot graphics | NOR R1 black logo applied and verified on the unit (1669 word programs, verified by a separate dump; cold boot shows black instead of the hand logo; bootloader and storage mode unaffected). Storage-mode screens cannot be changed from a ttsystem record in the reboot-with-Mac flow (the bootloader's host check precedes the ttsystem load) | - |

Open items
1. Audio: the speaker (1 kHz tone seen on a Mac microphone, about -81 dB to -43 dB) and the microphone (1 kHz spike on codec input select 1, right channel) are verified with a Mac; a by-ear audibility check and a speech-grade capture gain are still open.
2. Drag test: drawn lines sit slightly below the finger path (user-reported); not yet analysed.
3. Battery percent and millivolts: the divider and reference assumptions need a measurement.
4. GPS: no fix indoors yet; try with a sky view.

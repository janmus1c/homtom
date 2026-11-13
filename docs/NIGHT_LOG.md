# Night log

## 2026-11-06 night 1
- 23:33 watchdog recovery verified (feeder killed -> reset -> storage mode). init.sh now sets INFORM0=5 + runs a feeder.
- 23:39 SD slot works via loadable module (kernel/modules/homtom-sdi, built with kernel/scripts/build-module.sh): mmcblk1 7.7 GB, mounts. Module is loaded at boot from the initramfs.
- 23:40 HARDWARE_MAP.md written by an Opus advisor: BT = CSR BlueCore4 on ttySAC1 (GPJ9 reset), GPS = SiRF III on ttySAC2 (GPJ6 on), motion sensors probably not fitted, audio codec via bit-banged SPI, touch via GPG12-15 + ADC 5/9.
- 23:45 PROBLEM: since ~23:34 a reboot (INFORM0=5 + SWRST) no longer lands in storage mode; Linux boots directly. Asked the reboot advisor why. Until solved, new images are staged from Linux (cp to /mnt/internal/ttsystem, ttsystem.prev kept) and tested by reboot; recovery from a failed image needs storage mode (WDT reset path is known to give it).
- 00:00 GPS works (SiRF III NMEA 4800 on ttySAC2, GPH4/5 func2, GPJ6 power high; no fix indoors). ADC raw samples work (userland/homtom-hw). HTTP status JSON on :8080 (userland/homtom-status) is in the image and verified after reboot. LCD frame push works via /dev/mem (tools/devtools/devcp, tools/homtom-draw).
- 00:10 Bluetooth: UART1 (GPH2/3 func2) + GPJ9 reset pulses, sync/HCI-reset frames at 9 baud rates: chip stays silent. Open (power, mode pin or clock?).
- 00:30 Opus review done. Findings acted on: stage-live now targets /dev/mmcblk0 (INTERNAL, guarded by ttsystem.lab2613) because /mnt/internal at boot is currently the SD card (mount loop tried mmcblk1p1 first). Verified INTERNAL holds ttsystem (status image) + ttsystem.prev (sdi image) + lab2613.
- Policy for the rest of the night: NO new ttsystem staging (risk: nothing recovers an early-dying image while the Mac is locked). Live work only.

### Pending image changes (stage with the owner present)
- init.sh: watchdog feeder before insmod (done in repo), mount INTERNAL (mmcblk0) first (done), nf grep self-echo fix (done).
- userland/homtom-hw GPH pin value fix (done in repo; the running device has the live-patched files only via FTP).
- devmem guard for GPJ/GPL writes: written and tested live as /tmp/devmem-new (tools/devtools/devmem.c); goes into the image.
- Add to the image: tools/devtools/{hcictl,hciattach,homtom-ts}, userland/homtom-bt-up, modules (bluetooth, hci_uart, serdev, ecc, ecdh_generic, libaes, uinput from artifacts/modules-6.1).
- TODO: panic handling (panic=N) once the in-kernel restart path is understood.
- 00:50 BT: stack modules + hciattach + hcictl built and loaded live; hci0 registers, BCSP link establishment does not complete (see LESSONS). Touch sampler (tools/devtools/homtom-ts) built and idle-tested (pen-up), needs a finger test. uinput.ko loaded.

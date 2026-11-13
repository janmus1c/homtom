# Architecture

One page. Evidence for each claim is in `docs/LESSONS.md`, `docs/HA_LINK.md` and `docs/STORAGE_GFX.md`.

## Boot chain

```
power on
  |
  v
TTBL bootloader (512 KiB NOR, stock, never replaced)
  |  draws the hand logo (NOR image), then SPLASHW.BMP from INTERNAL
  |  reads INFORM0: 5 = normal reboot path, storage mode appears for the host
  |  loads INTERNAL:/ttsystem (records, signed; ttsystem.prev is the fallback copy)
  v
ttsystem  =  Linux 6.1 zImage (kernel/homtom board file) + BusyBox initramfs
  |  cmdline arms the watchdog at boot (s3c2410_wdt.tmr_atboot=1)
  v
/init.sh  (PID 1, never exits)
  |  INFORM0 := 5, watchdog feeder, usb0 = 192.168.1.10, DHCP for the Mac
  |  mounts INTERNAL at /mnt/internal, writes boot logs there
  v
services (below)
```

## Services on the device

```
init.sh main loop --writes /tmp/hb every 30 s--> watchdog feeder
                                                  (stops feeding if /tmp/hb is stale ~4 min, so the WDT resets)
init.sh --sv respawn (restart after 5 s)--> telnetd :23, homtom-shell :2323, ftpd :21,
                                            homtom-statusd :8080 (C; JSON in 8-40 ms; single ADC owner)
init.sh --12 s after boot--> homtom-bt-start
                               |  needs INTERNAL:/homtom/bt.enable and ttsystem.lab2613 on /mnt/internal
                               |  BCSP handshake, crystal frequency (ANA_FREQ), hciattach, hci0 up
                               |  bt.trying = crash-loop breaker
                               v
                             homtom-btd (respawn): PIN pairing, SDP, RFCOMM channel 1
                               pairing window opens with /tmp/bt.pair, btd closes it after 300 s
```

Other binaries in `/bin` are run by hand: `homtom-touch`, `homtom-gui`, `homtom-bl` (backlight), `homtom-snd2` (tone and microphone), `hcictl`, `hcictl-info`, `devmem`.

## USB link, bridge and Home Assistant

```
GO 630 usb0 192.168.1.10  <--USB-ECM-->  Mac
   statusd JSON :8080   ------------>  tools/homtom-ha-bridge.py (polls, posts only complete readings)
   shell :2323, ftp :21 <------------  tools/homtom-sh, homtom-put, staging tools
                                         |
                                         | HTTPS POST (webhook id is a secret in .runtime, never committed)
                                         v
                                       Home Assistant: automation -> helpers (battery raw and estimate,
                                       light, charging, external power, last seen) -> online sensor,
                                       dashboard, offline alert
```

The device does not talk to Home Assistant directly. The Mac is the gateway; Bluetooth is not yet a data path. The bridge also sets the device clock after each reboot.

## Safety nets

| Net | What it covers | Limit |
|---|---|---|
| NOR bootloader untouched | Device always reaches the bootloader and its storage window | Does not help if NOR is damaged; JTAG or a programmer is then needed |
| `ttsystem.prev` and `tools/homtom-recover` | Restores the last good image after a bad stage | Needs the Mac awake and unlocked |
| INFORM0 = 5 | A reset lands in the reboot and storage path | Never write 3 |
| Watchdog armed in kernel cmdline | Resets a hung kernel or userspace | No test of a hang before userspace yet |
| Heartbeat-gated feeder | A wedged init stops feeding and the unit resets | Covers userspace only |
| `sv` respawn | Listeners and statusd restart after crashes | Not for PID 1 |
| Bluetooth crash-loop breaker | A bad bring-up is skipped on the next boot | Delete `bt.trying` to retry |
| Matching-modules check in `build-ttsystem-6.1.sh` | Refuses images whose modules come from another kernel tree | |
| Shell CSRF guard | `homtom-shell` ignores a first line that looks like an HTTP request | Link is still unauthenticated |
| One ADC owner | Avoids corrupted battery and touch readings | Stop statusd before ADC experiments |

# Review 3: changes b072fe2..HEAD (BT daemon, statusd, touch, sound, backlight, init, recovery)

Read-only code review, 2026-11-07. Nothing was run on the device. [V] = verified by reading the cited code. [H] = hypothesis that needs a test.
`init.sh` lines are cited as lines of `kernel/scripts/build-ttsystem-6.1.sh`, which contains the script as a heredoc.

## Ranked findings and minimal fixes

1. **High [H] A web page on the Mac can reach the raw root shell.** `build-ttsystem-6.1.sh:117` runs `tcpsvd 0.0.0.0 2323 /bin/sh` with no authentication. Port 2323 is not on the browsers' blocked-port list (21 and 23 are), so a page the user visits can send `POST http://192.168.1.10:2323/` and every line of the request body runs as root. That includes `devmem`, which can reach the NOR flash and the bootloader. FTP at `:118` runs `ftpd -w /` anonymously, so any local process can overwrite `/mnt/internal/ttsystem`, `bt.pin` and `btkeys`. The tools also target the address `192.168.1.10` without binding to the USB interface (`tools/homtom-sh:4`, `homtom-touch-run:19`), and a home LAN on 192.168.1.0/24 would collide with it.
   Fix: put a small wrapper in front of the shell on 2323 that reads the first line and exits on `GET*|POST*|PUT*|HEAD*|OPTIONS*`, or that requires a token baked into the image, which `homtom-sh` would send first. Run `ftpd` without `-w` except while staging. Bind the listeners to 192.168.1.10.
2. **Medium-High [V] A module built for another kernel can wedge every boot before the network comes up.** The trimmed defconfig has no `CONFIG_MODVERSIONS`, so a module built against a different `.config` loads silently. `build-ttsystem-6.1.sh:16` falls back without warning to an old `kernel/modules/homtom-sdi/homtom-sdi.ko`, and nothing checks that `MODDIR` matches `ZIMAGE`. `build-modules-tree.sh:17,19` hides make failures (`| grep ... || true`), and `:22` then copies stale `.ko` files that are still in the tree. `init.sh` loads homtom-sdi at `:91`, before the network starts at `:105`, so a bad module leaves the unit without telnet. The only way back is the storage-mode window, after the watchdog has reset the unit (up to about 4 minutes).
   Fix: remove the fallback. Delete `*.ko` in the tree before building and check `${PIPESTATUS[0]}`. Write the `.config` sha256 into `MODDIR`, and make build-ttsystem refuse to build when it differs from the `.config` next to the zImage.
3. **Medium [V] The Bluetooth restart path skips the ANA_FREQ fix.** The health loop at `userland/homtom-bt-start:35-40` resets the chip with `homtom-bt-up` (BT_RESET), which per `homtom-bt-handshake:2` clears the PSRAM settings. It does not repeat the ANA_FREQ write and the second handshake from `:17-24`. HCI can still answer the health check while the radio runs at the wrong reference frequency.
   Fix: move `:12-25` into a function and call it from the loop.
4. **Medium [V] homtom-btd collides with statusd as ADC owner.** `homtom-btd.c:30,478` forks `/bin/homtom-status`, which reads the ADC through `homtom-hw battery` and `homtom-hw adc 2` and writes GPJCON and GPJDAT through devmem (`userland/homtom-status:6,15-16`). statusd claims to be the only ADC user (`homtom-statusd.c:4`), and its `adc_read` (`:183-203`) does not check ADCMUX for interference, so both sides can return mixed readings. Each `status` also blocks the btd event loop, including PIN and link-key replies, for about 2 s, with a 5 s timeout. A paired client can queue many `status` commands in a single packet (`:575-583`).
   Fix: have statusd write `/tmp/homtom-status.json` atomically and have btd read that file, or connect to 127.0.0.1:8080. Allow at most one status per second per client.
5. **Medium [V] The pairing window never expires and the PIN is weakly protected.** `/tmp/bt.pair` stays open until the first link key is stored (`homtom-btd.c:205,249`), with no time limit. Pairing is legacy PIN pairing (SSP is not handled). The PIN accepts any length from 1 to 16 (`:210-213`) and sits on FAT, where storage mode and the anonymous FTP from item 1 can read it. A short PIN lets anyone who sniffs the pairing compute the link key offline.
   Fix: stat64 `PAIRF` and refuse when its mtime is more than 120 s old. Refuse PINs shorter than 8 characters (`:213`).
6. **Medium-Low [V] `/tmp/btd.log` grows without limit in RAM.** `homtom-bt-start:27` appends forever on a 64 MB board. A missing `rfcomm.ko` logs a line every 5 s, and the daemon logs every SDP PDU (`:730`).
   Fix: before each restart, move the log to `btd.log.1` once it passes 64 KB.
7. **Medium-Low [V] homtom-recover has gaps.** `tools/homtom-recover:13-14` continues to `homtom-to-linux` even when `cp` or `cmp` fails. It assumes `ttsystem.prev` is good, but `homtom-stage:5` overwrites `.prev` on every stage, so staging twice after a bad image destroys the good copy. LINUX detection needs port 2323 (`homtom-state:23`). An image that boots but has a broken 2323 never goes back to storage mode, so the script waits 900 s and fails. Storage-mode entry also needs the Mac to be awake and unlocked (`LESSONS.md:61`).
   Fix: change to `cp ... && sync && cmp ... || exit 1`. Keep `ttsystem.good` by its SHA-256 from the last verified boot. Make `homtom-stage` refuse to overwrite `.prev` when the current ttsystem does not match that hash. Try `homtom-to-storage` first when ping or telnet still answers.
8. **Low [V] The no-statusd escape switch cannot be set persistently.** `/etc/homtom/no-statusd` (`build-ttsystem-6.1.sh:119`) lives in the initramfs, so it cannot be set without rebuilding. INTERNAL is mounted only later (`:143-156`).
   Fix: start the status service after the mount, and check `/mnt/internal/homtom/no-statusd`.
9. **Low [V] homtom-snd2 can spin forever with the amplifier on.** `homtom-snd2.c:516-519` loops on `while (TXFULL());` with no bound, with AMP_ON high and the codec powered. There is no signal cleanup, so `killall` leaves the amplifier on.
   Fix: add a guard counter like `probe_one` (`:300`) and then call `power_down()`.
10. **Low [V] A stalled RFCOMM peer can block homtom-btd.** `put()` (`homtom-btd.c:528-538`) and the busy reply (`:710`) block in sendto. A peer that stops reading freezes the daemon, and pairing stops being answered.
    Fix: send with `MSG_DONTWAIT` (0x40) and drop the client on EAGAIN.

Minor issues:
- [H] Read-modify-write races on GPJDAT. statusd writes GPJ1 every 5 s (`homtom-statusd.c:178`), `homtom-bt-up` writes BT_RESET (bit 9), and snd2 writes bit 15. A lost update can undo another process's change, such as BT_RESET released. Bits 10 and 14 are kept, because nobody changes them.
- [V] `homtom-ha-bridge.py:15` ignores the result of `homtom-sh`, so a failed clock sync is not retried until the next reboot.
- [V] `homtom-touch-run:15` passes `$*` unquoted into the device shell. This is safe only while the operator supplies the arguments.

## Checked and found correct [V]

- **SDP parser** (`homtom-btd.c:305-475`): `de()` checks every length against the end of the buffer, and the attribute list is capped at 16 ranges. The response sizes fit their buffers: at most 610 bytes into 700, and the record is 86 bytes into `t[256]`. The continuation offset is checked against the total.
- **HCI handling in btd:** the event length is checked against the 260-byte buffer (`:261`). The 0x18 handler requires a payload of at least 23 bytes. The PIN is at most 16 bytes, into a 20-byte buffer.
- **RFCOMM commands:** only exact string matches. There is no shell, no exec of input, and no path built from input. The line buffer is capped at 127 bytes.
- **statusd HTTP:** at most 512 bytes, one client at a time, a 2 s deadline, read-only. NMEA lines over 100 bytes are rejected, the checksum is verified, and `"` and `\` are stripped before the line goes into the JSON. `due()` survives the 49-day millisecond wrap.
- **Touch calibration:** written only after the guard check, via tmp file and rename (`homtom-touch.c:1325-1332`). Options loaded from the cal file can only reach ADCTSC (masked to 0x1ff), ADC mux values up to 15, GPG12-15 and the upper byte of GPGUDP. The output buffers stay in bounds: 653 of 700 for kmsg, 639 of 640 for `ob`.
- **Watchdog feeder** (`init.sh:90`): starts before `insmod`, stops after about 240 s without a heartbeat, and the kernel keeps the watchdog running if the feeder dies unexpectedly. `tmr_atboot=1` covers a panic after the driver has probed.
- **Hazard registers:**
  - statusd `hw_wr` masks (`:174`): 0x4400 for GPJDAT, 0x30300000 for GPJCON and 0xfffff for GPLCON are correct, and the allowlist has exactly 3 entries.
  - snd2 `gw` (`:155`) rejects all of GPL, 0xF0-0xFC. In the clock block it touches only CLKSRC[15:14], PCLKCON bit 9 and SCLKCON bit 9, not EPLLCON.
  - homtom-bl touches only PCLKCON bit 10, timer 0 and GPB0.
  - homtom-touch writes GPG and ADC, and maps PCLKCON read-only.
  - The framebuffer at 0x33F00000 is reserved with `memblock_reserve` (`kernel/homtom/mach-homtom-go630.c:326`).
  - No reviewed file writes NOR, the bootloader or EPLL.

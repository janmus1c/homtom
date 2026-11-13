# Safety

This project writes to the boot storage of a navigation unit. It can brick the
unit. Read this before you stage anything.

TomTom is a trademark of its owner; this project is not affiliated with or endorsed by TomTom; it ships no TomTom software, maps or firmware.

## What can go wrong

- The bootloader lives in a 512 KiB NOR flash on the board. If it is damaged,
  the unit does not start. Recovery needs JTAG or a serial console on the
  board, or an external programmer. The S3C2443 has no USB or SD rescue mode.
- A NOR write from Linux once froze the device (see `docs/flash-safety.md`).
  The cause is not confirmed.
- This project has not demonstrated a recovery from a dead bootloader. Where
  `docs/` or the tools describe a recovery path for a bad kernel image, it
  covers only the case where the bootloader still runs. `tools/homtom-recover`
  has not been tested against a real failed boot.

## Rules

1. Never write NOR flash. Do not use `norwrite` or any other NOR writer on a
   unit you cannot afford to lose. The `norid` tool only reads.
2. Never write `INFORM0=3`. The bootloader's first stage may then jump to
   whatever is in `INFORM1`. The working value is 5, set by the init script.
3. Do not run raw disk commands against the unit's storage or NOR. Stage files
   only with the tools in `tools/`.
4. Keep a known-good `ttsystem.prev` on INTERNAL and run `tools/homtom-mark-good`
   after each verified boot. Staging keeps the last good image as the fallback
   until then.
5. Storage mode appears only when the host is awake and unlocked. Keep the host
   awake while staging (on a Mac, `caffeinate -dimsu`).
6. Do not toggle USB soft-connect on the gadget. USB stays dead until reboot.
   Use plain `reboot` on the device, not `reboot -f`.
7. Build kernel modules from the same tree as the `zImage`. Mismatched modules
   fail to load (`docs/LESSONS.md`).
8. Telnet, the raw shell on port 2323 and anonymous FTP are open on the USB
   link. Do not bridge that link onto a network you do not trust.

## Risk

You use this software at your own risk, on hardware you can afford to lose. It
comes with no warranty (see `LICENSE`). Nothing here promises that a unit can be
recovered.

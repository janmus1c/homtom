# Flash safety rules

The GO 630 boots from a 512 KiB NOR flash (`/dev/mtd0`). The S3C2443 has no USB or SD
rescue mode, so a damaged bootloader can only be recovered with JTAG or a serial
console on the board. Treat every NOR write as potentially fatal.

## What went wrong (2026-11-02)

`norflash --write` on blocks 0x20000–0x3FFFF froze the whole device: no userspace,
touchscreen dead, network stack still answering pings. The write had passed a dry
run that compared contents only. Nothing had shown that the kernel's MTD driver can
erase and program this chip at runtime. Block 0x20000 also holds the tail of the
gzip-compressed bootloader (0xB294–0x247DC), so an interrupted erase there can stop
the device from booting.

Probable causes, in order. None is confirmed yet.

1. The NOR is write-protected in hardware (WP#/VPP on a GPIO). The 2.6.13 CFI
   driver then polls a status that never changes, in a non-preemptible loop.
2. The ngffs filesystem is mounted on the same chip and the driver holds it.
3. The chip needs a different command set or unlock sequence from the one the
   kernel map driver uses.

## Rules for any future NOR write

1. **Prove the path on a harmless block first.** Erase and program one block that
   holds nothing the boot depends on, read it back, and confirm the system stays
   responsive throughout. The free blocks are 0x44000–0x4FFFF, inside the ngffs area,
   so check with the ngffs map first. Do not write the image blocks until this works.
2. **Never write a block that shares data with boot code** when a layout exists
   that avoids it. Here the palette and images start at 0x28000, inside the block
   that holds the bootloader tail. Prefer changes confined to 0x30000–0x3FFFF, or
   accept the risk explicitly.
3. **Check write protection before erasing.** Read `/proc/mtd` and MTD flags
   (`MEMGETINFO` flags must include `MTD_WRITEABLE`). Read the CFI query and the
   chip lock status (`MEMGETREGIONINFO`, `MEMISLOCKED` if available) and the
   relevant GPIOs.
4. **Run the writer detached, with a log on persistent storage.** Start it with
   `nohup`/`setsid`, writing progress to `/mnt/sdcard/homtom/logs/`, so a hung
   Telnet session does not hide progress and the log survives a reset.
5. **Put a time limit on each erase and write.** The tool must exit with an error
   instead of hanging. The kernel cannot be interrupted, so if a block erase has not
   finished in a few seconds, stop before the next block.
6. **Unmount ngffs (`/mnt/flash`) before writing** so nothing else touches the chip.
7. **Keep two independent backups of the full chip** on the host before writing.
   `artifacts/backups/mtd0-before-flash-*.bin` and
   `artifacts/bootloader-dump-*/mtd0.bin` hold the original.
8. **Test in the emulator first** once `docs/emulator-plan.md` gives a NOR model
   that runs the real bootloader.

## Recovery if the bootloader is damaged

The original image is in `artifacts/backups/`. Restoring it needs one of:

- JTAG on the S3C2443 (ARM920T) with OpenOCD and a CFI flash driver
- a serial console, if the stage-1 loader still runs and offers a download mode
  (not yet investigated)

Locating JTAG/UART pads on PCB `DA0VF8MB8C0 REV.C` is the first step.

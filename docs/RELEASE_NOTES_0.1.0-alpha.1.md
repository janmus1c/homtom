# homtom 0.1.0-alpha.1 (draft, not published)

First pre-release of homtom: our own Linux 6.1 on a TomTom GO 630, meant as a Home Assistant edge node. This is an alpha for people who are comfortable with a unit that may need the power key and a Mac. It is not affiliated with or endorsed by TomTom and contains no TomTom software.

**Warning.** Installing replaces the `ttsystem` image on the unit's INTERNAL volume. It can leave the unit unusable until you power-cycle it, and nobody has tested it on another unit. NOR flash (the bootloader) is never touched and there is no tested way back from a damaged bootloader. Read `docs/flash-safety.md` and `INSTALL.md` before you start, and copy the original INTERNAL volume first.

## What works

Verified on one unit (user-reported or measured, see `CHANGELOG.md`):

- Boot of Linux 6.1 from `ttsystem` on INTERNAL, USB Ethernet dev link with a root shell, INTERNAL and SD storage.
- Install loop from a Mac through the bootloader's storage mode, with a `ttsystem.prev` fallback.
- LCD, backlight PWM, calibrated touch (5-point fit, 9/9 target test), a configurable on-device GUI.
- Status daemon (JSON in 8 to 40 ms), Bluetooth Classic pairing with a PIN and an RFCOMM serial channel from a Mac, a Home Assistant link through a Mac-side bridge.
- Speaker and microphone paths are alive, GPS delivers NMEA.

## What does not, or is not proven

- Audio is distorted (CPU-fed I2S FIFO underruns). No beeps you would want to listen to.
- The USB link can go silent while Linux keeps running; replugging fixes it. Storage mode needs an unlocked Mac.
- GPS has no indoor fix; aiding is untested. The light sensor is probably not fitted. Battery percent is an unverified estimate.
- Recovery is proven for one failure mode (a wedged userspace is reset into storage mode by the watchdog). A hang before the kernel sets INFORM0 needs the power key. `tools/homtom-recover` has no recorded successful run.
- The stock hand logo and storage-mode screens stay as they are. Going back to stock has no validated procedure here.
- No licence has been chosen yet; see `LICENSE.NOTE.txt`.

## Install

See `INSTALL.md`. Verify the image with `shasum -a 256 -c ttsystem.sha256`, then `tools/homtom-stage ./ttsystem` and `tools/homtom-to-linux` in storage mode. On the unit, `/etc/homtom-release` names the version and the commit it was built from.

## Checksums

Filled in at release time from `ttsystem.sha256` and the tarball's `.sha256`: TODO.

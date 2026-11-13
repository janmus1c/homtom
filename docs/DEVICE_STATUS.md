# Current device checkpoint

Updated 2026-11-02. Prefer `HANDOVER.md` if this file and the handover disagree.

The GO 630 runs Linux 2.6.13 (GCC 3.3.4) on S3C2443. USB gadget is
`s3c-udc-hs` + `g_ether` (CDC ECM). Device `192.168.1.10`, host iface typically
`en9` at `192.168.1.100` (MAC `02:54:54:00:06:31`).

## Link

USB Ethernet and Telnet/FTP work when the gadget is up. The firmware image
embeds **hsudc-v9** (alt **v8**). Runtime trials through **v13d** live only in
RAM until `firmware/build.py` is pointed at a new artifact and installed.

v8 fixed first-boot SET_CONFIGURATION / INDEX races (late EP0 ack). Later
trials (v10–v13d) address IRQ draining and dual-FIFO OUT behaviour: without
draining the second OUT packet, device→host replies wait for the next host
packet (RTT ≈ ping interval). v13d is the current best RAM candidate; long
soak results were incomplete at last handover write.

Some boots still fail to link until `network.sh` reloads the gadget. Watcher
is not the primary cause of drops on an already-up link.

## Access

```sh
python3 scripts/tt.py 'cmd' [timeout]
curl -sS --interface en9 ftp://192.168.1.10/…
tools/homtom-watch
tools/homtom-linklog .runtime/link/linklog.log
```

Owner authorized full device root/Telnet for lab work. Bind to the gadget
interface; do not use VPN routes for `192.168.1.10`.

## Direction

**homtom OS** from scratch (see `docs/HOMTOM_OS.md`): HA edge device, no TomTom
maps/Nano-X. Field link = Bluetooth; kernel = **6.1.y** (6.2 dropped S3C24xx).
OpenTom 2.6.13 is lab shell only.

FAT boot BMPs on INTERNAL are already **homtom** (`splashw` / lowbattery /
antitheft = `build/bootsplash/`). USB **storage-mode UI** (NOR image table) is
still stock TomTom: storage LUNs cannot write NOR; in-kernel `norflash` hangs
and leaves the chip unchanged (`docs/flash-safety.md`). NOR art needs an
external programmer / JTAG. Offline image: `artifacts/homtom-nor/mtd0-homtom.bin`.

Emulator (`emulator/run`, `emulator/stop`) helps userspace UI; it does not model
real USB or RAM limits.

## Safety

Boot-file copies exist under `artifacts/backups/`; not a full flash backup.
NOR bootloader sectors appear hardware-protected; see `docs/flash-safety.md`.
Do not claim recovery or a successful modern kernel boot without artifacts.

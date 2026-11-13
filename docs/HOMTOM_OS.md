# Homtom OS — product direction

User direction **2026-11-02**: build **our own** firmware / userspace / UI on
this GO 630. Not a TomTom revival, not OpenTom, not Nano-X, not maps.

## What we are building

A small **homtom** system: Home Assistant edge node on S3C2443 hardware.

- Our boot path and our userspace (BusyBox or similar, our services, our UI).
- **Bluetooth Classic** as the primary link to HA (and to us). USB gadget is
  only a lab/development pipe on the old kernel until BT is solid.
- Every useful on-board function working: GPS, compass/gyro/accel, ADC,
  battery, audio (WM8750), display, touch, storage.
- Branding: homtom (HA blue / ink / Poppins) everywhere we control.

**Out of scope:** TomTom navigation maps, Nano-X, keeping OpenTom as the
product UI. OpenTom/`ttsystem` stays only as a temporary way to talk to the
hardware while the new stack is born.

**Worst-case radio:** if onboard BT cannot be made reliable, a DIY ESP32 (or
similar) may provide Wi‑Fi/BLE as an add-on. Prefer native BT first.

## Kernel baseline (verified 2026-11-02)

| Version | S3C24xx / S3C2443 in mainline |
| --- | --- |
| **6.1 LTS** | **Last line that still contains the platform** (use this). |
| **6.2** | Platform **removed** — commit `61b7f8920b17` (“ARM: s3c: remove all s3c24xx support”, Arnd Bergmann, 2022-09-29, for the 6.2 window). Vanilla 6.2 does **not** “support” this chip; you would be re-adding deleted code. |
| 6.3+ | Same: no in-tree s3c24xx. |

So the earlier “try 6.2 first” guess is inverted: **start from `v6.1` (latest
6.1.y)**, board file + DT or platform data for GO 630, bring up serial/FB,
MMC, then sensors and BT. Going newer than 6.1 means an explicit out-of-tree
carry of `mach-s3c` / drivers — a later choice, not the first boot target.

Mainline also had `CONFIG_USB_S3C_HSUDC` (`s3c-hsudc`) for this UDC while
`ARCH_S3C24XX` existed; it goes away with that platform. USB gadget on the new
kernel is secondary to BT for the product, but useful for bring-up.

## Learning curve vs shortcuts

Prefer the long path: own init, own UI, own HA bridge, own driver bring-up on
6.1. Using stock 2.6.13 + OpenTom is allowed only as a **debug host** (Telnet,
dumps, sensor probes), not as the destination.

Cutting corners (shipping HA on top of Nano-X, keeping TomTom chrome) is
rejected unless the user reopens that.

## Bring-up ladder

1. **Done (2026-11-02):** Linux **6.1.191** unpacked at `.runtime/linux-6.1`;
   in-repo board skeleton under `kernel/homtom/`; apply + Docker cross-build
   scripts in `kernel/scripts/`. See `kernel/README.md`.
2. Produce `artifacts/kernel-builds/homtom-6.1/zImage` (compile proof).
3. Smallest **device** boot proof: UART or early FB + initramfs (wrap as
   `ttsystem` or alternate loader). ATAG mach type **605** (`MACH_TOMTOMGO`).
4. MMC/FAT or initramfs root; then display + touch.
5. Power/battery, audio, GPS, IMUs — `docs/BOARD_CAPABILITIES.md`.
6. Bluetooth HCI + Classic for HA; USB gadget optional.
7. Homtom UI + HA integration.

## Repo anchors

- Kernel track: `kernel/README.md`, `kernel/homtom/`
- Capabilities / live `/dev`: `docs/BOARD_CAPABILITIES.md`
- Brand: `brand/`
- NOR art (blocked on-chip): `docs/flash-safety.md`, `docs/NOR_PLAN.md`

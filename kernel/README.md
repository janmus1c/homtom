# Homtom Linux 6.1

Own OS kernel track for the GO 630. Product direction: `docs/HOMTOM_OS.md`.

## Tree

| Path | Role |
| --- | --- |
| `.runtime/linux-6.1` | Unpacked **Linux 6.1.191** (gitignored). Source tarball beside it. |
| `kernel/homtom/` | Board file, Kconfig, defconfig — **in git** |
| `kernel/scripts/apply-homtom.sh` | Copies board support into the tree |
| `kernel/scripts/docker-build.sh` | Debian bookworm + `gcc-arm-linux-gnueabi` → `zImage` |

Do not use the TomTom gcc 3.3.4 image for 6.1; that toolchain is for the
legacy 2.6.13 modules only.

## ATAG machine type

Stock bootloader expects **mach type 605** (`MACH_TOMTOMGO` in OpenTom).
Barcelona “Detected type 25” is a different board id. The board file uses
`MACHINE_START(TOMTOMGO, "homtom GO 630")` so ATAGs still match.

## Build

```sh
# once: tree already at .runtime/linux-6.1 from linux-6.1.191.tar.xz
./kernel/scripts/docker-build.sh
# → artifacts/kernel-builds/homtom-6.1/{zImage,System.map,.config,SHA256SUMS}
```

Compile proof (2026-11-02): `artifacts/kernel-builds/homtom-6.1/zImage`
(4.3 MiB, SHA256 `1d2fbcc2…`) with `CONFIG_ARCH_S3C24XX`, `CPU_S3C2443`,
`MACH_TOMTOMGO`, `USB_S3C_HSUDC`, and `homtom_*` symbols in `System.map`.
Defconfig is **`s3c2410_defconfig` + homtom options** (a sparse fragment once
produced a useless multi-V7 image — do not revive that).

Boot on hardware is the next step (wrap as `ttsystem` / serial / FB). No boot
claim without a device log artifact.

## What’s in 6.1.191 for this SoC

Confirmed present: `CPU_S3C2443`, `mach-smdk2443`, `s3c-hsudc`, `s3c2410fb`,
`s3cmci` / `sdhci-s3c`, Samsung UART. Removed in **6.2** with the rest of
s3c24xx — stay on 6.1.y.

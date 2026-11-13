# Prior art

Status: desk research done 2026-11-07 with web search, page fetches, the GitHub API, the Internet Archive search and CDX APIs, and the pristine `linux-6.1.191.tar.xz` in `.runtime/`. Nothing here was tested on hardware.

Labels:

- **[V]** verified: the claim was read in a source fetched during this research (page, API response, tarball or PDF text).
- **[H]** hypothesis: from a search-result snippet only, an inference, or a source that could not be fetched.

People are not named. Sources are cited by URL only.

## Summary

1. **There is a public Linux 6.1 port for another TomTom.** A GitHub repository published on 2026-10-14 runs Linux 6.1.182 on a TomTom ONE XL (Samsung S3C2412) from a `ttsystem` loaded by the stock bootloader, with a `mach-tomtomgo.c` board file in `arch/arm/mach-s3c`, framebuffer, touch, audio, BlueCore Bluetooth over BCSP and USB networking. Its approach is close to ours. This project cannot claim to be the first recent kernel on a TomTom. [V]
2. **OpenTom** never moved past TomTom's vendor 2.6.13 kernel. Its builds download TomTom's `golinux-tt1114405` tree (2.6.13, `MACH_TOMTOMGO`, `BARCELONA_*` drivers, `CPU_S3C2443` selectable) and a gcc 3.3.4 toolchain. The original site `opentom.org` now serves an unrelated website. [V]
3. **TomTom's GPL kernels** archived at `tomtom.com/gpl` are 2.4.18-based (`ttlinux`), 2.6.13-based (`golinux-tt*`), and one 2.6.28 for ARM11 units. The live `tomtom.com/gpl` URL returns 404. [V]
4. **Mainline**: S3C24xx (including S3C2443, `mach-smdk2443.c` and `s3c-hsudc`) was marked deprecated in 6.0 and removed in 6.3. Linux 6.1 is the last LTS that still has it, with a projected EOL of December 2027. [V]
5. Searches found no public report of a kernel newer than 2.6.13 on an **S3C2443-based** TomTom such as the GO 630, and no report of the `s3c-hsudc` gadget driver in use on a TomTom. [V for "no hits in these searches"; absence is not proof]

## Findings

| # | Source (URL) | Date | What it shows | Confidence |
|---|---|---|---|---|
| 1 | (public project, not named here) | Autumn 2026 | Linux 6.1.x on a TomTom ONE XL (S3C2412, ARM926), installed by replacing `ttsystem`, with its own board file; hardware-tested by its author. Same family of approach as ours on a different SoC. | [V] |
| 2 | https://github.com/george-hopkins/opentom | Created 2015-07-15; last commit 2017-01-25; last push 2021-02-01 | Unofficial OpenTom build system. `Makefile` unpacks `golinux-tt1114405.tar.gz` as `linux-s3c24xx`, applies `patchs/kernel_tt1114405_opentom.patch` and wraps zImage + initramfs into `ttsystem` with `mkttimage`. Toolchain `gcc-3.3.4_glibc-2.3.2`, downloaded from an archived `tomtom.com/gpl` URL. Kernel config header: `Linux kernel version: 2.6.13`, `CONFIG_MACH_TOMTOMGO=y`, `CONFIG_CPU_S3C2443=y`, `CONFIG_BARCELONA_*` drivers (ADC, TS, GPS, BAT, USBMODE, sound ALC5628). | [V] repo contents via GitHub API |
| 3 | https://github.com/micky-it/Working-2021-OpenTom | Created 2021-03-22; pushed 2026-05-23 | OpenTom fork "tested to be working on April 2021". Same 2.6.13 base. | [V] README via API |
| 4 | https://github.com/LeddaZ/opentom and https://github.com/LeddaZ/opentom-kernel | Both 2024-05-17/18 | Fork of the 2021 OpenTom and a separate kernel repository. This project's `docs/HARDWARE_MAP.md` already uses `opentom-kernel` as the stock 2.6.13 reference (`mach-tomtomgo.c`, `s3c2443*.c`, `drivers/barcelona/`, `s3c-udc-hs.c`). | [V] repo metadata via API |
| 5 | https://github.com/navit-gps/NavitTom | Created 2017; pushed 2021-02-01 | "Tiny System for Tomtom devices to run navit", OpenTom-based. | [V] repo metadata |
| 6 | https://github.com/bm16ton/tomtom2023, https://github.com/valiantlynx/tom (now redirects to `mythrantic/tom`), https://github.com/drugo72/golinux | 2023-07-30; 2022-03-20; 2013-08-24 | Further OpenTom copies and a mirror of TomTom's kernel. None mentions a newer kernel. | [V] metadata via API; contents not reviewed beyond README |
| 7 | https://www.opentom.org/ | Fetched 2026-11-07 | The domain now serves an unrelated Japanese WordPress site. The OpenTom wiki is gone from this address. | [V] fetched |
| 8 | http://opentom.free.fr/index.php?title=Main_Page | Page last modified 2015-02-01 | OpenTom wiki mirror. Main page is empty, and the MediaWiki install emits PHP warnings. | [V] fetched |
| 9 | https://www.mikrocontroller.net/topic/460766 | 2018-10-09 to 2022-02-03 | German forum thread: OpenTom considered dead, original site down, sources partly lost. Build started from 2.6.13. Models discussed: ONE v1-v3, ONE XL, GO 920. No working image came out of the thread. | [V] fetched |
| 10 | https://events.ccc.de/congress/2005/fahrplan/attachments/696-slides_hacking_into_tomtom_go.pdf | 22C3, Dec 2005 | OpenTom talk slides (German). First GO: Samsung S3C2410 (ARM920T); stock kernel string `2.4.18-rmk6-swl5-tt201`; `ttsystem` = loader + gzip kernel + parameter block; image signatures used Blowfish, and the key was recovered; JTAG and serial console mentioned for early units. | [V] PDF text extracted |
| 11 | https://hackaday.com/2006/12/29/opentom-roll-your-own-tomtom-distro | 2006-12-29 | OpenTom coverage; comments note the site is gone and point to archives. | [V] fetched |
| 12 | https://laforge.gnumonks.org/blog/index-7.html ("TomTom and your own kernel") | 2004-10-24 | Forward-port of TomTom's 2.4.18-rmk6 patches to 2.4.27; a 2.6 port called possible but not a priority. | [V] fetched |
| 13 | https://gpl-violations.org/news/20041024-linux-tomtom/ | 2004-10-24 | GPL settlement: TomTom to publish kernel source with its modifications at `tomtom.com/gpl`. | [H] search snippet only; not fetched |
| 14 | https://www.tomtom.com/gpl (redirects to `/en_gb/gpl`) | Fetched 2026-11-07 | HTTP 404. Current manuals still point to `tomtom.com/gpl` (for example https://download.tomtom.com/open/manuals/new_GO/html/en-gb/Copyrightnotices(allregions)-Nav4.htm, search snippet). | [V] 404 observed; manual reference [H] |
| 15 | Wayback CDX for `tomtom.com/gpl/*` (https://web.archive.org/cdx/search/cdx?url=tomtom.com/gpl/*) | Captures 2007-2024 | 181 archived URLs, 60 of them `golinux-tt*` tarballs/diffs (for example `golinux-tt1008` to `golinux-tt244890`, `golinux-tt1114405` captured 2024-12-24); `ttlinux194` (2.4 era); `arm11/linux-kernel-2.6.28-tt855479.tar.gz` (captured 2023-11-18). We did not check which tarball matches GO 630 firmware. | [V] CDX output |
| 16 | https://archive.org/download/golinux-tt1114405.tar.gz/golinux-tt1114405.tar.gz | Uploaded 2017-01-10 | Archive.org item titled "Linux Kernel for TomTom GO 9.x"; this is the tree OpenTom downloads. | [V] archive.org search API |
| 17 | https://navit.readthedocs.io/en/latest/_sources/user/platforms/maintained/TomTom.rst.txt | Current docs | Navit on GO 910/730 and ONE XL through the OpenTom toolchain, which it calls outdated; "newer Linux kernels can't be used yet because of display support". | [V] fetched |
| 18 | https://wiki.openstreetmap.org/wiki/TomTom | Current wiki | Lists TomTom models and add-on software; no SoC or kernel data, no GO 630. | [V] fetched |
| 19 | https://lists.infradead.org/pipermail/linux-arm-kernel/2022-October/783158.html | Oct 2022 | `[PATCH 01/21] ARM: s3c: remove all s3c24xx support`: removes s3c2410/2412/2416/2440/2442/2443/244x, `mach-smdk2443.c`, `s3c2443.c`, the s3c2443 clock binding header, the s3c2410 defconfig and the board files (bast, gta02, h1940, mini2440, rx1950, osiris, ...). 126 files, about 21,000 lines deleted. Cites deprecation commit `6a5e69c7ddea` ("ARM: s3c: mark as deprecated and schedule removal"). | [V] fetched |
| 20 | https://lists.infradead.org/pipermail/linux-arm-kernel/2022-October/783498.html | Oct 2022 | `[PATCH 13/21] usb: gadget: remove s3c24xx drivers`: removes the s3c2410 `udc` and the `hsudc` drivers because the platform is gone. Mirrors date the mainline commit 2023-01-16 (search snippet). | [V] patch; commit date [H] |
| 21 | https://patchew.org/linux/99179367-0d59-4938-b44c-ca9408ad784e@app.fastmail.com | Jan-Feb 2023 | "ARM and RISC-V SoC changes for 6.3": boardfile removal "a follow-up to the deprecation ... merged in linux-6.0"; includes "ARM: s3c: remove all s3c24xx support". | [V] fetched |
| 22 | https://lists.infradead.org/pipermail/linux-arm-kernel/2022-June/756251.html | June 2022 | "Scheduling (unused) board file removal for linux-6.x": s3c24xx scheduled for removal in early 2023, after the next LTS. | [H] search snippet; thread not fetched |
| 23 | `linux-6.1.191.tar.xz` (kernel.org tarball in `.runtime/`) | 6.1.191 | `ARCH_S3C24XX` is "(deprecated, see help)" and `depends on ATAGS && UNUSED_BOARD_FILES`. Help text: "Without such feedback, the platform will be removed after 2022." Present: `mach-smdk2443.c`, `s3c2443.c`, `drivers/usb/gadget/udc/s3c-hsudc.c`, `drivers/clocksource/samsung_pwm_timer.c`, and S3C24xx boards amlm5900, anubis, at2440evb, bast, gta02, h1940, jive, mini2440, n30, nexcoder, osiris, otom, qt2410, rx1950, rx3715, s3c2416-dt, smdk2410/2413/2416/2440/2443, tct_hammer, vr1000, vstms. No TomTom board. | [V] tarball listing |
| 24 | https://www.kernel.org/category/releases.html | Fetched 2026-11-07 | 6.1 longterm: released 2022-12-11, projected EOL Dec 2027. | [V] fetched |
| 25 | https://lists.infradead.org/pipermail/linux-arm-kernel/2024-July/946956.html | 2024-07-31 | RFC on ARM deprecation timeline: ARMv4T still supported (ep93xx, at91rm9200, clps711x, imx1, nspire), expected to stay until about the end of the decade; proposes dropping ARMv4 (non-T) and OABI toolchain support. | [V] fetched |
| 26 | https://lists.infradead.org/pipermail/linux-arm-kernel/2024-September/964089.html | Sept 2024 | "soc: convert ep93xx to devicetree" pull (ARM920T family), merged for 6.12 per search snippets. A current ARMv4T platform kept alive by moving to DT. | [H] snippet; not fetched |
| 27 | https://cgit.openembedded.org/openembedded/tree/recipes/linux/linux-smdk2443_2.6.20+git.bb?h=testing | 2007 era | OpenEmbedded recipe for an SMDK2443 kernel (2.6.2x). Historical S3C2443 community work outside TomTom. | [H] snippet |
| 28 | https://hackaday.com/2019/09/01/running-doom-on-a-doomed-gps | 2019-09-01 | Doom on a Magellan unit under Windows CE. Not Linux, listed only because it surfaces in searches. | [V] fetched |
| 29 | https://www.pocketgpsworld.com/downloads/910teardown.pdf | Unknown | GO 910 disassembly guide (Torx T6, LCD frame). Not a GO 630, no debug pads. | [H] snippet |
| 30 | GO 630 debug pads (JTAG/UART) | n/a | Searches found no public GO 630 teardown that identifies JTAG or UART pads. | [V] no hits in searches below |

## Searches made

Web search (2026-11-07), queries as typed:

- `OpenTom TomTom Linux kernel opentom.org`
- `TomTom GO S3C2443 mainline Linux kernel`
- `TomTom "s3c2443" kernel source`
- `TomTom GO 630 Linux hack bootloader ttsystem custom kernel`
- `TomTom GO 630 JTAG OR UART OR serial console pads teardown`
- `"TomTom ONE XL" "Linux 6.1" port`
- `TomTom navigator run Debian OR "custom linux" GitHub ttsystem 2020 OR 2021 OR 2022 OR 2023 OR 2024`
- `TomTom GPL source code download kernel 2.6 opensource tomtom.com`
- `TomTom opensource GPL archive golinux kernel 2.6.13 tarball tomtom`
- `TomTom GPL source code page navigation devices "gpl" tomtom.com download golinux tt`
- `gpsunderground tomtom linux kernel bootloader custom ttsystem`
- `hackaday TomTom linux hack old satnav`
- `reddit old TomTom GPS install linux custom kernel repurpose`
- `postmarketOS OR buildroot TomTom navigator device port mainline kernel`
- `old TomTom satnav "Home Assistant" repurpose display sensor`
- `ARM s3c24xx board files removal Arnd Bergmann 2022 "s3c24xx" remove`
- `"Scheduling (unused) board file removal for linux-6.x" s3c24xx`
- `"Linux 6.3" s3c24xx removed obsolete ARM boards pxa removed`
- `s3c-hsudc removed linux kernel usb gadget driver`
- `Linux kernel ARMv4T ARM920T support deprecate remove 2024 OR 2025 Arnd Bergmann armv4`
- `mini2440 OR s3c2440 Linux 6.1 boot 2023 mainline kernel ARM920T`
- `ep93xx device tree conversion merged Linux 6.12 ARM920T`

GitHub repository search (`gh search repos`): `tomtom linux`, `tomtom go`, `ttsystem`, `opentom`, `s3c2443` (no results for the last).

Internet Archive: advanced search for `golinux` and `tomtom AND (gpl OR linux OR kernel)`; Wayback CDX for `tomtom.com/gpl/*`.

Gaps and failures:

- The web search tool is US-only. German, Dutch, French and Italian forums (for example GPSUnderground, MyTomTom, tomtomforums.com) were reached only through incidental hits. No GPSUnderground page appeared in any result.
- Reddit, YouTube and Discord were not searched directly; no Reddit result appeared.
- `git.kernel.org` returned 403 to the fetcher, so commit `6a5e69c7ddea` and the hsudc removal commit were not read directly. A beagleboard GitLab mirror was behind a bot challenge. `kernelsources.org` timed out.
- The Wayback availability API returned HTTP 429, so archived OpenTom wiki pages (hardware variants, GO 630 entries) were not read.
- `www.tomtom.com/gpl` returns 404.

## What appears to be new in this project

Each statement is limited to the searches listed above. Absence of a hit is not evidence that nobody has done it.

- We found no public report of a Linux kernel newer than TomTom's own 2.6.13 booting on an **S3C2443-based** TomTom unit, or on a **GO 630** specifically. The closest prior work (finding 1) is Linux 6.1.182 on an S3C2412 ONE XL, published 2026-10-14.
- We found no public report of the mainline **`s3c-hsudc`** high-speed UDC driver in use on any TomTom device, or with USB ECM networking on Linux 6.x. We also found no public report of the OUT-FIFO drain defect described in `docs/ROBUSTNESS.md` (E5). We did not search the LKML archives for it specifically.
- We found no public report of a TomTom navigator used as a **Home Assistant** sensor or display node.
- We found no public GO 630 hardware map (GPIO, ADC channels, BlueCore crystal setting, LCD timing) for a recent kernel. Our `docs/HARDWARE_MAP.md` is derived largely from TomTom's own GPL 2.6.13 source, so the facts are not new. Only their collection and check against a 6.1 kernel may be.

What is **not** new and should be credited in any release:

- Booting custom code by replacing `ttsystem` and keeping the stock bootloader: documented by OpenTom since 2004-2005 (findings 2, 10).
- A Linux 6.1 LTS board file under `arch/arm/mach-s3c` with ATAGs from the TomTom bootloader: done independently for the ONE XL (finding 1).
- BlueCore Bluetooth over BCSP on a TomTom with a modern BlueZ: also done for the ONE XL, which goes further (A2DP).

Most defensible wording for a release note:

> As far as we could find (searches listed in docs/PRIOR_ART.md, November 2026), this is the first public report of Linux 6.x on an S3C2443-based TomTom GO 630. A Linux 6.1 port for the S3C2412-based TomTom ONE XL was published in October 2026 and takes a similar approach.

If even "first public report" is too strong, use: "We found no earlier public report of ...".

## What we can reuse or contribute upstream

### From others

| Item | Source | Use here | Confidence |
|---|---|---|---|
| Board-file and early-boot ideas from the other TomTom 6.1 port (finding 1) | Finding 1 | Compare with `kernel/homtom/mach-homtom-go630.c` and our early-boot handling; read, do not copy (no licence file). | [H] |
| Bluetooth pairing and audio notes from the other port (finding 1) | Finding 1 | Reference for the same BlueCore family | [H] untested here |
| TomTom 2.6.13 GPL source (`golinux-tt*`, `opentom-kernel`) | Findings 2, 4, 15, 16 | Already the reference for pins and drivers (`docs/HARDWARE_MAP.md`). The 60 archived `golinux` versions could narrow down which one matches GO 630 firmware. | [V] |
| OpenTom userland (Nano-X, tslib config, `mkttimage`) | Finding 2 | `docs/emulator-plan.md` already plans to use it | [V] |

### To others

| Item | Where | Notes | Confidence |
|---|---|---|---|
| `kernel/patches/0001-s3c-hsudc-drain-out-fifo.patch` (drain both OUT buffers per interrupt) | Not mainline: `s3c-hsudc.c` was removed for 6.3 (finding 20). Options: post it to `linux-usb` and `linux-samsung-soc` as an RFC against 6.1 for the record, or publish it in this repo and in the ONE XL project's issue tracker for anyone on S3C2443/2416 hardware. | Stable rules normally require a fix to land in mainline first, so a 6.1-stable backport is unlikely. The patch has not been booted yet (`docs/ROBUSTNESS.md`). | [H] |
| GO 630 board file (`kernel/homtom/mach-homtom-go630.c`, `Kconfig.board`) | Not upstreamable to mainline: the S3C24xx platform no longer exists there. Publish it as a 6.1 out-of-tree patch series. | A DT conversion for current mainline would mean bringing back S3C2443 support first (clock, irq, pinctrl), a much larger project. ep93xx (finding 26) shows such conversions happen only for platforms with active users. | [H] |
| Hardware notes: GO 630 GPIO/ADC map, BlueCore crystal (`ANA_FREQ`) setting, INFORM0 boot-path values, bootloader storage-mode round trip, NOR safety incident | Public repo docs; OpenTom-derived wikis; the ONE XL project as a cross-reference | Mark user-reported against measured, as the repo already does | [V] docs exist here |
| `ttsystem` build script for a 6.1 zImage plus initramfs (`kernel/scripts/build-ttsystem-6.1.sh`) | Public repo | Shares the format with OpenTom's `mkttimage` and the ONE XL release | [V] |
| Report to `linux-samsung-soc` that S3C2443 hardware still runs 6.1 | One short informational mail after release | The 6.1 Kconfig help text asked users to come forward. That point has passed, but it is a useful record. | [H] |

## Where to announce a release

Suggested announcement posts, from what this research found. One post each.

- Home Assistant community forum (Share your Projects category). [H] category name not checked
- Hackaday tips line: OpenTom coverage in 2006 shows interest in the topic. [V] coverage exists
- The other TomTom Linux 6.1 project (finding 1): contact its author before reusing anything; there is no licence file. [V] exists
- OpenTom forks on GitHub (findings 2-4), for example an issue linking the 6.1 work. [V] repos exist
- mikrocontroller.net (German, OpenTom thread in finding 9) and tomtomforums.com. [V]/[H]
- `linux-samsung-soc@vger.kernel.org` and `linux-arm-kernel@lists.infradead.org`: informational only, with the hsudc patch as an RFC. Both lists appear in the 6.1 `MAINTAINERS` entry for ARM/SAMSUNG S3C. [V] tarball

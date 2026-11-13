# NOR plan: why the writes froze, and how to write the hand logo safely (2026-11-07)

[V] = checked against source, a binary or a dump in this repo, or against a fetched datasheet cited in 9.5. [M] = measured on the device by this project. [H] = hypothesis. Sections 1-7 are offline research. Section 8 records stages N1, N2 and N2b, which ran on the device on 2026-11-07 with the user's approval. Section 9 is the R1 writer (`tools/devtools/norwrite.c`), its datasheet check and its host harness; the device binary is built but has never been uploaded or run. Section 10 is the R2/R3 sector-erase analysis, host only.

## 1. Answer

- The two NOR writes on 2026-11-02 did not freeze because of the chip, a write-protect pin or the command set. The stock TomTom 2.6.13 kernel panics on purpose when anything erases or writes the NOR outside the ngffs area (0x40000-0x7FFFF). `norflash` asked MTD to erase 0x20000 and the kernel called `panic()` before it sent a single command to the chip. Confidence: high. The guard is in the kernel source and in the project kernel binary that was running. Only the panic text itself was never seen, because the unit has no console.
- So writing the NOR is not unsafe in itself. The danger is the layout. The hand logo (id 0) and the shared palette sit in the 64 KiB sector 0x20000-0x2FFFF, together with the last 18 KiB of the compressed bootloader. If an erase of that sector is cut short, the unit no longer boots, and there is no ROM fallback to recover it.
- There are workarounds. Section 5 describes a route with no erase at all that blanks the hand logo. Replacing it with homtom art needs the sector erase, so do that only once a JTAG adapter or chip programmer has been shown to read the chip.

## 2. Why the earlier writes froze

| Finding | Tag | Evidence |
|---|---|---|
| The TomTom AMD driver rejects any erase outside 0x40000-0x7FFFF with `CRASH_BURN` = `printk` + `dump_stack` + `panic("Goodbye cruel world...")` | [V] | `.runtime/kernel-build/drivers/mtd/chips/cfi_cmdset_0002.c:797-802` (macro), `:1637-1647` (erase guard), `:1088-1101` (same guard on writes). The read guard is under `#if 0`, which is why the dumps worked. |
| The running kernel has this guard | [V] | The kernel gzip at offset 0x1703E0 in `artifacts/kernel-builds/homtom-6.1/ttsystem.lab-repack` (SHA-256 47165003…, bit-identical to the project image) contains `Invalid erase request`, `Invalid write request` and `Goodbye cruel world`. The YOLO session ran on the project kernel: `swap-udc.log` shows its UDC modules. |
| Symptoms match a panic | [V] | `kernel/panic.c` turns IRQs back on and loops forever, and the cmdline has no `panic=` (`lab-usb-followup-dmesg.log:18`). IRQs and softirqs keep running, so ICMP answers while Telnet, FTP and all of userspace stop. Nothing calls `sync`, so `norflash.log` was never written. `network.log` stops at about 3104 s. |
| The chip was never told to erase | [V] | All 7 dumps, including `mtd0-after-yolo-attempt.bin`, have SHA-256 d48b41…. The guard runs before any bus cycle. |
| MTD reported the chip writable | [V] | Dry run: `flags 0x5` = MTD_CLEAR_BITS\|MTD_ERASEABLE (2.6.13 has no MTD_WRITEABLE bit). |
| Command set and unlock addresses | [V] correct | TomTom's `jedec_probe_chip` (`jedec_probe.c:2082ff`) forces EN29LV400BB with unlock words 0x555/0x2AA in x16 mode, through the `finfo_uaddr` quirk that returns `uaddr[0]`. The boot log reads `man=0x1c id=0x22ba` four times, once per 512 KiB alias of the 2 MiB window (`dmesg:95-102`). Bus writes therefore reach the chip and autoselect works. |
| The bootloader issues the same commands | [V] | `bootloader.out` 0x30B08918 is a word-program routine: IRQs masked, `AAAA@0xAAA`, `5555@0x554`, `A0A0@0xAAA`, data, then DQ6 toggle polling with a DQ5 timeout. 0x30B089E4 enters autoselect and reads sector-protect status (word 2) for sector 0, which feeds the info line `Sector protection: Yes/No` (0x30B098B0). Where the program routine is called from was not found. |
| WP# pin | [V] none | No NOR WP GPIO appears in `tomtomgo-iomodena.h` (only `WP_SD`). The EN29LV400 pin table lists A0-A17, DQ0-DQ15, CE#, OE#, WE#, RESET#, RY/BY#, BYTE#, Vcc and Vss, with no WP# or ACC pin (9.5, D1 and D2). Sector protection is set with RESET# or A9 at high voltage; N2 measured it. |
| Sector protection | [M] SA0 only | Stage N2: the autoselect protect word is 0x0001 for SA0 (0x00000) and 0x0000 for SA1-SA10. See section 8. |
| XIP (code running from NOR while it is erased) | [V] not a factor | Stage 1 runs from NOR only before Linux starts. The bootloader runs from RAM at 0x30B00000, the kernel runs from RAM, and `CONFIG_MTD_XIP` is not set. Under 6.1 no driver maps the NOR. |
| ngffs mounted / interrupts | [V] not the cause | The guard fires first. ngffs uses the same MTD path, but only at 0x40000 and above. |
| 6.1 MTD | [V] never tried | `workspace/kernel-trim/defconfig.diff:19`: the board file registers no NOR map, so 6.1 has no `/dev/mtd0`. |

Corrections to earlier claims, which should be carried into the other documents:

- `flash-safety.md` hypothesis 1 (hardware write protect) and the earlier note "bootloader sectors appear hardware-protected" are not supported. The cause is the software guard above.
- `flash-safety.md` rule 1 names 0x44000-0x4FFFF as a free block. It is not an erase unit: 0x40000-0x4FFFF is one 64 KiB sector, and the ngffs header sits at 0x40000.
- Refinement after N2 [M]: SA0 (vector table and device info block) does report sector protection. SA1-SA10, which include the stage 1 code, the gzip stream, the images and ngffs, do not. So "bootloader sectors appear hardware-protected" is true only for SA0, and the 2026-11-02 freeze at 0x20000 still comes from the software guard, not from protection.

## 3. Chip and layout

EN29LV400BB, x16, bottom boot. Sectors and contents come from the d48b41 dump [V]:

| Sector | Range | Content |
|---|---|---|
| SA0 | 0x00000-0x03FFF | Vector table, device info block at 0x100 |
| SA1, SA2 | 0x04000-0x07FFF | All 0xFF |
| SA3 | 0x08000-0x0FFFF | Stage 1 (0x8000-0xB293), start of the gzip stream |
| SA4 | 0x10000-0x1FFFF | gzip stream |
| SA5 | 0x20000-0x2FFFF | gzip tail to 0x247DC, 0xFF, palette at 0x28000, records id 0 (0x28201-0x2919A), 218, start of 219 |
| SA6 | 0x30000-0x3FFFF | Remaining records to 0x3ED8B, 0xFF, bootloader version string at 0x3FFF0 |
| SA7-SA10 | 0x40000-0x7FFFF | ngffs. SA8 holds `settings.dat`; SA7, SA9 and SA10 hold only a header at offsets 0x00-0x29 (0x2A bytes, 21 words; corrected from 0x29, see 9.1). |

`artifacts/homtom-nor/mtd0-homtom.bin` differs only in SA5 (0x28000-0x2FFFF, 32,253 bytes) and SA6 (0x30000-0x312B8, 4,752 bytes). SA0-SA4 and SA7-SA10 are identical [V]. Of the changed bytes, 21,308 in SA5 and 2,954 in SA6 need a 0→1 bit change, so both sectors need an erase [V].

## 4. Recovery facts

- [H, strong] The S3C2443 has no mask ROM: the boot source is set by the OM straps, and boot ROMs with SD or USB recovery arrived with the S3C2416/2450. The board has no raw NAND (moviNAND is eMMC on HSMMC). A corrupt SA0-SA5 therefore means JTAG or chip-off. Confirm against the S3C2443 user manual's boot-mode table.
- [V] The bootloader's storage mode and the `ttsystem` fallbacks only help while stage 1 and the gzip still decode.
- [H] SA6 and the image chain are read only to draw pictures. If SA6 is corrupt, the boot path that draws only id 0 may survive, because the lookup stops at the first record. The storage-mode scenes walk into SA6. Before relying on this, check the lookup loop and its end-of-chain handling in the disassembly (0x30008430 users).
- [H] A tool killed while the chip is in autoselect or CFI mode leaves the array unreadable until a reset command or a power cycle. Whether SWRST or the watchdog asserts the NOR's RESET# is unknown. Stage N2's tool therefore resets the chip and verifies the array before it exits.

Hardware routes. A SOIC clip does not apply: it is made for SPI flash, and this is a 48-pin parallel part that shares the bus with the SoC.

- **J (JTAG, in circuit).** Find TCK/TMS/TDI/TDO/nTRST/nRESET on PCB `DA0VF8MB8C0 REV.C`. OpenOCD supports `target arm920t` and `flash bank cfi 0 0x80000 2 2 $_TARGETNAME jedec_probe` [H: works if the chip answers CFI (stage N2 tells) or is in OpenOCD's non-CFI table]. Gate: a JTAG read of 512 KiB matches SHA d48b41.
- **P (chip-off).** Hot-air removal, a TSOP48 adapter on a universal programmer (XGecu T48 or TL866II Plus class; check that EN29LV400 or Am29LV400BB is in its device list), then reflow. Pre-program spare bottom-boot chips with the same sector map (EN29LV400AB, Am29LV400BB or S29AL004D-BB [H: check the maps]) with both the stock and the homtom image. This first needs a photo of the chip to confirm the package [H: TSOP48 vs FBGA48]. Correction (2026-11-07, `docs/BOARD_PHOTOS.md`): the photos show U9 as a leadless 48-ball FBGA (6 x 8 ball-index silkscreen, about 6.5 x 8.5 mm), not TSOP48. Route P is BGA rework with an FBGA48 socket adapter and reballing, which makes route J the first choice.
- **UART.** Pads for a bootloader serial log would help diagnosis [H: not located].

Undo images: `artifacts/backups/mtd0-before-flash-20261102T115819Z.bin` and `mtd0-before-yolo-20261102T174230Z.bin`, plus 5 dumps under `artifacts/bootloader-dump-*`. All have SHA-256 d48b41… [V].

## 5. Routes, safest first

| Route | What changes | Erase? | If interrupted |
|---|---|---|---|
| R0 | Nothing on NOR. FAT `SPLASHW.BMP` is already homtom | - | - |
| R1 Blank the hand | Program the 2,257 non-zero pixel-value bytes of record 0 to 0x00 (palette index 0 = 0x0000, black [V]). Leave the 1,377 RLE count bytes as they are, so the chain and lengths stay intact [V: token walk ends exactly at 0x2919A]. Programming only clears bits, so each halfword is written as (count byte kept, value byte 0). | None | Partly blacked logo. Boot code untouched |
| R2 SA6 only | Replace records that lie wholly in SA6 (220, 221, 222-239, 263) with re-encodes of exactly the same length in the stock palette. The hand logo and 218/219 stay stock | SA6 | Images corrupt; boot code untouched. Rewritable from Linux if the boot path survives (see 4) |
| R3 Full homtom | `mtd0-homtom.bin`: SA6 first, then SA5. After the SA5 erase, program 0x20000-0x247DC first (about 9.2 k words) | SA5, SA6 | Bricked if the SA5 erase or the gzip-tail program is cut short. Recovery by J or P |

R1 gives a black screen where the hand was, then the homtom splash. It is the only route that changes what the user sees at power-on without erasing a sector that holds boot code. A program command at a wrong address can still clear bits in the gzip, so the writer must hard-code the allowlist 0x28211-0x2919A and verify each word.

Writer design, for any route:

1. A freestanding ARMv4T tool on the `tools/devtools/norprobe/norprobe.c` skeleton: `/dev/mem` with O_SYNC (uncached) at physical 0, 16-bit accesses only, and a compiled-in sector and address allowlist per route. Before each step it verifies the full-chip FNV against the expected image. It uses the datasheet algorithm, the same one the bootloader uses: AA@555, 55@2AA, A0@555, data; DQ6 toggle polling; on DQ5 it re-reads, then resets with F0. For erase: AA, 55, 80, AA, 55, 30@sector. Every operation has a timeout; every word is verified after programming; progress goes to a log with fsync per sector.
2. IRQ masking is not needed in userspace. Nothing reads the NOR under 6.1, and the EN29LV400 datasheet sets no maximum gap between command cycles [V: no such parameter in the AC tables of D1 or D2]. The part does not support multi-sector erase, so the 50 µs sector-erase window of the AMD parts does not exist here either [V]. Signals stay blocked during each sequence.
3. Alternative: rebuild mainline MTD (physmap, cfi or jedec probe, cmdset 0002) as 6.1 modules with partitions where 0x00000-0x1FFFF is read-only, so a second layer refuses boot-sector writes. Mainline jedec_probe may not list EON parts. It works through cfi_probe only if stage N2 finds CFI.
4. Test the writer first against a host model of the AMD state machine, or against QEMU `pflash_cfi02` with the EN29LV400BB geometry (`emulator-plan.md` 3b), including a write-protect mode and a mid-erase kill.

Timings [V, 9.5]: sector erase 0.5 s typical, 10 s maximum; word program 8 µs typical, 300 µs maximum; chip erase 5 s typical, 100 s maximum. The R3 brick window (SA5 erase plus the 9,198-word gzip-tail program) is therefore about 0.6 s typical and at most about 12.8 s. On a battery-powered unit the main risk is a software fault, not power loss.

## 6. Staged plan with go/no-go gates

| Stage | Action | Bus writes | Go if | Stop if |
|---|---|---|---|---|
| N0 offline | (Datasheet part done 2026-11-07, see 9.5.) Get and read the EN29LV400(A)BB datasheet (no WP#? protect-verify address, timings, CFI). Review `norprobe.c`. Statically check the image lookup and end-of-chain handling. Optional: find the trigger for the bootloader info screen, which shows `Sector protection` without any lab write | None | Datasheet agrees with section 5 | Datasheet requires a different sequence |
| N1 read | Under 6.1: `norid dump /tmp/nor.bin`, fetch it, SHA-256 on the Mac | None | SHA d48b41, or `fnv_low` 17f85537 | Mismatch: the mapping or bus width is wrong; stop |
| N2 identify | User approval, then `norid id SEND-ID-COMMANDS`. Commands are limited to AA/55/90/98/F0 | Commands only | mfr 0x001C, dev 0x22BA, `array verified`. Record the protect bits for all 11 sectors and whether CFI is present | Exit 3: do not reboot; user present, retry reset, then power cycle |
| N3 safety net | User buys and sets up route J or P; a full chip read matches d48b41 | None on the device NOR | Verified read by the external route | No route: only R1 is allowed, and only with explicit acceptance of the risk |
| N4 path proof | Erase SA10 (0x70000) and program its 0x2A-byte header (21 words) back from the backup. 6.1 does not mount ngffs | SA10 only | Erase within the datasheet max, read-back equals the backup, unit responsive throughout | SA10 reported protected, timeout, or read-back mismatch |
| N5 write | R1, or R2, or R3 (R3 only after N3) | Allowlisted range only | Full-chip FNV equals the expected image | Any verify failure: stop, keep the unit powered, rewrite from the backup with the same tool |
| N6 confirm | User cold boot, normal path and storage mode; record in `LESSONS.md` | None | Logo or scenes as expected | Boot fails: recover by J or P |

## 7. What the user needs

- Now: nothing to buy for N0-N2. N2 needs the user's approval, because it sends ID commands to the chip (no program and no erase).
- Before R2 or R3: a photo of both PCB sides, to find the NOR package and any JTAG or UART pads. Then either an FT2232H-based JTAG adapter (or a J-Link EDU Mini) with fine wire, or a universal programmer with a TSOP48 adapter, a hot-air station and 2-3 spare bottom-boot 4 Mbit chips.
- R1 needs no hardware, but a fault in the writer could still brick the unit. Accept it only after N1, N2 and N4 pass.

## 8. Measured results: N1 and N2 (2026-11-07, Linux 6.1.191 #97)

Tool: `tools/devtools/norid.c`, a reviewed copy of the `norprobe` draft built with `tools/devtools/build.sh norid` (binary SHA-256 73dc6ed0…). The draft in `tools/devtools/norprobe/` is unchanged. Artifacts are in `artifacts/nor-analysis/n1-n2-20261107/` with `SHA256SUMS`.

Pre-run review [V]:

- Physical address: the stock 2.6.13 config has `CONFIG_MTD_PHYSMAP_START=0x00000`, `LEN=0x80000` and `BANKWIDTH=2`. The SMC register SMBCR0 (0x4F000014) reads 0x00303010 on the device, so bits [5:4] are 01 and bank 0 is 16 bits wide [M].
- Mapping: `/dev/mem`, opened O_SYNC, mmap2 at page 0. Physical 0 is not a valid RAM pfn (RAM starts at 0x30000000), so 6.1's `phys_mem_access_prot` returns `pgprot_noncached` (C=0, B=0 on ARMv4). `CONFIG_STRICT_DEVMEM` is not set.
- Bus accesses: the disassembly has only `ldrh` on the NOR mapping and exactly one `strh`, inside `cmd()`. That store runs only after a (word, data) match against a 5-entry table, which appears once in the binary: (555,AA) (2AA,55) (555,90) (055,98) (000,F0). Word 0x555 is byte offset 0xAAA and 0x2AA is 0x554, the same addresses as the bootloader's program routine. The binary has no program, erase, unlock-bypass or protect sequence and no ARMv5 instructions.
- Nothing else uses the NOR [M]: `/proc/iomem` has no resource below 0x30000000, `/proc/mtd` and `/dev/mtd*` do not exist, and the only `/dev/mem` mappings belong to statusd, touch and the GUI, at 0x4C000000, 0x56000000, 0x58000000 and 0x33F00000. The ARM920T `crval` sets the V bit (0x3135), so exception vectors are at 0xFFFF0000 in RAM, and nothing executes from physical 0 until the next reset.
- Timeout: setitimer gives a 20 s SIGALRM. During the command window every signal except SIGALRM is blocked.

Results:

| Gate | Result | Evidence |
|---|---|---|
| A: read-only dump | Pass [M] | `nor-n1-linux61.bin`, SHA-256 d48b41abc85c…128ead5, byte-identical to `artifacts/backups/mtd0-before-flash-20261102T115819Z.bin`. `fnv_low` 17f85537, `fnv_all` 3642476a. Physical 0x80000 aliases 0x00000, which fits a 512 KiB part on a wider chip select. |
| B: identify | Device ID pass; manufacturer word differs from the gate as written | Word 0 = **0x007F**, word 1 = **0x22BA** (EN29LV400 bottom boot). 0x7F is the JEDEC continuation code. The stock `jedec_read_mfr` (`jedec_probe.c:1815-1835`) skips 0x7F and reads word 0x100, which is how the 2.6.13 boot log got `man=0x1c`. EON's JEDEC code is 0x7F followed by 0x1C, so the result is consistent with the chip, but this project did not read word 0x100 itself. |
| B: protection | Measured [M] | Protect word (sector base + 2) = 0x0001 for SA0, 0x0000 for SA1-SA10 |
| B: CFI | Not present [M] | After 98@55, words 0x10-0x3C read the normal array (0x8004, 0x0000, 0x8008…). The chip ignores the CFI query and stays in read mode. |
| B: reset and re-dump | Pass [M] | After F0 the 50 check words matched, the whole-array FNV was 3642476a, and a later full dump (`nor-n2-after-reset.bin`) is again SHA-256 d48b41…, byte-identical. Uptime went from 343 s to 498 s with no reset. The GUI and services kept running. |

What follows from this:

- The unit has no CFI. OpenOCD `flash bank cfi` will not probe it through CFI, and mainline `cfi_probe` will not either. Route J needs OpenOCD's non-CFI (JEDEC) table to cover the EON part, or a manual geometry. A 6.1 MTD route needs `jedec_probe` with an EON EN29LV400BB entry (mfr 0x7F/0x1C, id 0x22BA).
- SA0 is protected and the other sectors are not. An erase or program aimed at SA0 should fail on the chip, which is an extra layer for the vector table. It does not protect stage 1 or the gzip (SA3-SA5), so the R3 brick window in section 5 stays as described. SA10, the N4 target, is unprotected.
- The bootloader's `Sector protection: Yes/No` line reads sector 0 only, so it should show Yes.

Go/no-go:

- N2 is done apart from the manufacturer byte: the gate expected 0x1C at word 0 and the chip returns 0x7F there. Closing this needs either the user's acceptance of the continuation-code explanation, which the stock driver source and boot log support, or a separate approval for one more autoselect read of word 0x100. That read uses the same allowlist with no new commands. **Closed by N2b below.**
- N3 (external read path): **go**. It writes nothing to the device's NOR. Use the JEDEC route, not CFI.
- N4 (SA10 erase): **no-go for now**. It is the first stage that needs a program or erase capability. Before it runs: the user closes N2's manufacturer item, a separate writer is written and tested against a host AMD model without CFI (section 5, step 4), and the user explicitly approves. Starting N4 before N3 needs explicit acceptance of the risk. The measured state is otherwise good for it: SA10 is unprotected, the bus and mapping are proven, and reset recovery works from Linux.

### 8.1 N2b: JEDEC continuation byte (2026-11-07, same boot)

Tool: `tools/devtools/norid2.c`, a copy of `norid.c` built with `tools/devtools/build.sh norid2` (binary SHA-256 a6256128…0719). Changes from norid: the CFI step is removed, and while in autoselect the tool also reads words 0x100-0x103 and 0x200. The allowlist table and `cmd()` are unchanged. The tool sent only AA@555, 55@2AA, 90@555 and F0@0; 98 is still in the table but nothing calls it. An independent review of the source and disassembly passed before the run: one `strh` can reach the mapping, the one in `cmd()` behind the table; every NOR access is 16-bit; there are no ARMv5 instructions; the syscalls are correct; the FNV gate runs before any write. The uploaded binary was fetched back over FTP and matched by SHA-256. Artifacts are in `artifacts/nor-analysis/n2b-20261107/` with `SHA256SUMS`.

| Gate | Result | Evidence |
|---|---|---|
| Manufacturer, bank 1 | **0x001C (EON)** [M] | Word 0x100 = 0x001C after 0x007F at word 0. The JEDEC ID is 0x7F 0x1C = EON, as the stock driver and the 2.6.13 boot log implied. |
| Other autoselect words | Measured [M] | 0x101 = 0x22BA, 0x102 = 0x0001, 0x103 = 0x0000, 0x200 = 0x007F. Inside a sector, autoselect decodes the low byte of the address plus A8 only: bank 2 (0x200) repeats 0x7F. Device ID 0x22BA and the protect bits (SA0 = 1, SA1-SA10 = 0) are the same as in N2. |
| Reset and whole-chip hash | **Pass** [M] | `array verified after reset`, `fnv_all_after 3642476a`, exit 0. A later `norid2 dump` fetched to the Mac has SHA-256 d48b41abc85c…128ead5, byte-identical to `artifacts/backups/mtd0-before-flash-20261102T115819Z.bin`. |
| Unit health | Pass [M] | Uptime went from 734 s to 746 s with no reset; load average about 1.1 with the GUI running. `/tmp/norid2` and the dump were removed from the device afterwards. |

N2 is now complete: mfr 0x7F/0x1C, device 0x22BA, SA0 protected, no CFI, and reset recovery works from Linux.

## 9. R1 writer, datasheet check and host harness (offline)

Status (2026-11-07): the R1 writer `tools/devtools/norwrite.c` exists and is built for the device (9.7), but the binary has never been uploaded, staged or run. Every result in this section comes from the host model and the harness. No device stage below may run without the remaining gates in 9.8 and a separate approval.

### 9.1 Host model

`workspace/nor-model/` (Python stdlib; run `python3 -m unittest` there):

- `en29lv400bb.py` models the chip at 16-bit word addressing: 0x40000 words; sectors 16K, 8K, 8K, 32K and 7 x 64K bytes; addresses alias above 512 KiB. It implements the command state machine: unlock AA@555/55@2AA, autoselect 90, program A0, sector erase 80 then 30@sector, chip erase 10@555, and F0 reset, which is ignored while busy. Any other sequence returns the chip to the mode it was in. Programming only clears bits (`cell &= data`); asking for a 0→1 change ends with DQ5 = 1 and needs F0. SA0 is protected, so program and erase there show busy briefly and change nothing. Status reads model DQ7, DQ6, DQ5, DQ3 and DQ2. Time is virtual. Fault hooks: power loss, which partly clears the word being programmed or randomises a sector being erased; an operation that stays busy for ever; and the bank 1 autoselect value.
- After the datasheet check (9.5) the model changed in four places: maximum word program time 200 → 300 µs; a program to a protected sector is busy for 2 µs instead of 1 µs; sector erase 0.7 s → 0.5 s typical and 15 s → 10 s maximum; and the 50 µs multi-sector erase window is gone, so DQ3 reads 1 straight after the 30 cycle and a second 30 is ignored. The DQ3 test changed accordingly and a `Datasheet` test class was added.
- `writer_ref.py` is the Python reference algorithm the C writer follows: allowlisted program, toggle-bit polling with a DQ5 re-check and F0 on failure, per-operation timeouts, and the R1 and N4 plans.

Results (29 tests, all pass, 2026-11-07, after the datasheet changes):

| Property | Result |
|---|---|
| Measured N2/N2b answers reproduced; the identify sequence leaves the array bit-identical | pass |
| Programming only clears bits; a 0→1 attempt gives DQ5 and needs a reset; F0 is ignored while busy; DQ7 shows the complement of the data | pass |
| A program to protected SA0 toggles DQ6 for about 2 µs without DQ5 and changes nothing; a sector erase starts at once and ignores a second 30 and F0 | pass |
| A stuck-busy chip trips the per-word timeout, and the writer issues F0 and stops | pass |
| An allowlist violation, a 0→1 target, or a foreign pre-image is refused before the first bus write | pass |
| Program or erase in protected SA0 changes nothing; chip erase skips SA0 | pass |
| R1 plan: 1,669 words (0x1410B-0x148CC; bytes 0x28216-0x29198), 2,257 bytes and 9,002 bits cleared, all inside 0x28211-0x2919A, every target a bit-subset of the stock word | pass |
| R1 result: record 0 decodes with every pixel at index 0 (black); all 24 records still chain; bytes outside the allowlist are unchanged; no 80 command | pass |
| R1 power cut at 200 random points, then rerun: never outside the allowlist, RLE count bytes never change, completes to the exact target | pass |
| R1 is idempotent; R1 with SA5 unexpectedly protected stops at the first word and changes nothing | pass |
| N4: a power cut mid-erase of SA10 leaves SA0-SA9 intact; erase again and 21 header words restore d48b41 | pass |
| R3 sequence gives `mtd0-homtom.bin` exactly; a cut during the SA5 erase destroys the gzip tail | pass |

Correction found by the model earlier: the ngffs header in SA7, SA9 and SA10 spans bytes 0x00-0x29, which is 0x2A bytes, not 0x29. Sections 3 and 6 carry the corrected value.

### 9.2 Stage R1: the writer

Effect: record 0 (id 0, the 237x51 TOMTOM wordmark with the hand) is drawn all black. The header, the lengths and the RLE count bytes do not change. Only value bytes go to 0x00, and palette index 0 is 0x0000.

`tools/devtools/norwrite.c` with the generated table `tools/devtools/norwrite_plan.h` (`workspace/nor-model/gen_plan.py` writes it from `writer_ref.r1_plan` on the d48b41 dump; `gen_plan.py --check` confirms the header on disk). Freestanding ARMv4T on the `norid2` skeleton: raw syscalls, `/dev/mem` opened O_SYNC and mapped at physical 0 (uncached, as in section 8), 16-bit accesses only.

| Mode | Bus access | What it does |
|---|---|---|
| `--dry-run` | none | Prints the plan: 1,669 words, word range 0x1410B-0x148CC, byte allowlist 0x28211-0x2919A, 2,257 bytes and 9,002 bits cleared, the command list, and the plan, stock and target SHA-256 |
| `--plan-hash` | none | Prints the plan SHA-256 (`e6135999f3b3e403a95871aa473ec401fbf4e46901915589c2c68fd64a9d8d1e`, computed at run time from the compiled-in table) |
| `--check` | reads, read-only mapping | Whole-chip SHA-256 and state: stock, patched, partial or foreign |
| `--reset` | F0 only | F0, settle, F0, settle, then as `--check`. For a chip left in autoselect by a killed tool |
| `--write R1-BLACK-LOGO` | reads, commands, plan words | The R1 procedure below, on a stock chip |
| `--resume R1-BLACK-LOGO` | same | Same, and also accepts a partial R1 state |

Structure:

- All bus access goes through `read16(word)` and `write16(word, value)`, and all time through `now_us()`. With `NORWRITE_HOST` defined, these call into the harness; otherwise they are the `/dev/mem` mapping and `clock_gettime(CLOCK_MONOTONIC)`. The decision code is the same text in both builds.
- `write16()` has two callers. `cmd()` stores only the table pairs (555,AA) (2AA,55) (555,90) (555,A0) (800,F0). `prog(i, cur)` stores only plan entry i's target at plan entry i's word, after checking the word range, that the target is a bit-subset of both the stock word and the word just read, and that it differs from the current value. The address and data of a program cycle come only from the compiled-in table, never from a read.
- Reset goes to word 0x800 (byte 0x1000), not word 0. The datasheet makes the reset address a don't-care [V], and word 0x800 is an erased word inside protected SA0. This only matters if a killed earlier run left the chip in program setup and the chip takes the next F0 as program data: it then lands on an unused erased word in a protected sector instead of the reset vector. The harness found this case (9.6).
- The default build has no erase, unlock-bypass, CFI or protect command. The R2/R3 erase path sits behind `#ifdef NORWRITE_ERASE_EXPERIMENT`, which stops the compile with `#error` unless `NORWRITE_HOST` is also set. `build.sh` sets neither.

`--write` / `--resume` procedure:

1. Self-check of the compiled-in plan: words strictly increasing and inside 0x1410B-0x148CC, every target a strict bit-subset of its stock word, every changed byte inside 0x28211-0x2919A, plan SHA-256 equal to the compiled-in value. Failure: exit 4 before mapping `/dev/mem`.
2. Gate, read-only: one pass over the whole chip computes the raw SHA-256 and a normalised SHA-256 in which every plan word is replaced by its stock value. Normalising is only allowed when each plan word lies between target and stock bit-wise. Raw = 87dc81ca… → patched: exit 0 with no bus writes. Raw = d48b41…, with every plan word equal to its table stock value → stock. Normalised = d48b41… → partial (an interrupted R1), accepted only with `--resume`, otherwise exit 5 with no bus writes. Anything else → foreign: exit 4 with no bus writes.
3. Signals blocked except SIGALRM; a hard 30 s ITIMER_REAL. F0, settle (DQ6 stops toggling at word 0x800 within 1 ms), F0, settle. This clears a half-sent sequence left by a killed run.
4. Identify: AA@555, 55@2AA, 90@555; read words 0x000, 0x100, 0x001, 0x002 and SA5's protect word 0x10002; F0. Then re-hash the whole chip; it must equal the gate hash, otherwise exit 3. Required: low byte 0x7F at word 0, 0x1C at word 0x100, 0x22BA at word 1, and SA5 unprotected. Otherwise exit 4 (the array has been verified unchanged). Since 9.9, SA0 must also read protected (low byte 0x01 at word 0x002), otherwise exit 6.
5. Per plan word: read it. Equal to the target → skip. Bits set that the stock word lacks, or a target that needs a 0→1 change → stop. Otherwise `prog()`, then the datasheet's toggle-bit algorithm (flowchart 6) on that word: DQ6 stops toggling → done; DQ6 toggling with DQ5 = 1 → re-check once, still toggling → DQ5 failure; 1 ms per-word timeout (datasheet maximum 300 µs), with one more toggle check after the timeout in case the process was descheduled. Then read the word back: it must equal the target.
6. Any failure: F0, report (plan index, word, before, read, want, and whether the chip is back in read mode), stop with exit 3, and never retry by itself.
7. Final whole-chip SHA-256 must equal **87dc81ca967579b424e3e48d1e680685549ee0d73d433d2cfd00c5c4611a384e**, otherwise exit 3.

Exit codes: 0 done or already patched; 2 usage; 3 stopped after bus writes (keep the unit powered, do not reboot, dump with `norid2` and analyse); 4 refused before any program cycle; 5 partial state without `--resume`; 6 SA0 not reported protected; 7 single-instance lock held. Codes 6 and 7, the lock and the clock check were added after the review (9.9).

Expected device time [H]: three whole-chip passes of 262,144 uncached reads with SHA-256, plus 1,669 programs at 8 µs typical (300 µs maximum, so at most 0.5 s). The 30 s hard limit leaves a wide margin. The model's virtual bus time is 0.81 s at 1 µs per access.

### 9.3 Sector-erase path (N4, then R2/R3)

Moved to section 10. The erase code exists only as the host-only `NORWRITE_ERASE_EXPERIMENT` block in `norwrite.c`, used by the harness for the analysis there.

### 9.4 Go/no-go (2026-11-07)

Superseded by 9.8.

### 9.5 Datasheet check (gate R1-G0)

Sources, fetched 2026-11-07. The PDFs are not stored in the repo; the SHA-256 identifies the exact copy read.

- **D1**: EON EN29LV400, "4 Megabit (512K x 8-bit / 256K x 16-bit) Flash Memory, Boot Sector Flash Memory, CMOS 3.0 Volt-only", Rev. C, issue date 2004/03/18, Eon Silicon Solution Inc., 42 pages. Mirror URL `https://datasheet4u.com/pdf/548411/EN29LV400.pdf`, SHA-256 a5c1f93c68f8739a9caf4ac0736354c6f359e76aaf6ae2af6397a38f56ceac85.
- **D2**: EN29LV400A, same title, Rev. I, issue date 2016/05/23, 43 pages, with a revision list from Rev. A (2004/01/07); later pages carry the Elite Semiconductor Memory Technology footer. Mirror URL `https://datasheet4u.com/pdf/1347128/EN29LV400A.pdf`, SHA-256 839e42f7a95a330e5f35999753285dcbff8b492baacc72cbdfef40ad32c427f6.

Both are third-party mirrors; no copy from the manufacturer's own site could be fetched (one distributor link returned an error). The chip on the unit reports 0x7F/0x1C/0x22BA, which both documents list for the bottom-boot part. The package marking has not been read, so which of the two revisions matches the chip is unknown; the points below agree between them unless noted.

| Point | Datasheet [V] | Where | Effect on writer / model |
|---|---|---|---|
| Program command | Word mode: AA@555, 55@2AA, A0@555, then PD@PA. Byte mode uses AAA/555/AAA | D1 Table 5 p. 11; D2 Table 5 | Writer uses exactly these word addresses with upper address bits 0 |
| Autoselect | AA@555, 55@2AA, 90@555. Manufacturer 1Ch at word 100 (A8 = H); with A8 = L the chip returns 7Fh (D2 Rev. G adds this). Device ID 22BA (bottom boot) at X01. Sector protect verify at (SA)X02: XX01 protected, XX00 not. Decoded address bits: A0, A1, A6, A8, plus A17-A12 for the sector | D1 Tables 4-5 p. 7, 11; D2 Table 4 note 1 | Matches N2/N2b. Writer checks 7F, 1C, 22BA and SA5 protect = 00 |
| Unlock address decode | Not stated. The table gives 555/2AA; SA bits are A17-A12 | D1/D2 Table 5 | Stays [H] in the model (A10-A0). The writer always writes the exact addresses, so the decode width does not change what it does |
| Invalid sequences | "Incorrect addresses, incorrect data values or improper sequences will reset the device to Read Mode" | D1 p. 11; D2 | Model: returns to read mode (stays in autoselect if it was there: [H]) |
| Reset F0 | Address don't-care. Accepted between the cycles of a program, erase or autoselect sequence before the operation begins; required to leave autoselect; returns to read mode after DQ5. "Once programming begins, however, the device ignores reset commands until the operation is complete"; the same for erasure | D1 p. 12 | Model ignores F0 while busy. F0 written straight after A0 is ambiguous ("before programming begins" vs the 4th cycle being data): the harness runs both readings (9.6), and the writer's reset address 0x800 makes both harmless |
| DQ7 Data# polling | While programming, DQ7 reads the complement of the data; after completion, true data. During erase 0, after erase 1. Must poll an address in the sector being programmed, not a protected one. DQ7 can change before DQ0-DQ6 are valid; read again for valid data | D1 p. 14 | Writer uses the toggle algorithm and then a separate full read-back, which covers DQ7 |
| DQ6 toggle | Toggles on every read while a program or erase runs; stops when done. Read twice; if toggling and DQ5 = 1, read twice more; still toggling = failure, write reset | D1 p. 15-16, flowchart 6 | `wait_done()` implements exactly this |
| Protected sector | Program: "DQ6 will toggles for about 2 µs, then stop toggling without the data in the sector having changed". Erase of only protected sectors: about 100 µs, then read mode, no change. No DQ5 | D1 p. 15; D2 "DQ6: Toggle Bit I" | Silent failure: the writer catches it with the read-back and SA5's protect word. Model busy time 1 → 2 µs |
| DQ5 | Set when the internal pulse-count limit is exceeded, for example a program of 1 over 0. May read 1 just as the operation completes, so re-check DQ6. Reset needed to return to read | D1 p. 15 | As modelled; writer re-checks DQ6 before treating DQ5 as failure |
| DQ3 / multi-sector | "This device does not support multiple sector erase command sequences"; DQ3 shows 1 right after the first 30 | D1 p. 15; D2 | Model: 50 µs window removed |
| Sector erase | AA@555, 55@2AA, 80@555, AA@555, 55@2AA, 30@SA. Once begun, only Erase Suspend (B0, at most 20 µs to suspend) is valid; all other commands ignored. Erase is preceded by an internal preprogram to 00h | D1 p. 11, 13; D1 Table 11 | Erase suspend is not modelled (no writer uses it). An interrupted real erase may leave 0x0000 words; the model randomises instead |
| Unlock bypass | D1 lists 20h / A0 PA PD / 90-00. D2 Rev. C (2006/02/21) removed the unlock-bypass commands | D1 Table 5; D2 revision list | Not used, not in the writer |
| Timing | Word program 8 µs typical, 300 µs maximum. Sector erase 0.5 s typical, 10 s maximum (excluding the 00h preprogram). Chip erase 5 s typical, 100 s maximum. Hardware reset during an embedded algorithm: 20 µs to read | D1 Table 11 p. 31, reset timing p. 26; D2 Table 11 | Model defaults updated; writer timeout 1 ms per word |
| Write protect | No WP# or ACC pin. Hardware protection by RESET# or A9 at Vid (sector protect / temporary unprotect). Low-Vcc write inhibit below VLKO (< 2.5 V); write pulses under 5 ns ignored | D1 pin table p. 3, p. 9-10 | Section 2 row updated |

Still [H] after the datasheet: the unlock decode width and whether the upper data byte of a command is ignored (the writer sends 0x00AA, 0x0055 and so on, so neither matters to it); whether F0 written as the 4th program cycle resets or programs (both handled); what an invalid sequence does in autoselect mode; the cell states an interrupted erase leaves; the real program and erase times of this unit; behaviour under bus glitches; and whether the bootloader tolerates an all-black record 0 (low-to-moderate confidence that there is no checksum over the image region).

R1-G0 is met for the program path: the command, toggle/DQ5 algorithm, maximum word program time and protected-sector behaviour are confirmed from fetched documents, the model is updated, and all model tests pass. It is only as strong as two third-party mirror copies, one of which may be a different revision from the chip.

### 9.6 Harness (gate R1-G1)

`workspace/nor-model/harness/` (`python3 run_harness.py [OUTDIR]`; needs a host C compiler):

- `harness.c` includes `tools/devtools/norwrite.c` unchanged, with `NORWRITE_HOST` defined, and provides `read16`/`write16`/`now_us` from `nor_model.c`, a C port of `en29lv400bb.py` with the same states, rules and virtual clock, plus harness-only fault hooks (forced DQ5, slow program, silent protection, the F0-after-A0 variant). Process deaths are simulated with `longjmp` out of the bus callback.
- A bus monitor checks every write the writer makes: only AA@555, 55@2AA, then 90@555 or A0@555; after 90 only F0@800; after A0 exactly one data write of plan entry i's target to plan entry i's word; F0@800 when idle. Any other write fails the run. It also counts any 80, 30 or 10 byte in a command slot as an erase-capable cycle.
- Invariants after every faulted run: every word outside the plan equals stock; every plan word lies between target and stock bit-wise (so no 0→1 change and no RLE count byte change). Every distinct intermediate image region is decoded with the repo's decoder, `firmware/norimage.py`. Then a recovery ladder must reach 87dc81ca: `--write` (or `--resume` when it reports partial), else `--reset` and again, else a power cycle and again. Then one more `--write` must make zero bus writes. Any run that prints "no bus writes" is checked to have made none.
- The clean run's transcript of every bus read and write is replayed into a fresh Python model; every read value must match the C model and the final image must hash to 87dc81ca. Eight random-traffic transcripts (program, 0→1, autoselect, sector and chip erase, garbage, CFI, bad unlocks, waits) are replayed the same way to compare the two models read by read.

Results (2026-11-07, 0 failures):

| Case | Runs | Faulted run exit | Recovery |
|---|---|---|---|
| Clean run | 1 | 0; 812,559 bus ops, 6,682 writes, 1,669 program cycles, 0.81 s virtual | final SHA 87dc81ca… |
| Rerun on the patched chip | 1 | 0; 0 bus writes (262,144 reads) | - |
| Power cut, uniform over the program phase | 1,000 | cut | 998 via `--resume`, 2 direct |
| Power cut, mid-word (chip busy; partial bit clears) | 400 | cut | 400 via `--resume` |
| Power cut in gate, ID or re-hash | 100 | cut | 100 direct |
| Power cut around the autoselect commands | 24 | cut | 24 direct |
| Power cut in the final hash | 50 | cut | 50 direct (already patched) |
| Process killed, program phase (chip finishes; half the runs with each F0-after-A0 reading) | 300 | killed | 300 via `--resume` |
| Process killed around autoselect | 24 | killed | 12 direct, 12 need `--reset` (chip left in autoselect; the hash gate refuses with no writes) |
| DQ5 on a random program | 120 | 3 (0 program cycles after the fault) | 120 via `--resume` |
| Stuck busy on a random program | 40 | 3 (F0 sent while busy, ignored) | 40 need a power cycle, then `--resume` |
| Program slower than the 1 ms timeout | 40 | 3 (F0 while busy; the chip finishes) | 40 via `--resume` |
| Process descheduled 20 ms mid-poll | 50 | 0 (the post-timeout re-check sees completion) | - |
| Wrong ID (AMD, wrong bank 1, top boot, other device, other maker, 7F/7F) | 6 | 4; 0 program cycles, chip stock and in read mode | - |
| SA5 protected (reported) / silently protected | 1 / 1 | 4 / 3 after one program cycle; chip stock, read mode | direct |
| Foreign image (one word differs: SA3, next to the plan, a plan word over-cleared, SA6) | 4 | 4; 0 bus writes | - |
| Single bit flip on one read (300 anywhere, 300 in the program phase) | 600 | 260 × 0, 236 × 3, 104 × 4 | 551 direct, 48 `--resume`, 1 `--reset` |

Totals: 8,164 writer runs; 18,396,568 bus writes checked by the monitor; **0 monitor failures; 0 erase-capable cycles**; 1,613 distinct intermediate image regions, all decoding to 24 records with unchanged ids and sizes, an unchanged palette and records 1-23, and record 0 pixels that only lost bits. Replay: 805,877 reads matched the C model one by one, 6,682 writes, Python image SHA-256 87dc81ca…; the Python model saw only the commands 90 and A0 after an unlock. Fuzz: 8 transcripts, 103,015 bus ops, all reads equal between the C and Python models.

Found and fixed by the harness: in the first run, 59 of 300 kills during the program phase did not converge. A kill between A0 and the data cycle leaves the chip in program setup. The next run's first command, AA@555, then became program data aimed at word 0x555 in SA0 (protected, so nothing changed, but the ID check then failed). The writer now starts the write phase with F0, settle, F0, settle, and sends F0 to word 0x800 instead of word 0. With SA0 protected [M], neither reading of the reset rule changes the array. Residual [H]: if SA0 were not protected and the chip took F0 as program data, word 0x800 (0xFFFF, unused) would become 0x00F0. The chip would then hash as foreign, and the writer would refuse every later run. Since 9.9 the writer refuses (exit 6) before any program cycle unless SA0 reports protected, so this case cannot arise from a run of the current build.

Only SIGKILL (or SIGSTOP, which just pauses) can stop the writer between two command cycles, because every other signal is blocked and SIGALRM fires only after 30 s.

### 9.7 Device binary (R1-G2 self-check; the independent review is still open)

Built with `tools/devtools/build.sh norwrite` only (Debian bookworm container, `arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -O1`). Not uploaded, staged or run on the device.

- `tools/devtools/norwrite`: 24,412 bytes, SHA-256 **dcd8297a20e0c4f3ae10569a355c3ff2040b097dc6caf65a1279ec1c759382c9**. Superseded by 8e575d24… (9.9); this section describes the reviewed build.
- ELF attributes: `Tag_CPU_arch: v4T`, EABI5 soft-float. The disassembly (1,664 lines) contains no blx, clz, ldrd, strd, pld, bkpt, saturating or DSP multiplies, ldrex/strex, rev, extend, movw/movt or barrier instructions. `umull` appears twice (the divide-by-1000 in `now_us`); it is an ARMv4 long multiply, which the ARM920T has.
- Exactly two `strh`: one in `cmd()` behind the table compare, and one in `run_r1` where `prog()` is inlined. Its address and data come from the plan tables and it follows the range, subset and 0→1 checks and the three `cmd` calls (AA, 55, A0).
- `.rodata` byte check: the command table (555,AA)(2AA,55)(555,90)(555,A0)(800,F0) appears once; the pairs (555,80), (555,10), (555,20) and (055,98) do not appear; the plan word, target and stock tables each appear once and byte-match `writer_ref.r1_plan` on d48b41.
- Run under ARM user-mode emulation in a container, never on the device: `--dry-run` printed the plan above and the plan SHA-256 e6135999…8d1e computed by the binary's own SHA-256 code; `--plan-hash` printed the same; `--write yes` printed usage and exited 2. `--check`, `--reset` and `--write` were not run anywhere except against the model.

### 9.8 Gates before any device run of R1 (2026-11-07)

| Gate | State |
|---|---|
| R1-G0 datasheet | **Met** for the program path (9.5), from two mirror copies. Open item: photograph the chip marking to tell EN29LV400 from EN29LV400A |
| R1-G1 host trace and faults | **Met** (9.6). The writer source runs unchanged against the model; the transcript replays into the Python model to 87dc81ca… |
| R1-G2 independent review | **Met for dcd8297a…** (`docs/NOR_REVIEW.md`, GO). **Open for 8e575d24…**: the F1-F3 hardening (9.9) changed the binary; a second person must review the delta (`NOR_REVIEW.md`, "Delta since the reviewed build") |
| R1-G3 same-boot preconditions | Open (device): `norid2 dump` gives SHA d48b41, or `norwrite --check` reports `stock`; `/proc/iomem` has nothing below 0x30000000 and there is no `/proc/mtd`; the uploaded binary is fetched back and matches 8e575d24… (the build that passed the delta review); `norwrite --dry-run` on the device prints plan SHA e6135999…; no `/tmp/norwrite.lock` and no other `norwrite` in `ps` |
| R1-G4 approval | Open: explicit approval for R1, recorded, accepting that no external recovery route exists until N3 and that undoing R1 needs an SA5 erase |
| R1-G5 result | Open: `norwrite` reports `R1 complete`; a separately fetched `norid2 dump` has SHA 87dc81ca… on the Mac; then the user cold boots (normal path and storage mode) and records the result in `LESSONS.md` |

Independent reviewer checklist for R1-G2:

1. `python3 workspace/nor-model/gen_plan.py --check`, then compare `norwrite_plan.h` with `writer_ref.r1_plan` independently (1,669 entries, every target ⊆ stock, every changed byte in 0x28211-0x2919A).
2. Read `norwrite.c` end to end. Confirm that `write16()` has exactly two callers, that `cmd_allowed` holds only the five pairs, that nothing outside the `NORWRITE_ERASE_EXPERIMENT` block can issue 80, 30, 10, 20, 98 or B0, and that this block cannot compile without `NORWRITE_HOST`.
3. Rebuild with `tools/devtools/build.sh norwrite` and compare the SHA-256 with dcd8297a… (a second build on 2026-11-07 gave the same hash; if a later toolchain differs, review the new binary instead and record its hash).
4. Disassemble: confirm `Tag_CPU_arch: v4T`, no ARMv5+ instruction, exactly two `strh` and no `str`/`strb` through the NOR mapping pointer, and that the inlined `prog` store follows the checks and the AA/55/A0 calls.
5. Byte-check `.rodata`: the command table, and the plan word, stock and target tables against `writer_ref.r1_plan`.
6. Check the syscall numbers and arguments (open 5 with O_RDWR|O_SYNC, mmap2 192 at page 0 with length 0x80000 and MAP_SHARED, setitimer 104, rt_sigprocmask 175 with the SIGALRM hole, clock_gettime 263 with CLOCK_MONOTONIC, write 4, exit 1).
7. Check the gate logic in `scan()`: a foreign image can never be classed as stock, patched or partial; the partial class needs `--resume`.
8. Run `python3 workspace/nor-model/harness/run_harness.py` and `python3 -m unittest` in `workspace/nor-model`, and compare the counts with 9.6.
9. Confirm the stop path: any DQ5, timeout or read-back mismatch leads to F0 and exit 3 with no further program cycle.
10. Confirm the datasheet claims in 9.5 against D1/D2 (same SHA-256), especially the protected-sector, reset-while-busy and 300 µs points.

### 9.9 Hardening after the independent review (2026-11-07, offline)

`docs/NOR_REVIEW.md` gave GO for dcd8297a… and recommended F1-F3. All three are now in `norwrite.c`. The binary changed, so this build needs the delta review described in `NOR_REVIEW.md` ("Delta since the reviewed build") before R1-G3. Nothing here touched the device.

| Finding | Change | Exit |
|---|---|---|
| F1 SA0 protect not enforced | In `run_r1`, next to the SA5 check, after the ID check and the post-autoselect re-hash: refuse unless `(prot0 & 0xff) == 0x01` (the same autoselect read at word 0x002 that is already printed as `protect_sa0`) | **6**, before any program cycle. The autoselect cycles (AA/55/90, F0) are bus writes that happen before this check, as for the SA5 refusal; the protect bit cannot be read without them |
| F2 no single-instance lock | `/tmp/norwrite.lock`, created with O_WRONLY\|O_CREAT\|O_EXCL (0644) and holding the user's pid in decimal, is taken before the first bus access by `--check`, `--reset`, `--write` and `--resume` (`--dry-run`, `--plan-hash` and usage take no lock). A lock whose pid no longer exists (kill(pid, 0) = ESRCH), or holds our own pid, is stale: it is removed once and the create retried. Empty, non-numeric or unreadable content counts as held. Released on every return path through `locked()`, and by the new SIGALRM handler, which removes the lock, prints `STOP: 30 s hard timeout` and exits 3 (before, the default action killed the process). SIGKILL still leaves the file; the next run removes it as stale | **7**, no bus access |
| F3 clock_gettime result ignored | `now_us()` sets `clock_bad` when the syscall fails; `locked()` calls `now_us()` once after taking the lock and refuses if it failed | **4**, no bus access |

Exit codes are now: 0 done or already patched; 2 usage; 3 stopped after bus writes (or the 30 s alarm); 4 refused before any program cycle (now also a failed clock); 5 partial without `--resume`; **6 SA0 not reported protected**; **7 lock held or not creatable**.

`lock_held` is `volatile` because the signal handler reads it. F4 (timer still armed after `platform_unlock`) and F5 are unchanged.

**Binary.** Built from a temporary copy of `norwrite.c` and `norwrite_plan.h` with `build.sh` (same container, `arm-linux-gnueabi-gcc` 12.2.0-14, same flags), twice, and once more after a comment-only edit. All three builds gave the same hash. The same toolchain still rebuilds the reviewed source (commit 48918a2) to dcd8297a…, so the toolchain did not drift.

- `tools/devtools/norwrite`: 26,100 bytes, SHA-256 **8e575d247dc0cc2eda6feaf6d0f0af90ffbc3f946c3fc898c53cc6260499cacf** (replaces dcd8297a…, 24,412 bytes).
- Source: `norwrite.c` SHA-256 45fe498dc84e8b27ce9884addbb2cb86df709b1c5b1a052ff359fec13c75391e (+168/−10 lines against 48918a2); `norwrite_plan.h` unchanged (a6e08398…).
- `Tag_CPU_arch: v4T`. 1,932 disassembly lines (was 1,664). No blx, clz, ldrd/strd, pld, bkpt, ldrex/strex, movw/movt, rev, sxt/uxt, barriers, DSP/saturating multiplies or divide instructions; no `__aeabi` helper. `umull` 3 times (was 2): the extra one is the divide by 10 that formats the pid.
- **Exactly two `strh`**, as before: `0x102a4` in `cmd()` after the table compare, and `0x11570` in `locked` (into which `run_r1` and `prog` are now inlined). The second follows the checks `cur == t` skip, `cur & ~stock`, `t & ~cur`, the range check `w + 0xfffebef5 <= 0x7c1` (w − 0x1410B ≤ 0x7C1), `t & ~stock`, then `bl cmd` with (555,AA), (2AA,55), (555,A0). Its address comes from `nw_plan_word` (0x141d0) shifted left by 1 and its data from `nw_plan_target` (0x1345c). The loop bound is 0x685 = 1,669.
- `.bss` is now addressed through one anchor at 0x16bf8: `clock_bad` +0, `nor` +4, `sigsave` +8, `lock_held` +0x10. Every pc-relative load of the anchor was resolved. Stores through it: `now_us` `strhi` to +0 (`clock_bad = 1`), `platform_map` to +4 (the mmap result, as before), `locked` to +0x10 twice (`lock_held` = 1 and = 0). Every other new store is to the stack: the pid text, and the `act` and `block` arrays built in `platform_lock`. `nor` (+4) is loaded only by `cmd`, `settle`, `scan`, `stop` and `locked` (ID reads, the per-word read, the program store and polling), the same set as before.
- `.rodata`: `cmd_allowed` unchanged, (555,AA)(2AA,55)(555,90)(555,A0)(800,F0). The plan tables are unchanged. The `sigaction` template in `.data` at 0x16be4 is {`on_alarm` 0x10ab4, flags 0, restorer 0, mask all}. The block mask at 0x141a8 is {0xffffdfff, 0xffffffff}.
- New syscalls: getpid 20; open 5 of `/tmp/norwrite.lock` with 0xC1 (O_WRONLY|O_CREAT|O_EXCL) and mode 0644, or read-only; write 4; read 3 (12 bytes); close 6; kill 37 with signal 0; unlink 10; rt_sigaction 174 (SIGALRM 14, sigsetsize 8); exit 1 in the handler with status 3. The `/dev/mem` open, mmap2, setitimer and rt_sigprocmask arguments are unchanged.
- The SA0 check in the binary: `tst fp, #0xff` (SA5 must read 0, else rc 4), then `and sl, sl, #0xff; cmp sl, #1; bne` → `platform_unlock`, the message, rc 6, then the lock drop.

**ARM user-mode emulation in a container without `/dev/mem`** (not on the device): `--dry-run` gives the same plan and plan SHA e6135999…; `--plan-hash` gives the same; `--write yes` gives exit 2. `--check` creates and removes the lock (seen with `qemu-arm -strace`: getpid, open with O_EXCL, write of the pid, close, clock_gettime, open `/dev/mem` fails, unlink, exit 4). A lock held by a live process (`sleep`): `--write` and `--reset` exit 7 and the file is untouched; `--dry-run` still exits 0. A lock of a dead pid: "removing stale", then the run proceeds and the file is removed at exit. An empty lock file or one holding pid 1: exit 7.

**Harness (`run_harness.py`), same seed, before (reviewed source) and after.** Apart from the totals line, every line that existed before is identical, byte for byte, in the new output. The rest are new lines:

| | Before (dcd8297a source) | After |
|---|---|---|
| Writer runs | 8,164 | 8,184 (+20: 3 SA0 runs, 12 lock-held runs, 5 lock/clock runs) |
| Bus writes checked by the monitor | 18,396,568 | 18,411,614 (+15,046: two full clean runs of 6,682, the SA0 refusal's 6 command writes, a cut run of 1,670 and its 6-write refusal) |
| Monitor failures / erase-capable cycles | 0 / 0 | 0 / 0 |
| Distinct intermediate states (snapshots) | 1,613 (2,700) | 1,613 (2,700) |
| Clean transcript replay | 805,877 reads, 6,682 writes, 87dc81ca… | identical |
| Fuzz | 8 transcripts, 103,015 bus ops | identical |
| Harness failures | 0 | 0 |
| `python3 -m unittest` | 29 OK | 29 OK |

New harness cases: SA0 not protected on a stock chip → exit 6, 0 program cycles, 6 command writes, chip stock, read mode. A partial state (process killed mid-program), then SA0 not protected with `--resume` → exit 6, 0 program cycles, chip unchanged. Lock held by a live copy, empty, or garbage, for each of `--write`, `--resume`, `--check`, `--reset` → 12 of 12 exit 7 with 0 bus operations and the lock file unchanged; `--dry-run` still exits 0. Stale lock of a dead pid → exit 0, target reached, lock removed. Lock holding our own pid → exit 0. Lock not creatable → exit 7, 0 bus operations. `clock_gettime` failure → exit 4, 0 bus operations, lock removed. Every run that returns checks that it did not leave its own lock. Every cut or kill leaves its lock behind, and the next run removed it as stale 1,902 times (1,898 cuts and kills in section 3, 1 in section 10, 1 in the SA0 partial case, and the 2 explicit stale cases).

**Reviewer's adversarial tests (`adv.c`, unchanged, own chip model).** T0 and T2a-T8 give the same results as in the review. T1 and T2b now report UNSAFE. That is because their chip variants report SA0 as unprotected: T1's `prot_reports = 0` reports 0 for every sector, and T2b clears SA0 protection. The writer now refuses both with exit 6, chip stock and 0 program cycles, before the scenario the test sets up. With a scratch copy that keeps SA0 reported in T1 and asserts the refusal in T2b, T1 is again SAFE (exit 3 after 1 program cycle, chip stock, read mode). T2b' is SAFE: `--write` and `--resume` exit 6 with 0 program cycles, and kills at 40 points never reach program setup. 0 adversarial failures.

Residuals of the new code [H unless tested]:

- Stale-lock race: if a run is killed, then two new copies start within microseconds of each other, both can read the dead pid. One removes the file and creates its own; the other then removes that new file. Both would run. Not tested. The procedural rule stays: one copy, check `ps` first.
- A copy killed between creating the lock and writing its pid leaves an empty file. Every later run refuses with exit 7 until the file is removed by hand (fail closed). A reused pid that belongs to an unrelated live process does the same.
- The lock only excludes other `norwrite` copies that use the same `/tmp`. `norid2` and `devmem` are not excluded. `/tmp` is RAM on the unit, so the lock does not survive a reboot.
- `--check` now writes and removes a file in `/tmp`, and refuses while another copy holds the lock.
- The SIGALRM handler does no bus access. It is not exercised by the harness or qemu (it needs 30 s inside the bus phase); it is checked from the disassembly only.

Source diff against the reviewed source (`git diff -U1 48918a2 -- tools/devtools/norwrite.c`; the full-context diff is reproducible with the same command without `-U1`):

```diff
--- a/tools/devtools/norwrite.c
+++ b/tools/devtools/norwrite.c
@@ -27,7 +27,9 @@
  *
- * Order: plan self-check; whole-chip SHA-256 gate; F0, settle, F0, settle (clears a half-sent command sequence
- * left by a killed run); autoselect ID and SA5 protect check, F0, whole-chip re-hash; per word: skip if done, else program with toggle-bit (DQ6) polling, DQ5 handling and a timeout,
+ * Order: plan self-check; single-instance lock /tmp/norwrite.lock and a clock check (every mode with bus access);
+ * whole-chip SHA-256 gate; F0, settle, F0, settle (clears a half-sent command sequence
+ * left by a killed run); autoselect ID, SA5 and SA0 protect check, F0, whole-chip re-hash; per word: skip if done, else program with toggle-bit (DQ6) polling, DQ5 handling and a timeout,
  * then read back; final whole-chip SHA-256 must equal the R1 target. Any failure sends F0 and stops without
  * retrying. Exit codes: 0 done or already patched, 2 usage, 3 stopped after bus writes (keep the unit powered,
- * do not reboot, dump and analyse), 4 refused before any program cycle, 5 partial R1 state without --resume. */
+ * do not reboot, dump and analyse), 4 refused before any program cycle, 5 partial R1 state without --resume,
+ * 6 SA0 not protected (refused before any program cycle), 7 another copy holds the lock (no bus access). */
 typedef unsigned int u32;
@@ -44,2 +46,3 @@ typedef unsigned char u8;
 #define CONFIRM "R1-BLACK-LOGO"
+#define LOCK_PATH "/tmp/norwrite.lock"
 
@@ -50,2 +53,4 @@ typedef unsigned char u8;
 #define EX_PARTIAL 5
+#define EX_SA0 6
+#define EX_LOCKED 7
 
@@ -56,3 +61,6 @@ typedef unsigned char u8;
 
-/* ---- platform: bus, timer, output ------------------------------------------------------------------- */
+static volatile int lock_held;	/* also read by the SIGALRM handler */
+static int clock_bad;
+
+/* ---- platform: bus, timer, output, lock file ------------------------------------------------------- */
 #ifdef NORWRITE_HOST
@@ -62,2 +70,29 @@ u32 nw_host_now_us(void);
 void nw_host_out(const char *s, long n);
+long (*nw_host_lock)(int op, u32 pid, char *buf, long n);	/* harness lock file and pid table; NULL: always free */
+
+static u32 lk_self(void)
+{
+	return nw_host_lock ? (u32)nw_host_lock(0, 0, 0, 0) : 1;
+}
+
+static long lk_create(const char *s, long n)
+{
+	return nw_host_lock ? nw_host_lock(1, 0, (char *)s, n) : 0;
+}
+
+static long lk_read(char *b, long n)
+{
+	return nw_host_lock ? nw_host_lock(2, 0, b, n) : -2;
+}
+
+static int lk_alive(u32 pid)
+{
+	return nw_host_lock ? (int)nw_host_lock(3, pid, 0, 0) : 1;
+}
+
+static void lk_unlink(void)
+{
+	if (nw_host_lock)
+		nw_host_lock(4, 0, 0, 0);
+}
 
@@ -137,3 +172,4 @@ static u32 now_us(void)
 
-	sc(263, 1, (long)ts, 0, 0, 0, 0);	/* clock_gettime(CLOCK_MONOTONIC) */
+	if (bad(sc(263, 1, (long)ts, 0, 0, 0, 0)))	/* clock_gettime(CLOCK_MONOTONIC) */
+		clock_bad = 1;
 	return (u32)ts[0] * 1000000u + (u32)ts[1] / 1000u;
@@ -146,2 +182,53 @@ static void out(const char *s, long n)
 
+static u32 lk_self(void)
+{
+	return (u32)sc(20, 0, 0, 0, 0, 0, 0);	/* getpid */
+}
+
+static long lk_create(const char *s, long n)
+{
+	long fd = sc(5, (long)LOCK_PATH, 01 | 0100 | 0200, 0644, 0, 0, 0);	/* O_WRONLY | O_CREAT | O_EXCL */
+	long r;
+
+	if (bad(fd))
+		return fd;
+	r = sc(4, fd, (long)s, n, 0, 0, 0);
+	sc(6, fd, 0, 0, 0, 0, 0);
+	if (r != n) {
+		sc(10, (long)LOCK_PATH, 0, 0, 0, 0, 0);
+		return -5;
+	}
+	return 0;
+}
+
+static long lk_read(char *b, long n)
+{
+	long fd = sc(5, (long)LOCK_PATH, 0, 0, 0, 0, 0), r;
+
+	if (bad(fd))
+		return fd;
+	r = sc(3, fd, (long)b, n, 0, 0, 0);
+	sc(6, fd, 0, 0, 0, 0, 0);
+	return r;
+}
+
+static int lk_alive(u32 pid)
+{
+	return sc(37, (long)pid, 0, 0, 0, 0, 0) != -3;	/* kill(pid, 0): only ESRCH means gone */
+}
+
+static void lk_unlink(void)
+{
+	sc(10, (long)LOCK_PATH, 0, 0, 0, 0, 0);
+}
+
+static void on_alarm(int sig)	/* the 30 s hard limit: drop the lock, then terminate as the default action did */
+{
+	(void)sig;
+	if (lock_held)
+		lk_unlink();
+	out("STOP: 30 s hard timeout\n", 24);
+	sc(1, EX_FAIL, 0, 0, 0, 0, 0);
+}
+
 static int platform_map(int writable)
@@ -164,4 +251,6 @@ static void platform_lock(void)
 	u32 block[2] = { ~(1u << 13), 0xffffffffu };	/* everything but SIGALRM (14) */
+	u32 act[5] = { (u32)on_alarm, 0, 0, 0xffffffffu, 0xffffffffu };	/* handler, flags, restorer, mask */
 
-	sc(104, 0, (long)it, 0, 0, 0, 0);	/* setitimer(ITIMER_REAL): the default SIGALRM action terminates */
+	sc(174, 14, (long)act, 0, 8, 0, 0);	/* rt_sigaction(SIGALRM) */
+	sc(104, 0, (long)it, 0, 0, 0, 0);	/* setitimer(ITIMER_REAL): SIGALRM ends the run through on_alarm */
 	sc(175, 0, (long)block, (long)sigsave, 8, 0, 0);	/* rt_sigprocmask(SIG_BLOCK) */
@@ -596,2 +685,7 @@ static int run_r1(int resume)
 	}
+	if ((prot0 & 0xff) != 0x01) {
+		platform_unlock();
+		put("SA0 does not report sector protection (the reset word 0x800 relies on it): refusing, array verified unchanged\n");
+		return EX_SA0;
+	}
 
@@ -672,2 +766,66 @@ static int check(int reset)
 
+/* ---- single-instance lock: taken before the first bus access, dropped on every return -------------------- */
+/* The file holds the user's pid in decimal. A pid that no longer exists (or is our own) marks a stale lock
+ * left by a killed run; it is removed once and the create retried. Unreadable or empty content counts as held. */
+static int lock_take(void)
+{
+	char me[12], b[12];
+	u32 self = lk_self(), v = self, pid;
+	long n = sizeof(me), r, k;
+	int tries;
+
+	do
+		me[--n] = (char)('0' + v % 10u);
+	while ((v /= 10u) && n);
+	for (tries = 0; tries < 2; tries++) {
+		r = lk_create(me + n, (long)sizeof(me) - n);
+		if (!r) {
+			lock_held = 1;
+			return 0;
+		}
+		if (r != -17)	/* EEXIST */
+			break;
+		r = lk_read(b, sizeof(b));
+		for (pid = 0, k = 0; k < r && b[k] >= '0' && b[k] <= '9'; k++)
+			pid = pid * 10u + (u32)(b[k] - '0');
+		if (r <= 0 || k != r || !pid || (pid != self && lk_alive(pid))) {
+			put("another norwrite holds " LOCK_PATH " (pid ");
+			dec(pid);
+			put(", 0 = unreadable): refusing, no bus access\n");
+			return EX_LOCKED;
+		}
+		put("removing stale " LOCK_PATH " of pid ");
+		dec(pid);
+		put("\n");
+		lk_unlink();
+	}
+	put("cannot create " LOCK_PATH ": refusing, no bus access\n");
+	return EX_LOCKED;
+}
+
+static void lock_drop(void)
+{
+	if (lock_held) {
+		lock_held = 0;
+		lk_unlink();
+	}
+}
+
+static int locked(int write, int arg)	/* every mode with bus access runs inside this */
+{
+	int rc = lock_take();
+
+	if (rc)
+		return rc;
+	now_us();
+	if (clock_bad) {
+		put("clock_gettime failed: refusing, no bus access\n");
+		rc = EX_REFUSED;
+	} else {
+		rc = write ? run_r1(arg) : check(arg);
+	}
+	lock_drop();
+	return rc;
+}
+
 static void dry_run(const u8 *plan_sha)
@@ -724,9 +882,9 @@ int norwrite_main(int argc, char **argv)
 	if (argc == 2 && same(argv[1], "--check"))
-		return check(0);
+		return locked(0, 0);
 	if (argc == 2 && same(argv[1], "--reset"))
-		return check(1);
+		return locked(0, 1);
 	if (argc == 3 && same(argv[1], "--write") && same(argv[2], CONFIRM))
-		return run_r1(0);
+		return locked(1, 0);
 	if (argc == 3 && same(argv[1], "--resume") && same(argv[2], CONFIRM))
-		return run_r1(1);
+		return locked(1, 1);
 	put("usage: norwrite --dry-run | --plan-hash | --check | --reset | --write " CONFIRM " | --resume " CONFIRM "\n");
```

## 10. R2/R3 sector-erase analysis (host only)

Nothing here is built for the device. The erase code is `norwrite_exp_erase()` and `norwrite_exp_program()` in the `NORWRITE_ERASE_EXPERIMENT` block of `norwrite.c`; the harness compiles it as `harness-erase` with every sector allowed so that each can be analysed. It sends one sector per command (AA, 55, 80, AA, 55, 30@sector base) with the toggle algorithm and a 12 s timeout (datasheet maximum 10 s), and checks the sector reads all 0xFF afterwards. A device erase writer would need its own allowlist per stage (N4: {10}; R2: {6}; R3: {6, 5}) and its own R1-G0..G2-style gates.

### 10.1 What each sector holds and what an interrupted erase leaves

Harness: a power cut at 10 %, 50 % and 90 % of the typical erase time for each sector, then the damaged regions compared with d48b41. The model leaves every word of an interrupted sector random. A real chip preprograms to 00h and then erases, so a real interruption leaves some mix of the old data, 0x0000 and partly erased words [H]. Either way, the whole sector must be treated as unusable.

| Sector | Range | Contents | Interrupted erase damages (model) | Consequence | Recovery |
|---|---|---|---|---|---|
| SA0 | 0x00000-0x03FFF | Vector table, device info | Nothing: protected [M], the erase toggles about 100 µs and returns [V]; the harness's erase reports "not erased" | None | - |
| SA1, SA2 | 0x04000-0x07FFF | 0xFF | 0xFF area only | Probably none [H: nothing known reads it] | Linux, by erasing again |
| SA3 | 0x08000-0x0FFFF | Stage 1 (0x8000-0xB293) and the gzip start | Stage 1 and gzip | Brick | J or P only |
| SA4 | 0x10000-0x1FFFF | gzip stream | gzip | Brick | J or P only |
| SA5 | 0x20000-0x2FFFF | gzip tail to 0x247DC, palette, records 0, 218, start of 219 | gzip tail, palette, records | Brick | J or P only |
| SA6 | 0x30000-0x3FFFF | Rest of the record chain (end of 219 to the last record, id 221) to 0x3ED8B, version string at 0x3FFF0 | Records, version string | Images corrupt; the boot path that draws only id 0 may survive (section 4) [H] | Linux, if it still boots: erase SA6 again and program it |
| SA7, SA9, SA10 | 0x40000-0x4FFFF, 0x60000-0x7FFFF | ngffs header (0x2A bytes) | Header | ngffs damaged for the stock firmware [H]; 6.1 does not mount ngffs | Linux: erase again and program the 21 header words (N4 test) |
| SA8 | 0x50000-0x5FFFF | ngffs `settings.dat` | Settings | Stock firmware settings lost [H] | Linux: erase and program from the backup |

### 10.2 R3 sequence under power cuts

Sequence as in section 5: erase SA6, program its 30,413 words; erase SA5, program the 9,198-word gzip tail (bytes 0x20000-0x247DC) first, then the remaining SA5 words, 55,997 steps in all. With erase scaled to 50 ms in the model so the sweep stays fast, the full sequence gives `mtd0-homtom.bin` exactly (1,011,538 bus ops). Of 1,000 power cuts placed uniformly over the bus ops, 215 fell inside the window from the start of the SA5 erase to the end of the gzip-tail program (ops 539,942-760,388, 21.8 % of the scaled run), and all 215 left the boot chain (bytes 0-0x247DC) damaged. The other 785 left it intact; **no cut outside that window damaged it**. On the real chip the window is the SA5 erase (0.5 s typical, 10 s maximum) plus 9,198 programs (about 74 ms typical, 2.8 s maximum) [V timings].

R2 (SA6 only) never touches SA0-SA5: in 10.1 a cut during an SA6 erase damages only SA6, so in the model no R2 power cut can damage the boot chain. What remains is the [H] question of whether the bootloader still boots and draws id 0 with a corrupt SA6.

### 10.3 Gates for erase stages

| Stage | Gates |
|---|---|
| N4 (erase SA10, program its 21 header words) | R1 has run cleanly on the device, or the user explicitly accepts going first with N4. A separate device erase writer with sector allowlist {10}, its own bus-monitor harness run (the monitor here would have to accept exactly one 80/30 pair aimed at word 0x38000), an independent review like R1-G2, the same-boot preconditions as R1-G3, and approval. Go if the whole-chip SHA is d48b41 again and the unit stays responsive |
| R2 (SA6) | N4 passed. Static check of the image lookup and its end-of-chain handling (section 4 [H]); ideally N3. Allowlist {6}; program data compiled in with its SHA checked at start; approval. Go if the expected image SHA matches and a boot test passes |
| R3 (SA6, then SA5) | **N3 required**: an external JTAG or programmer read that matches d48b41. Allowlist {6, 5}; SA5 last; gzip tail first after the SA5 erase; the unit on external power as well as battery; approval that accepts the 10.2 window. Go if the whole-chip SHA equals `mtd0-homtom.bin` (be6170de…) |

### 9.10 R1 device run: approval and log (2026-11-07)
- R1-G4: the user gave explicit approval ("lets do NOR") after the risk was stated: no external recovery route exists yet; the realistic worst case is a garbled logo image; undoing R1 needs an SA5 erase.
- R1-G3, measured in the same boot as the run: the uploaded `norwrite` fetched back over FTP hashes 8e575d24…acacf (matches the delta-reviewed build); `/proc/iomem` has nothing below 0x30000000; no `/proc/mtd` or `/dev/mtd*`; no other `norwrite` process and no `/tmp/norwrite.lock`; `norwrite --plan-hash` on the device prints e6135999…8d1e; `norwrite --check` reports `sha256 d48b41abc85c…128ead5` and `state stock`.

R1 result (2026-11-07): `norwrite --write R1-BLACK-LOGO` programmed 1669 words in 1219 ms; the writer's own whole-chip hash and a separately fetched `norid2 dump` hashed on the Mac both equal 87dc81ca967579b424e3e48d1e680685549ee0d73d433d2cfd00c5c4611a384e. A byte comparison with the stock backup shows 2257 changed bytes, all inside 0x28211-0x2919A, only bits cleared, everything else identical. Sector 0 read protected, SA5 unprotected, ID 7F/1C/22BA as before. R1-G5: user cold boot: the screen is black where the hand logo was, then the storage window (normal TTBL behaviour after a reboot; the Mac must eject), then the homtom splash, a brief burst of coloured noise before the console (kernel taking over the display, not the NOR), then the UI. No boot or storage-mode regression. The NOR now differs from the stock backup; the stock image is kept in artifacts/backups and the restore path is an SA5 erase with an external programmer (not done). Remaining NOR steps (R2/R3 homtom logo via sector erase) stay gated on an external read/write path.

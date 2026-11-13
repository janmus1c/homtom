# Storage-mode graphics without a NOR write (2026-11-07)

Question: can homtom images replace the bootloader's storage-mode graphics by adding records to `ttsystem` that load a replacement image table into RAM and redirect the bootloader's table pointer, with no NOR write?

**Answer (corrected after the device test): no for the storage-mode screens; yes for screens drawn after the load.** The record mechanism works: data-only records land, the pointer is read at every draw, and the bootloader accepts the extra records (stage 0 and 1 booted). But when the unit reboots with the Mac attached, storage mode starts **before `ttsystem` is read**. The bootloader opens a 1000-tick USB window right after the hand logo, and the Mac's bus reset arrives inside it (section 0). So no `ttsystem` record can reach the storage screens in that flow, which is the only practical one. A ttsystem-borne table is drawn only after the load; stage 1b demonstrates that. Changing the storage screens needs another route (section 0.4).

Grades: **[V]** = read from `workspace/bootgfx/bootloader.dis` (llvm-objdump of `artifacts/nor-analysis/bootloader.out` at 0x30B00000, cited as dis:LINE), from a repo artifact, or measured by a tool in this repo. **[H]** = hypothesis. **[M]** = earlier device measurement by this project (cited).

Mockups of every screen are not published, because the renders contain stock TomTom artwork. Tools: `workspace/storagegfx/` (drafts; see section 9).

## 0. Device result and re-analysis (2026-11-07 evening)

**Result [M], reported by the coordinator.** `sgp-stage0` and `sgp-stage1` (ladder-p base plus the 3 records, checked "well-formed and signed") both boot normally. After an INFORM0=5 reboot with the Mac attached, storage mode appeared about 4 s after the reset. It showed the stock screens: 0x00 and 0x42 alternating, and in stage 1 the stock label "TomTom GO 630" with the plain "6", not the blue block. The unit is back on ladder-p, marked good.

### 0.1 Is the pointer or palette re-initialised? No.
- The pointer 0x30008430 has exactly one writer, 0x30b0abb0 (dis:11036-11038), with one caller, main at 0x30b00180 (dis:144). The palette cache 0x30009400 has one writer, 0x30b067c8 (dis:6690-6693), with one caller, main at 0x30b00178 (dis:142). Main is called only by the reset stub (dis:26979) [V].
- No other literal base reaches 0x30008430 or 0x30009400 through an immediate offset (scan of every literal within 4 KiB below both addresses: the nearby bases 0x300083dc-0x300083f0 belong to other structures, and none is used to store at +0x40..+0x54 into the pointer) [V, scan; H that no computed address hits it].
- The only lookup reads the pointer itself at every call (dis:11063-11069). There is no copy, no cached base and no per-call table argument [V].
- The INFORM0=5 "reboot" is a full SoC reset (SWRST), so steps 1-5 below run again: `.data` is re-copied, which restores the stock pointer, and the palette is re-copied from NOR. That happens before any file is read, so it cannot undo a later record. It explains why every boot starts stock, not the observation.

### 0.2 When storage mode starts: at the first host check, before the load [V]
Main, after the hand logo and the 40- and 1000-tick delays (dis:150-180):

| Address | Call | What it does |
|---|---|---|
| 0x30b00214 | 0x30b14b90 | UDC software set-up and mass-storage callbacks (dis:21215-21251, 21055-21091) |
| 0x30b00218 | 0x30b0b7bc | USB state reset: flags cleared, 0x30b0b598 drives the USB GPIOs (the 500-tick disconnect pulse) (dis:11807-11829) |
| 0x30b0021c-0x30b00220 | 0x30b0b818, 0x30b0b828 | Enable flags, then the connect state machine 0x30b0b664: with the USB-detect GPIO set it connects (0x30b11fe0) and starts a 2000-tick timer (dis:11773-11789) |
| **0x30b00224** | **0x30b0b838** | **The window: counter [0x30b0b894] = 1000, loop calling 0x30b0ab64 (USB state machine, scene step) until it runs out** (dis:11838-11850) |
| 0x30b0022c | 0x30b14bf0 | First host check, then storage mode 0x30b164c0 (dis:187-189) |
| 0x30b00340 | 0x30b000e4 | Only now: scene 0x45 and the `ttsystem` load |

- The flag that check reads, [0x30b0b874 struct + 0xc], is set by 0x30b0b620. The HS-UDC interrupt handler calls it on event 3, **USB bus reset** (dis:22142-22146), or on status bit 14 (event 4, dis:22121-22123). It does not wait for the host to configure the device.
- A host already attached resets the bus within a fraction of a second after the connect, inside the 1000-tick window. The first check therefore succeeds, and storage mode starts before `ttsystem` is opened. About 4 s from reset fits: stage-1 gunzip, init, logo, 40 + 1000 + 1000 ticks. This corrects section 1 step 9 and the earlier reading of `LESSONS.md:61` ("window while ttsystem loads"): the window is before the load.
- After eject, main loops back to 0x30b00238 and loads `ttsystem`. That load is when the records land, followed by the splash and the kernel. No table draw follows on that path (0x45 is drawn before the load, the splash is 24-bit) [V].
- The second check (0x30b0034c) can only fire if a bus reset arrives during the load. The load does not poll USB (no 0x30b0ab64 call in 0x30b0bb80/0x30b07ddc), and the connect state machine only advances when polled. With the cable unplugged at boot there is no connect at all, so plugging in during the splash cannot trigger it [V for the code; H that the IRQ stays live during the load with the cable already in]. **The "plug in during the splash" experiment would boot Linux, not reach storage mode.**

### 0.3 Is `ttsystem` read lazily? No, but it is read after storage mode
The load is eager: all records are copied and verified inside 0x30b0bb80 before main continues (dis:12048-12206). It simply runs after storage mode in this flow.

### 0.4 What id 263 shows, and what the user sees
- Idle scene 97 draws ids 218 and 219 (`scenes.txt`). After every frame the engine calls 0x30b1a838 (dis:6841). For model digit '6' (0x36), that sets r4 = 0x107 (dis:27278-27280, literal 0x30b1aa48 = 0x107) and draws id 230 ("TomTom GO 730") at (337,8), then id 263 at (443,8) (dis:27311-27319). The user sees 218 + 219 + 230 + 263.
- The device itself confirms it: in the NOR, the only "6" glyph is id 263, and 230 reads "730". So the "6" the user saw came from the **stock** table, and the pointer was stock at that draw, consistent with 0.2 [V].

### 0.5 Consequences
- **No `ttsystem` record can change the storage-mode screens** in the reboot-with-host flow. That also rules out a "re-init defeating" record: nothing re-initialises after the load. The screens are simply drawn before it, from NOR and the NOR palette copy.
- **Silver lining for safety.** Because storage mode precedes the `ttsystem` read, a `ttsystem` whose graphics records break the after-load draw (or anything after the load) can always be undone. Reset with the pinhole, keep the Mac attached and unlocked: storage mode appears before the bad image is read, and `ttsystem.prev` is copied back. Section 4's worry about hangs inside storage mode does not apply to this flow.
- **Routes that can change the storage screens:**
  1. NOR R2/R3 (`docs/NOR_PLAN.md` section 5). The writer exists and R1 ran. R2 (SA6 only) covers 220, 221, 222-239 and 263, but not 218 or 219: those sit in SA5 with the gzip tail, which is R3, needing an erase of a boot sector.
  2. A Linux "reboot to storage" that does not reset: load a RAM copy of the bootloader with its table pointer patched and jump to it (route C). This affects only tool-driven storage entries; cold boots stay stock. It is not built, and it needs a careful hand-over (caches, MMU, IRQs, DMA off) [H].
  3. A data record that sets the host flag, so that storage mode is entered after the load. Rejected: every load would set it again, so the unit could never boot Linux without replacing `ttsystem` in storage mode.

### 0.6 Next discriminating experiment: stage 1b (`ttsystem.sgp-stage1b`)
Goal: a **large** visible difference that only a draw after the load can produce, proving the redirect end to end.

- Records: ladder-p's two records unchanged; then the table at 0x30F00000 (the homtom candidate table, with id 0 replaced by the full-screen 480x272 homtom base art), its palette at 0x30009400, the pointer, and **an 11-byte data record at 0x30B05F78 that turns the 8.3 name "SPLASHW BMP" into "NOSPLASHBMP"**.
- How it works [V]: the splash loader takes the name from the bootloader's RAM image as data (`add r0, pc, #548` at 0x30b05d4c → 0x30b05f78). With the name changed, the lookup fails and main draws id 0, centred, through the redirected table (dis:128-132, 56-68). This is a data read, so no instruction-cache problem arises; patching code this way would not be safe.
- Emulated: id 0 (480x272) is drawn by the bootloader's own code at the centred position (0,0), all 130,560 pixels match, no stray writes. All other ids also match.
- Verified: `ttbl.py verify` reports 6 records, signatures OK, no overlaps (the name range is whitelisted on purpose).
- What the user sees: at power-on, black (R1) and the stock 0x45 start screen as before. Then, **instead of the homtom splash, the full-screen paletted homtom screen** (mark, wordmark, faint watermark), then Linux. The quiet-boot kernel adopts that frame. In the reboot-with-Mac flow: stock storage screens (unchanged, as predicted), then after eject the same full-screen homtom screen. If the redirect failed, the normal splash would appear, because the name record then never lands either; if only the pointer failed, black would replace the splash.
- Safety: as stage 0/1. The new draw is fully emulated, and recovery is the pinhole reset with the Mac attached: storage mode comes before the image is read (0.5).
- File: `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sgp-stage1b`, 4,456,083 bytes, SHA-256 `e22e9fb03f6c0cb61cf489da7d6afe4b6a462c2b10480e49d21f74babcc359ae` (`.sha256` next to it). Base `ttsystem.ladder-p` (6da6e2a2…282b).
- Steps for the user: (1) `tools/homtom-to-storage`, then `tools/homtom-stage artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sgp-stage1b`. (2) `tools/homtom-to-linux`, and watch the screen after the start screen: the full-screen homtom image instead of the splash = **pass**; the normal splash = records not applied; black = name applied but table not. (3) For a cold-boot check, power off and on without USB and watch the same moment. (4) To undo: `tools/homtom-to-storage` (storage mode precedes the load), `tools/homtom-stage` ladder-p (or copy `ttsystem.prev`), `tools/homtom-to-linux`. If the unit hangs on the image: pinhole reset with the Mac attached and unlocked, then the same undo.
- The timing variant the coordinator suggested (USB unplugged at reboot, plugged in during the splash) is **not** useful: per 0.2 it boots Linux without storage mode. A safe, cost-free check of 0.2 with the stock image: run `tools/homtom-to-storage` and watch whether the homtom splash appears **before** the storage screen. Expected per 0.2: black, then storage directly, with no 0x45 and no splash. After eject: 0x45, then the splash, then Linux.

**Go/no-go:** stage 1b is **GO** (the stages were approved; the pinhole reset exists; the recovery path is stronger than first assessed). Stage 2 as planned (homtom storage screens through `ttsystem`) is **NO-GO**: it cannot work in this flow. The storage screens need NOR R2/R3 or route C.

## 1. Order of events from power-on (step 9 corrected in 0.2)

Main is 0x30b0016c, called from the reset stub 0x30b1a4c0 (dis:26976-26980).

| # | Event | Address | Evidence |
|---|---|---|---|
| 1 | Mode stacks set (SVC sp 0x303f5800, others to 0x303f8000) | 0x30b1a644 | [V] dis:27073-27096 |
| 2 | `.data` copied 0x30b353a4 → 0x30008000 (0xa28 bytes), `.bss` 0x30008a28-0x3016b76c zeroed. **This resets the table pointer to 0x28201 on every reset** | 0x30b1a5f8 | [V] dis:27054-27072 |
| 3 | Hardware init: MMU (flat map, page table 0x303f8000), heap 0x30880000-0x30afffff, power decision from INFORM0 (5 or 9 = "Reboot", boot on; otherwise "Power down", wait for the power key) | 0x30b0ac4c | [V] dis:11075-11160, 3549-3600; INFORM0 branch [M] `LESSONS.md:41` |
| 4 | NOR palette 0x28000 copied to RAM 0x30009400 (512 bytes). **Draws use this copy** | 0x30b00178 → 0x30b067c8 | [V] dis:141-142, 6690-6693 |
| 5 | Table pointer [0x30008430] = 0x28201 (only writer) | 0x30b00180 → 0x30b0abb0 | [V] dis:143-144, 11036-11038 |
| 6 | Hand logo: clear, draw id 0 (id 1 if a board GPIO is set; id 1 is absent) | 0x30b00194 → 0x30b00020 | [V] dis:56-68, 149 |
| 7 | Delay 40, board checks, text line, **delay 1000**, more init | 0x30b00198-0x30b00210 | [V] dis:150-180 |
| 8 | USB device init for the current USB mode | 0x30b00214 → 0x30b14b90 | [V] dis:181, 21268-21281 |
| 9 | USB connect and a 1000-tick polled window (0x30b0b7bc, 0x30b0b818, 0x30b0b828, 0x30b0b838), then the first host check. **An attached host's bus reset lands in the window, so storage mode starts here, before the load** | 0x30b00218-0x30b0022c | [V] dis:182-189, 11807-11850, 21292-21328 (corrected; see 0.2) |
| 10 | Probe drives, then for drive 0: scene **0x45** (0x85 for drive 1) drawn **with the stock table**, mount, load `ttsystem` | 0x30b00340 → 0x30b000e4 | [V] dis:232-256, 105-125 |
| 11 | Load: structural pass over all records, then per record read straight to its load address while hashing, then verify the signature; stop at the first bad one | 0x30b0bf74 → 0x30b0bb80 | [V] dis:12048-12206 |
| 12 | `SPLASHW.BMP` drawn (24-bit, no table); if it fails, id 0 is drawn again **through the redirected table** | 0x30b00148 | [V] dis:128-132 |
| 13 | Second host check. With a host present: storage mode, scene **0x00** drawn **through the redirected table** | 0x30b0034c → 0x30b164c0 | [V] dis:259-261, 22880-22911 |
| 14 | In storage mode: busy scenes 0x41/0x42 etc. on host access, idle 0x00 after 5000 ticks | 0x30b07714, 0x30b077cc | [V] dis:7669-7757 |
| 15 | Host ejects: back to step 10. The pointer survives (no reset), so this time **0x45 is drawn from the new table**; records are reloaded | 0x30b00238 loop | [V] dis:190-309 |
| 16 | No host: kernel boot (ATAGs, a 500-tick USB disconnect pulse, jump to the terminator's entry 0x31700000) | 0x30b0035c → 0x30b0bff8 | [V] dis:262-263, 12334-12342 |

**Are all records in RAM before the storage window and the first storage scene?** **No** (corrected). In the reboot-with-host flow, storage mode starts at step 9, before the load (0.2). Records are in RAM only for draws after step 11: the id-0 splash fallback, the 0x45 screen on a later loop iteration, and a storage entry at step 13, which needs a bus reset during the load. The first version of this document assumed the opposite.

**INFORM0=5 reboot vs normal power-on.** Only step 3 differs. With 5 (or 9) the bootloader boots on; otherwise it waits for the power key, which is already held at a manual power-on [M] `LESSONS.md:41`. Steps 4-16 are the same code path in both cases [V]. The storage window is the same too: it needs a configured USB host at step 13.

## 2. How an image id is resolved

- Every draw goes through 0x30b06844(id, x, y) [V]. Its only lookup is 0x30b0ac1c (sole caller, dis:6725). That function reads **[0x30008430] at every call** (dis:11063-11069): the pointer is not cached, so a redirect takes effect at the next draw. A pointer of 0 makes every lookup fail and nothing is drawn (dis:11066-11070).
- Lookup 0x30b0abbc walks the chain linearly from the pointer: big-endian `len` (byte-wise read 0x30b22788, so odd addresses are fine), `id`, then `next = here + 8 + len`. It stops at `len == 0` (not found, nothing drawn) or at a matching id (dis:11039-11062). **There are no bounds checks**: no table end, no maximum `len`, no id range.
- The draw reads big-endian width and height and RLE-decodes `w*h` bytes into the buffer whose address is in [0x300082dc] (initial value 0x30600000), with no size check (dis:6728-6759). It then plots each pixel through the function pointer at [0x30009398], with colour = RGB565 entry from the **RAM palette 0x30009400** (dis:6767-6779).
- What is cached: the palette, copied once at step 4. What is not: the pointer, the records and the pixel data, all read at each draw.
- Scenes: selector 0x30b06ad8(state, drives) gives mode = state | drives<<6 and group = model (16 here) (dis:6852-6891). The scene table (bootloader image offset 0x28d94, 0x140 bytes per scene) gives ids, positions and delays. After each frame the engine draws the model label: id 230 at (337,8) plus id 263 at (443,8), which makes "TomTom GO 630" (dis:6841, 27308-27319) [V; which switch case this unit takes is H, strong, and the rendered stock frames reproduce the label].

## 3. RAM: what is used, what is free, chosen address

Bootloader RAM use [V unless noted]:

| Range | Use | Evidence |
|---|---|---|
| 0x30000000-0x30007fff | unknown; avoid | - |
| 0x30008000-0x30008a27 | `.data` (holds the pointer 0x30008430) | dis:27069-27071 |
| 0x30008a28-0x3016b76b | `.bss` (holds the palette 0x30009400, the scene state 0x300082dc) | dis:27063-27072 |
| below 0x303f5800 | SVC stack, grows down (the loader alone uses a 4.3 KiB frame) | dis:27096, 12050 |
| 0x303f5800-0x303fffff | mode stacks, MMU table 0x303f8000, IRQ vector words 0x303fff04-0x303fff20 | dis:27045-27052, 27092-27096 |
| 0x30400000-0x305fffff | frame buffers (uncached sections; the splash is scanned out of 0x30400000) | dis:3549-3600; `LESSONS.md:123` |
| 0x30600000-0x3061fdff | RLE decode buffer (480x272 max) | `.data` 0x300082dc |
| 0x30680000-0x306fffff | buffer pointed to by 0x30008130 (size not traced) | `.data` |
| 0x30700000-0x3087ffff | uncached DMA/USB buffers | dis:3549-3600, 2381-2382 |
| 0x30880000-0x30afffff | heap (also stage-1 gunzip work area) | dis:11081-11083 |
| 0x30b00000-0x30b35dcb | bootloader | - |
| 0x31000000-0x311357df | initramfs record in `ttsystem.ladder-q` | parser output |
| 0x31700000-0x319fba37 | kernel record | parser output |

No address in 0x30b35dcc-0x30ffffff appears in the bootloader's code or literal pools. A scan of every word finds such values only in its data area (sound, scene table, Blowfish S-boxes, from 0x30b22b10 on), where they are table data, not pointers [V]. No dynamic use is known [H]. **Chosen table address: 0x30F00000** (pointer value 0x30F00201). That leaves 1 MiB of headroom below the initramfs, 3.9 MiB above the bootloader, and no overlap with any range above. Alternative if needed: 0x31400000, between the initramfs end and the kernel.

Record format and constraints [V, dis:12048-12206; `tools/ttimage/mkttimage.c`]:

- File: `TTBL`, then records `u32 len, u32 addr, data[len], sig[16]`, then a terminator `0, entry, param`. If at least 8 more bytes follow, a `0` word must be followed by `1`, or the file is rejected (dis:12095-12113). Our images have no trailer.
- Pass 1 seeks over the records with no address or length checks. Pass 2 reads each payload **directly to `addr`** while computing MD5. It then reads the 16-byte signature, Blowfish-decrypts both halves with the key at image offset 0xbdb4, and compares. A mismatch aborts the load at once, so later records are never copied, but **earlier records are already in RAM**.
- Every record is data. The loader never executes a record; the only jump is to the terminator's `entry` at kernel boot. So the pointer can be a **4-byte data record at 0x30008430** and the palette a 512-byte data record at 0x30009400. No code is needed.
- Order used: the stock initramfs and kernel records first and unchanged, then table, then palette, then pointer last. If the table record fails verification, the pointer is never written. A failure in the stock records stops the load before any graphics record.
- Because the pointer and palette sit in `.data`/`.bss`, every reset restores the stock values (step 2). The redirect lives only as long as the `ttsystem` file that carries it.

## 4. What a failure looks like, and recovery

Failure modes (the emulation evidence is in section 7):

| Fault | Effect | When it bites |
|---|---|---|
| Graphics record signature bad | Load fails at that record. The loader tries the other file names and drives, then shows 0x44 and waits for media or USB | Every boot. Recovery is the stock path: USB host → storage mode → copy back `ttsystem.prev` |
| Chain broken (bad `len`) | The lookup walks through arbitrary memory. In emulation it faults on an unmapped read at 0x30b22788. On the device, unmapped addresses abort, and mapped I/O could be read with side effects. A false id match could set a huge w*h and overrun the decode buffer into DMA buffers, heap and code | Only when a table draw happens after the load (steps 12-15) |
| RLE stream bad | A zero token is skipped (no hang, emulated). The image shifts; a run that overshoots w*h writes up to 127 bytes past the image inside the 0x30600000 buffer region | Same |
| w*h too large | Overruns the 0x30600000 buffer by (w*h - 0x1fe00) bytes, into 0x30620000 and beyond | Same |

**What a crash or hang looks like** [H, from the code]: the screen keeps the last complete frame (often the homtom splash, or a partly drawn storage screen). The Mac sees the device enumerate but no disk appears, or nothing at all. There is no recovery inside the bootloader. Its only watchdog use is a deliberate self-reset (dis:3771-3775): it never arms the watchdog as a guard, the key is a GPIO that a hung CPU no longer reads, and storage mode itself is the bootloader.

**Recovery options:**

0. **Pinhole reset with the Mac attached (best, see 0.5).** Storage mode comes before `ttsystem` is read, so a bad graphics record never runs before the image can be replaced.
1. **Boot without a USB host.** On a boot with no host present, the bootloader draws nothing from the table after the load (steps 10-16: 0x45 before the load uses the stock table, then the 24-bit splash, then the kernel) [V]. So even a broken table never runs, unless `SPLASHW.BMP` fails, which is present and verified [M] `LESSONS.md:79`. Linux then boots from the unchanged kernel and initramfs records. Once Linux is up, plug USB in or use Bluetooth, and restore `ttsystem.prev` with the existing tools (`tools/homtom-stage-live`, `tools/homtom-recover`). This needs only that the unit can be **reset** out of the hang.
2. **Resetting a hung bootloader** is the open point. Nothing in the repo documents a hardware reset (a long power-key hold that cuts power, or a reset pin) [H]. The 10 s hold in `docs/POWER_KEY.md` is a Linux daemon and does not help. A sealed battery would otherwise have to drain. **User check before stage 0:** find out whether this unit has a reset pin or button, and whether a 15-20 s power-key hold forces a restart while the stock bootloader sits in storage mode. This is harmless today, and it decides how bad a hang would be.
3. An SD-card `TTSYSTEM` does not help against a bad table: the loader uses SD only if INTERNAL fails to load, and a well-signed bad table loads fine [V, dis:232-309]. It does help if a graphics record is corrupt (signature fails).
4. The NOR is not touched, so stage-1 code, the gzip and the stock table stay intact. A reset always starts from stock pointer and palette (step 2).

Because stage 0 makes the bootloader draw byte-identical images from the new location (emulated, section 7), a hang in stage 0 could only come from the record mechanism itself: an overlap or a loader quirk. Section 3 rules both out on paper.

## 5. Staged test plan (user present, unlocked Mac, USB cable)

Each stage uses the usual staging (`tools/homtom-stage` keeps `ttsystem.prev` = `ttsystem.ladder-q`, which must be marked good first with `tools/homtom-mark-good`). Before staging, check the SHA-256 against section 6.

**Stage 0: mechanism, no visible change.** `ttsystem.sg-stage0`: ladder-q plus a byte-identical copy of the current NOR table (including the R1 black id 0) at 0x30F00000, the identical palette, and the pointer.
- Expect: an unchanged boot (black, then the start screen, then the splash, then Linux), and an unchanged storage mode on the next reboot with USB. The Mac mounts INTERNAL; eject, and Linux boots.
- Observable: the boot completes; the storage screens look exactly as before. Optional: from Linux, `devmem` reads of 0x30F00000-0x30F0000F should show the palette's first bytes if the kernel has not reused that page yet [H; a miss proves nothing].
- Fallback: if the unit does not come back within 2 minutes, unplug USB, reset (see 4.2), power on without USB, wait for Linux, plug in, and restore `ttsystem.ladder-q` from Linux (`tools/homtom-stage-live`). Do not rely on `tools/homtom-recover` here: it needs storage mode, which is the screen under test.

**Stage 1: one tiny visible change.** `ttsystem.sg-stage1`: as stage 0, except that id 263 (9x12, the "6" in the label) is a solid block in the stock palette entry nearest homtom blue (index 165).
- Expect, in storage mode: the top-right label reads "TomTom GO ▮30" instead of "TomTom GO 630" on every frame. After ejecting, the "starting" screen during the reload shows the same label. At power-on the first start screen still shows "GO 630": that one is drawn before the load.
- What it proves: the redirect works, and storage mode starts after the records load. If the label stays "630" in storage mode but changes on the reload after eject, storage mode was entered at the early check (step 9), and the redirect cannot reach the first storage screen. If it never changes, either the label is drawn by another switch case or the pointer did not take effect; in that case repeat with id 219 (the dock picture) changed instead.
- Fallback: as stage 0.

**Stage 2: the real scenes** (only after the user approves the mockups). The table `workspace/storagegfx/tables/table-stage2-candidate.bin` with its own palette. Built only on request (`ttbl.py stage`). Stage 2 also replaces the palette in RAM. Because id 0 keeps index 0 = black, the splash-failure fallback (step 12) still draws black.
- Expect: the homtom storage screens (the mockups, regenerated locally) in storage mode and on the reload after eject. Power-on still shows black, then the **stock** 0x45 start screen (drawn before the load), then the splash.
- Fallback: as stage 0.

**Go/no-go for stage 0 on the device:** done [M]: stage 0 and stage 1 booted and storage mode worked, but the redirect never showed in storage mode, explained in 0.2. Current go/no-go: section 0.6. Stage 2 below cannot work in the reboot-with-host flow.

## 6. Candidate images (offline only; not staged anywhere)

| File | Bytes | SHA-256 | Records |
|---|---|---|---|
| `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sg-stage0` | 4,489,776 | `2ea818a811e2514876dc3a1b320fb8842c47951195ad9ed29738bb227257f622` | 0x31000000 (1,267,680), 0x31700000 (3,127,864), 0x30F00000 (93,580), 0x30009400 (512), 0x30008430 (4) |
| `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sg-stage1` | 4,489,678 | `3913ea1eaf908655e7e74cd6d6f3a24e54a3b7dc9417d9fdeffb977ecd946aad` | same, table 93,482 bytes |

Each has a `.sha256` file next to it. Base: `ttsystem.ladder-q` (SHA-256 a60810b8…), whose two records and terminator (entry 0x31700000, param 0x30000000) are carried over byte for byte.

Verification (`python3 workspace/storagegfx/ttbl.py verify IMAGE artifacts/nor-analysis/bootloader.out`) replays the loader. It checks the magic, the structural pass and the trailer rule. It then compares MD5 with the Blowfish-decrypted signature, using **the bootloader's own P-array and S-boxes (0x33dd4, 0x33e1c) and key (0xbdb4)**, so it shares no code with the signer. It also checks every load range against the RAM map in section 3. Results:

- `ttsystem.ladder-q`: 2 records, signatures OK (the verifier agrees with an image that boots) [V].
- The signer (`workspace/storagegfx/ttsign`, built from the repo's `tools/ttimage` MD5 and Blowfish sources) reproduces ladder-q's two signatures byte for byte [V].
- `sg-stage0` and `sg-stage1`: 5 records each, all signatures OK, no overlaps; "well-formed and signed" [V].
- Tamper test: one flipped bit in the stage-1 table record gives "signature BAD" for exactly that record [V].

## 7. Emulator: what is feasible

- No full-system path exists. There is no S3C2443 or ARM920T machine, and phase 3 of `docs/emulator-plan.md` (the real NOR bootloader) was never built. Running the whole boot flow offline is not feasible without that work.
- **Partial emulation, done.** `workspace/storagegfx/emu-draw.py` runs the bootloader's own lookup, RLE decoder and draw loop (0x30b06844, 0x30b0ac1c, 0x30b067d8) in a CPU emulator library inside a throwaway container (`Dockerfile.emu`; nothing installed on the Mac). It loads `bootloader.out` at 0x30b00000, copies `.data` as the C runtime does, puts the table where the records would, and draws every id of every group-16 scene, plus ids 0, 230 and 263, through a hooked plot function. It compares every plotted pixel with an independent decode (`firmware/norimage.py`) and traps writes outside the decode buffer and the stack.

| Table | Result |
|---|---|
| Stock NOR table (pointer 0x28201) | 19 ids, 354,526 pixels, all match (validates the harness) |
| `table-stage0.bin` at 0x30F00000 | 22 ids, 368,329 pixels, all match, no stray writes |
| `table-stage1.bin` at 0x30F00000 | 22 ids, all match |
| `table-stage2-candidate.bin` (own palette) | 22 ids, all match |
| Broken chain (id 219 `len` = 0x7fffff00) | Unmapped read fault at 0x30b22788 for every id after 219 |
| Zero RLE token (id 219) | Returns, no hang, image desynchronised |

Run: `docker run --rm --platform linux/amd64 -v "$PWD":/repo:ro -w /repo homtom-bgdraw:2.0.1 python3 workspace/storagegfx/emu-draw.py artifacts/nor-analysis/r1-run-20261107/nor-after-r1.bin artifacts/nor-analysis/bootloader.out TABLE` (build the image once with `docker build --platform linux/amd64 -t homtom-bgdraw:2.0.1 -f workspace/storagegfx/Dockerfile.emu workspace/storagegfx`).

Not emulated: the loader's FAT reads, USB timing (step 9 vs step 13) and the hardware. Those are what stage 1 measures.

## 8. Replacement art

Not published (stock artwork); regenerate locally with `workspace/storagegfx/run.sh`.

- Shown in storage mode on this unit: group 16, modes 0x00, 0x41/0x42, 0x81/0x82, 0xC1/0xC2 (storage), 0x45/0x85 (start), 0x44, 0xC4, 0x43 (no system or card checks), and 0x84 (in the table, never selected). Ids 218-221, 222-228, 232-239, plus labels 229-231 and 263 [V scene table].
- Sizes are unchanged from stock (218 480x272, 219 294x262, 220 270x117, 221 88x79, 222-227 76x123, 228/238 63x42, 229-231 134x12, 232-237 69x94, 239 90x90, 263 9x12). Positions are fixed by the scene table.
- Format: 8-bit indexed, one shared 256-entry RGB565 palette, RLE as in `firmware/norimage.py`. The stock palette has no homtom blue, so the candidate carries its own palette (index 0 kept black) and needs the palette record. The candidate table is 52,306 bytes (stock 93,580).
- Design: ink background, bone text, homtom blue, coral for warnings only, Poppins, the mark from `brand/homtom-mark.svg`. 219 carries the "Storage mode" title, the computer-and-drive pictogram and "Eject the drive on the computer to start homtom." 220 is "Copying data / Do not disconnect", 221 a "Please wait" spinner, 228/238 the "Internal" and "SD card" activity badges, 239 the error mark, 222-227 the SD card with a building strike, and 232-237 the card sliding out of a slot.
- Overlays are opaque rectangles, so each is cut from a full-screen composite of the layers it sits on. This keeps the edges invisible.

## 9. Files

| Path | What |
|---|---|
| (local output) `docs/storage-gfx-mockups/` | Contact sheet, one PNG per state, image sheet, stage-1 preview; not published |
| `workspace/storagegfx/render-art.swift` | Renders the homtom images at stock sizes (CoreText, Poppins) |
| `workspace/storagegfx/storagegfx.py` | Palette, RLE table build, scene simulator from the real scene table |
| `workspace/storagegfx/sheets.swift` | Review sheets |
| `workspace/storagegfx/run.sh` | Rebuilds art, candidate table and mockups |
| `workspace/storagegfx/stages.py` | Stage-0/1 tables from the current NOR dump (record splice; R1 left id 0 non-canonical, so no full re-encode) |
| `workspace/storagegfx/ttsign.c`, `ttbl.py` | Signer, packer (`stage`) and loader-faithful verifier (`verify`) |
| `workspace/storagegfx/emu-draw.py`, `Dockerfile.emu` | Bootloader draw-code emulation |
| `workspace/storagegfx/tables/` | `table-stage0.bin`, `table-stage1.bin`, `table-stage2-candidate.bin`, `broken/` test tables |

## 10. Steps for the user (stage 0)

Do these only when you decide to run stage 0, with the Mac unlocked and the cable at hand.

1. Answer 4.2: does the unit have a reset pin or button? Does a 15-20 s power-key hold restart it from stock storage mode? Note the result.
2. Confirm that Linux is running `ttsystem.ladder-q` and marked good: `tools/homtom-mark-good`.
3. Check the image: `shasum -a 256 artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sg-stage0` must print `2ea818a8…f622`. Then run `python3 workspace/storagegfx/ttbl.py verify artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sg-stage0 artifacts/nor-analysis/bootloader.out`, which must end with "well-formed and signed".
4. `tools/homtom-to-storage` (still the stock image, so storage mode is unchanged), then `tools/homtom-stage artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.sg-stage0` (keeps `ttsystem.prev`).
5. `tools/homtom-to-linux`: the unit loads stage 0 and boots. Expect black, the stock start screen, the homtom splash, then Linux as usual.
6. `tools/homtom-to-storage` again: this is the first storage mode drawn through the redirected table. Expect the stock storage screen and INTERNAL on the Mac. Then `tools/homtom-to-linux`. Write down what you saw.
7. If step 5 or 6 does not finish within 2 minutes: unplug USB, reset (step 1), power on **without** USB, wait for Linux, plug in, then restore from Linux with `tools/homtom-stage-live` using `ttsystem.ladder-q`. If storage mode works but Linux does not come up, `tools/homtom-recover` restores `ttsystem.prev` through storage mode as usual.
8. Report the result before stage 1 is staged.

# GO 630 board photos: components, pads, NOR package

Status: photo analysis, 2026-11-07. Desk study of 30 owner photos of an opened GO 630 (main board `DA0VF8MB8C0 REV.C`). Nothing was run on the device. Extends `docs/HARDWARE_MAP.md`, `docs/EXPANSION.md` and `docs/NOR_PLAN.md`.

Tags: **[V]** clearly visible in the named photo. **[H]** inference (reasoning given). Photo names refer to the user's originals in `.runtime/board-photos/converted/` (IMG_3698-3718) and `.runtime/board-photos/more/converted/` (IMG_3719-3728). Side A = the side with the SoC and shields; side B = the battery side.

Image rights: the photographs are the user's own work and the annotated images below are derived from them; the user will choose the licence.

## 1. Results in brief

- **NOR package: FBGA48, not TSOP48.** U9, next to the SoC, is a leadless ball-grid part of about 6.5 x 8.5 mm. Its footprint carries a ball-index silkscreen (columns 1-6, rows A-H), which is a 48-ball 6 x 8 array. Chip-off therefore means BGA rework, so JTAG is the first route for an external NOR read.
- **The part number of U9 is under a factory sticker.** Only a lot or date line is visible (`0814SDA`, see 3.2). The lab's chip-ID read (0x7F/0x1C/0x22BA, EN29LV400 bottom boot) remains the identification.
- **U33 (sticker `U33 VF8B`) is a second FBGA48 chip inside the GPS shield.** It sits next to a SiRF **GSC3e/LP**, the GSC3 variant without on-chip flash, so U33 is most likely the GPS firmware flash. It is not the boot NOR.
- **No labelled JTAG or UART pads are visible.** No TCK/TMS/TDI/TDO/nTRST, TX/RX or J-numbered debug header appears in any photographed area. The numbered rows near U9 and the SiRF chip are BGA ball indices, not pads. The best leads are the side-B test-point field (TP29 and about ten similar round pads under the yellow film) and a footprint **J9** that runs under the battery.

## 2. Annotated images

All images are in `docs/board-photos/publish/`, upright, metadata-free JPEG, at most 1600 px wide and under 500 KB.

| File | Shows | Source photo | Regions blurred |
|---|---|---|---|
| [01-side-a-overview.jpg](board-photos/publish/01-side-a-overview.jpg) | Side A with shield lids removed: SoC, DDR, U9, codec, moviNAND, LCD connector, GPS shield | IMG_3724 | Yellow programming sticker on the moviNAND |
| [02-u9-boot-nor.jpg](board-photos/publish/02-u9-boot-nor.jpg) | U9 close-up: leadless body, ball-index silkscreen, R27, lot line | IMG_3720 | None (the `U9 VF8B 5.5022` sticker is a programming label, not a unit number) |
| [03-gps-area.jpg](board-photos/publish/03-gps-area.jpg) | GPS block: SiRF GSC3e/LP, U33, crystal, oscillator, LTC3455 | IMG_3723 | None (the `U33 VF8B` sticker carries no unit number) |
| [04-side-b-overview.jpg](board-photos/publish/04-side-b-overview.jpg) | Side B: USB, jacks, J3, J7, power stage, coax socket, TP field, J9, Bluetooth | IMG_3703 | Whole battery label (barcode and pack serial); a small printed code box at the left board edge |
| [05-side-a-power-corner.jpg](board-photos/publish/05-side-a-power-corner.jpg) | Side A lower right: LTC3455, U27, round pads, SW2, 2x2 pad blocks, ring-lug wire | IMG_3702 | None needed (crop excludes the board sticker) |
| [06-side-b-test-pads.jpg](board-photos/publish/06-side-b-test-pads.jpg) | Side B test-pad field: TP29, larger round pads, J9 footprint | IMG_3708 | None needed (crop excludes the battery label) |
| [07-u33-gps-flash.jpg](board-photos/publish/07-u33-gps-flash.jpg) | U33 close-up with its A-H / 1-6 index silkscreen | IMG_3725 | None |

Privacy check, done on every output by a second visual pass: no board sticker (the white sticker on the side-A shield lid carries the board and unit serial), no battery barcode or pack number, no hand. The board part number `DA0VF8MB8C0 REV.C` and chip markings are kept. **The original photos must not be published**: they carry EXIF GPS coordinates and the camera model. The converted copies in `.runtime/` still carry that EXIF block and an orientation tag (their pixels are stored landscape), because `scripts/convert-photos.sh` reads `sips -g orientation`, which returns nothing for these files, so its rotate step never runs [V: `file` shows `orientation=upper-right, GPS-Data`]. The images here were rendered with the orientation applied and no metadata.

## 3. Component inventory

### 3.1 Table

| # | Ref / location | Marking read | Package | Function and block | Photos | Tag |
|---|---|---|---|---|---|---|
| 1 | Side A, centre-left, inside the large shield frame | `SAMSUNG S3C2443XL-40 0837 ARM N15C3PA4` | FBGA, about 13 mm square | SoC (ARM920T), date code 2008 wk 37 | 3719, 3722, 3724 | [V] marking; [H] date reading |
| 2 | Side A, top-left corner of the SoC frame | `SEC 834 FGC3 K4X51163PE YMG043A1` | FBGA, about 9.5 x 8.5 mm | Mobile DDR, 512 Mbit x16 = 64 MB | 3721, 3722, 3724 | [V] marking; [H] capacity from the part number |
| 3 | U9, side A, left of the SoC inside the same frame | Sticker `U9 VF8B / 5.5022 / 2G` (last glyph may be Q); chip line `0814SDA` | Leadless FBGA48 | Boot NOR (EN29LV400 by lab chip ID) | 3720, 3722, 3724 | [V] package features; [H] identity by position and chip ID |
| 4 | Side A, below the SoC frame, partly under the black SD tape | Partial chip line reading like `CSE664A6`; yellow sticker `… 2 80 2G` | Large FBGA/LGA | moviNAND (eMMC on HSMMC1, 2 GB) | 3698, 3722, 3724 | [V] presence; [H] function by position, size and the project's 2 GB HSMMC device |
| 5 | Side A, top, beside the screw post | `WM8750BG 87AL4GV` (Wolfson logo) | QFN-32 | Audio codec | 3700, 3722 | [V] |
| 6 | Side A, between SoC and DDR | `12.000 KDS…` | SMD crystal, plus a second metal-can part under a shield tab | 12 MHz system crystal; second part unread | 3719, 3724 | [V] 12.000; [H] second part |
| 7 | Side A, right of the SoC frame | Long FPC connector, silk `50` at one end; `PRx` resistor arrays marked `220`/`022` beside it | FPC, about 50 contacts | LCD/touch flex to the display board | 3699, 3722 | [V] connector and arrays; [H] contact count and 22 Ω series terminations |
| 8 | Side A, lower right shield (GPS shield) | `SiRF GSC3e/LP 7975 TABB N12 083x` | BGA, ball index 1-10 on silk | GPS (SiRF Star III, UART2) | 3723, 3725, 3728 | [V] marking |
| 9 | U33, inside the GPS shield above the SiRF chip | Sticker `U33 VF8B 2G`; partial chip line above the sticker | Leadless FBGA48 (A-H / 1-6 silk) | External flash for the SiRF firmware | 3723, 3725, 3728 | [V] package features; [H] function: the `e` variant of the GSC3 has no on-chip flash, and the stock driver has a `GPS_REPRO` reflash pin (GPJ8) |
| 10 | Inside the GPS shield | Crystal X5; metal oscillator marked `E4H3 …`; small SOT parts | SMD | GPS reference clocks (TCXO), LNA/SAW front end | 3723, 3727 | [V] parts; [H] roles |
| 11 | Side A, lower centre | `LT` logo, `3455`, `8023D` | QFN-24 | LTC3455 power manager, Li-ion charger and dual buck | 3702, 3714 | [V] |
| 12 | Side A, around the LTC3455 | `4R7` (several) and `470` inductors, tantalum caps | SMD | Buck inductors | 3698, 3702 | [V] |
| 13 | Side B, top power area, U16 | `825L1 P2624` | MSOP-8 | Unidentified power part | 3717 | [V] marking; [H] power path or regulator |
| 14 | Side B, top power area, U17 | `813L1 2010B` | DFN-8 | Unidentified power part | 3717 | [V] marking; [H] as above |
| 15 | Side B, top, D5 | `ON B2E1 R837` | SMB diode | Schottky in the power path | 3716 | [V] marking; [H] role |
| 16 | Side B, top | `470 6U` | Shielded power inductor | Boost or buck stage in the side-B power area | 3708 | [V] |
| 17 | Side B, bottom right | `CSR` logo plus lot lines | QFN, about 7 mm | Bluetooth (BlueCore4 ROM per the stock source) | 3704, 3715 | [V] CSR logo; [H] part from source |
| 18 | Side B, bottom right, X4 | Crystal | SMD | Bluetooth reference (16 MHz per source) | 3715 | [V] part; [H] frequency |
| 19 | Side B, bottom right edge | `INPAQ` | Ceramic chip antenna | Bluetooth antenna | 3704, 3715 | [V] |
| 20 | Antenna bracket (case) | `GVF8`, Inpaq logo | Ceramic patch | GPS patch antenna | 3718 | [V] |
| 21 | Side B, top right corner | Coax socket on the board edge | Through-hole coax | External GPS antenna input | 3703, 3709 | [V] socket; [H] role |
| 22 | Side B, J7 | Red, yellow, black wires to the pack | 3-pin wire-to-board | Battery (+, thermistor or ID, GND) | 3703, 3717 | [V] connector; [H] yellow = thermistor |
| 23 | Battery | Htenergy, Model VF8, 3.7 V 1320 mAh Li-polymer | Pouch | Battery (matches `GOBATTERY_1320`) | 3709, 3710 | [V] |
| 24 | Side B, J3 | Red/black pair to the speaker | 2-pin wire-to-board | Speaker | 3703, 3713 | [V] |
| 25 | Case | `NM 4Ω 2W D0-0140-G -26J-GP` | Round speaker | Speaker (4 Ω, 2 W) | 3711, 3713 | [V] |
| 26 | Side B, left edge | Mini-B socket | SMD | USB (gadget link, charging) | 3703, 3706 | [V] |
| 27 | Side B, left edge, lower | Two 3.5 mm jacks | Through-hole | Headphone and a second jack | 3703, 3706 | [V] jacks; [H] second jack = line-in (`LINEIN_DETECT` exists in source) |
| 28 | Side A, top, beside the codec | Empty white 2-pin socket | Wire-to-board | Unknown (nothing plugged in) | 3699, 3700 | [V] empty; [H] role |
| 29 | Side A, left edge | 4-pin tactile switch (silk pins 1-4), `ERF8P` SOT-23-6 beside it, black round capsule with a lead | SMD | Side button; capsule possibly the microphone | 3700, 3722 | [V] parts; [H] which button and capsule role |
| 30 | Side A, lower right, SW2 | Tactile switch at the board edge | SMD | Second button (power key or pinhole reset) | 3702, 3714 | [V] part; [H] role |
| 31 | Side A, lower right, U46 | Small light-coloured package at the edge | SMD | Ambient light sensor | 3714, 3723 | [H] (LX_EN/ADC 2 in source) |
| 32 | Side A, lower right, U27 | Unread | QFN-20 class | Unknown | 3702, 3714 | [V] package only |
| 33 | Side A, lower right | Black wire with ring lug on a screw post | — | Ground strap to the case or bracket | 3702, 3713 | [V] part; [H] role |
| 34 | Side B, centre | Mesh-covered opening on a copper pad | — | Acoustic port or vent | 3703, 3708 | [V] part; [H] role |

Not seen in any photographed area: a KXP74 accelerometer, an AK8970 compass, an Si4710 FM transmitter or an FM2010 echo DSP [V for the areas shown; areas under tape, copper foil and the battery were not visible]. This agrees with the HARDWARE_MAP inference that the GO 630 has no dead-reckoning parts.

No multi-pin dock connector is fitted on any photographed board edge: the external contacts are mini-USB, two jacks and the coax socket [V, 3703]. Where the dock UART (UART3) goes, if anywhere, is still open.

### 3.2 The NOR candidate U9 in detail

Package [V, 3720, 3722, 3724]:

- No leads on any side; the body edge is flat and the board pads are hidden under it. A TSOP48 would show 24 gull-wing leads on each short side.
- The footprint has a ball-index silkscreen: the digits `2 3 4 5 6` along the top edge (the `1` sits under the via next to the corner) and a column of letters down the left edge, partly hidden by the shield frame. U33 has the same silkscreen fully readable (`A B C D E F G H` and `2 … 6`, image 07). Six columns by eight rows is the 48-ball FBGA layout.

Size [H, measured in pixels on 3724, a near top-down shot]:

- Assumption: the S3C2443 is a 400-ball FBGA of 13 x 13 mm [H from memory; check the package drawing in the S3C2443 user manual].
- In 3724 the SoC body spans 262 x 250 px and U9 spans 127 x 155 px, giving U9 about 6.3 x 8.1 mm. The tilted shot 3722 gives about 6.7 x 9.4 mm. Both fit the common 6 x 8 mm FBGA48 body. A TSOP48 body is 12 x 18.4 mm (20 mm with leads), which would be wider than the SoC; U9 is about half its width.

Marking:

- The sticker covers the part-number lines. It reads `U9 VF8B / 5.5022 / 2G` [V]. `U9` repeats the reference designator and `VF8B` the board project, so this is a factory programming label [H]. `5.5022` is probably the bootloader version programmed at the factory [H]. The NOR's version string at 0x3FFF0 (`docs/NOR_PLAN.md`, SA6) can confirm it without touching the chip.
- One line is visible on the chip to the left of the sticker, rotated: `0814SDA` (the fifth glyph may be `U`) [V, 3720 at full resolution]. It reads like a date code (2008 week 14) plus a lot code; it does not name the maker [H].
- Every other photo angle (3722, 3724) shows the same line and nothing more. Reading the part number needs the sticker lifted (section 6).

What it means for programming:

- In-circuit access to the NOR pins is impossible: every signal sits under the package. The only in-circuit route is through the SoC, so **JTAG (route J)**.
- **Chip-off (route P)** becomes BGA rework: hot air with a bottom preheater, a few millimetres from the SoC and the DDR inside a soldered shield fence; then cleaning and reballing (48 balls, 0.8 mm pitch is usual for this body [H: check the EN29LV400 FBGA drawing in datasheet D1/D2]); an FBGA48 6 x 8 mm socket adapter on the programmer; and BGA reflow back. That is a real risk to a working unit. Spare chips must be the FBGA48 variant.
- The NOR_PLAN route P text assumed TSOP48; that line now carries a correction.

### 3.3 Is U33 also flash?

Probably yes, but for the GPS, not for the boot chain:

- U33 has the same FBGA48 footprint silkscreen as U9 and a programming sticker in the same style (`U33 VF8B 2G`) [V, 3725, image 07].
- It sits inside the GPS shield next to a SiRF `GSC3e/LP` [V, 3725, 3728]. The GSC3 family comes as `GSC3f/LP` with on-chip flash and `GSC3e/LP` without it [H from memory], so an `e` part needs an external flash for its firmware.
- The stock kernel has a `GPS_REPRO` pin (GPJ8) that puts the SiRF into its reflash mode (`docs/HARDWARE_MAP.md` 3.2), which fits a reprogrammable external flash.
- A few characters show above the sticker (an `0`, an `E` and part of a third glyph) [V, 3725]; not enough to name the part.

Leave U33 alone: it is not on the SoC bus, and the stock GPS firmware has no backup.

## 4. Pad and header candidates

Positions are given with side A oriented as in 3698 (board text `DA0VF8MB8C0` top right, SD slot bottom left) and side B as in 3703 (USB on the left edge, battery on the right). Confidence is for the stated assessment.

| # | Candidate | Where | What it may be | Confidence | Reasoning |
|---|---|---|---|---|---|
| P1 | **TP29 and about ten larger round pads** under the translucent yellow film | Side B, centre-left, between the USB socket column and the battery; image 06 | Factory bed-of-nails test points. Any of them could carry JTAG, UART0, nRESET or OM straps | Low to medium for "test points"; unknown for nets | TP29 is a labelled test point [V, 3705, 3708], so at least 29 test points exist; most of them are not in view. The larger pads stand apart from the smaller plugged vias. No other TP labels are readable through the film |
| P2 | **J9 footprint** | Side B, right edge of the yellow area, running under the battery (label `J9` beside a footprint corner); images 04 and 06 | A connector or pad header footprint: debug, dock or factory programming | Unknown (cannot tell from photos) | A J-number on a production board that is not an external connector often marks a factory header. Only its corner shows [V, 3708]. Needs the battery lifted |
| P3 | **R27 (unpopulated) and a round pad** | Side A, just above U9 (left of the SoC, inside the frame); image 02 | Build option strap; candidates: a boot-mode (OM) option, or a NOR pin option (BYTE#, RESET#) | Low for JTAG; medium for "strap" | Two bare pads with no part [V, 3720]; a separate round pad routed to them. Its position between SoC and NOR suggests a memory or boot option. **Do not bridge it** |
| P4 | **Four round plated pads** around a screw post | Side A, lower right, beside SW2; image 05 | Spring or pogo contacts (ground to case, or antenna), or test points | Low | Larger than vias, with rings, three in an L and one apart [V, 3702, 3714]; close to a mounting post, which suggests mechanical contacts |
| P5 | **2x2 split square pads** H16, H17, H19 and two unlabelled ones | Side A: H16 top left beside the codec; H19 left edge beside R243; H17 at the GPS shield's right edge; one below the SD tape near C272; one beside C266 under the GPS shield; images 01, 03, 05 | Ground contact pads for case springs or EMI gaskets | Medium | `H` designators are usually hardware, not circuits; four-way split squares are a common chassis-ground contact pad [H]. Useful as ground references once confirmed with a continuity test |
| P6 | **R263/R264/C369/C370**, a 2 x 4 pad block | Side A, right of the LCD connector, in front of the copper tape | Unfitted R/C option on two traces from the LCD connector area (touch or panel option) | Low for debug | Pads bare, two traces enter from the left [V, 3699] |
| P7 | **J10** with R191/C327 | Side A, below the lower end of the LCD connector | Two-pad jumper or small connector option | Low | Small footprint with a J number [V, 3699] |
| P8 | Empty 2-pin socket | Side A, top, next to the board name | Unknown peripheral (backup cell, microphone, or unused option) | Low | Fitted but unused [V, 3700] |
| — | Numbered silk rows `2 3 4 5 6`, `1 2 3 4 5 6 7 8 9 10`, `A … H` | Above U9, above the SiRF chip, beside U33 | **Not pads**: BGA ball indices | High | They frame BGA footprints [V, 3720, 3725] |
| — | Silk `1 2 3 4` beside the side switch | Side A left edge | **Not pads**: switch pin numbers | High | Pin labels around a 4-pin tactile switch [V, 3700] |

Areas not seen, which may hold more pads: under the battery, under the yellow film (labels illegible), under the black tape over the SD slot, under the copper foil at side A top right and over the GPS shield, under the shield frames' rims, and the board edges hidden by the case.

## 5. Probing plan

The goal is to find JTAG (and UART0 if it is reachable) without writing anything.

### 5.1 Safety rules

- ESD: wrist strap or touch the USB shell often; work on a mat.
- **Battery off for any soldering**: unplug J7 first, and USB too. Plug the battery back only to measure.
- Never connect a supply to a pad. Measure only. The first contact with an unknown pad is the multimeter, never the adapter.
- Do not probe inside the SoC/DDR frame with metal tips while powered: the 0402 parts there sit next to DDR and core supply nets, and a slip shorts them.
- Do not bridge R27 or any unfitted footprint.
- Use 100 Ω to 1 kΩ series resistors in every adapter line; leave the adapter's VCC unconnected and use its VTref (sense) input only.
- Stop at the first sign of a changed device state (screen, USB, heat).

### 5.2 Unpowered (battery and USB unplugged)

1. **Ground reference.** USB shell, the 3.5 mm jack sleeve, and the SD card slot's ground contacts are ground [H, standard]. Confirm two of them read 0 Ω to each other.
2. **Continuity sweep** of every candidate pad (P1-P8, all round pads in the side-B field) to ground. A pad that beeps is ground, mark it and drop it.
3. **Diode-mode signature** (black lead on ground, red on the pad). SoC I/O pins usually read 0.4-0.7 V; supply rails read low (0.1-0.3 V, many parts in parallel); open nets read OL. Record every value: pads with the same reading as known SoC pins are the interesting ones.
4. **Continuity to known nets**: USB VBUS (the mini-B's pin 1) and the SD slot's VDD pin, to find the 5 V and 3.3 V nets among the pads; SD CLK/CMD/DAT pins too, which removes GPE5-10 from the candidate list.

### 5.3 Powered (battery on J7, unit running the homtom image, USB attached)

1. **DC voltage of every candidate** against ground. Expected rails [H]: about 3.7-4.2 V battery, 5 V VBUS, 3.3 V I/O, 1.8 V for the mobile DDR, about 1.2-1.3 V core. A pad on any of these with low diode impedance is a rail: skip it for JTAG.
2. **Idle JTAG levels** [H: S3C24xx convention; check the S3C2443 manual's pin table]: TDI and TMS high (internal pull-ups, so about 3.3 V); nTRST either high (pull-up) or held low by a board pull-down; TCK low or high depending on its pull; TDO high-impedance (it may float between levels, or sit at the level its trace picks up). nRESET high.
3. **Find UART0 and GPB9 by toggling** (EXPANSION section 3, step 2): toggle GPH0, GPH1, GPH8, GPH9 and GPB9 with masked `devmem` writes at 0.5 Hz and look for the pad that follows. Each pad found is also removed from the JTAG candidate list.
4. **Logic analyser** (optional): clip the ground, put up to 8 channels on candidate pads through 1 kΩ, and capture a cold boot at 1-4 MHz. A UART TX idles high and shows bursts after power-on; decode at 115200 and 38400. JTAG lines stay quiet.

### 5.4 JTAG test (read-only)

Only after 5.2 and 5.3 leave a short list of pads that idle like JTAG:

1. Solder 0.1 mm wire to ground, VTref (a 3.3 V point found in 5.2), and the candidate pads, each through a 1 kΩ resistor to the FT2232H's ADBUS0-3 (TCK, TDI, TDO, TMS). Battery unplugged while soldering; strain-relieve the wires with Kapton.
2. Plug the battery in, power the unit, start OpenOCD with an FT2232H config, `transport select jtag`, `adapter speed 100` (kHz), and **no target declared**, so OpenOCD auto-probes the chain. Commands: `init`, `scan_chain`. Nothing else: no `reset`, no `halt`, no flash command.
3. A good result is one TAP with IR length 4 and IDCODE **0x0032409D**, the ARM920T core ID [H: from memory, the value OpenOCD's configurations for other S3C24xx parts expect]. All-ones or all-zeros means a wrong pin assignment or a held TAP reset (try with the nTRST candidate driven high through the adapter's ADBUS4).
4. If the four pins are not known, try the permutations of the shortlisted pads, one OpenOCD run per assignment. With five candidates that is 120 runs, so the shortlist matters. An open-source JTAG pin-enumeration firmware on a 3.3 V microcontroller can automate this; treat it like the adapter (series resistors, no VCC).
5. Record the IDCODE output as an artifact. Halting the core or reading the NOR comes later, under NOR_PLAN gate J.

Warning signs that end the session: the unit resets or powers off when a pad is touched, an adapter pin gets warm, a pad reads below 0.2 V in diode mode (a rail), or `scan_chain` returns a TAP with a different IDCODE (the chain may include another device).

## 6. Photo retakes needed

| # | Area | How | Settles |
|---|---|---|---|
| R1 | U9 marking | Note the sticker text, lift one corner with tweezers (paper label, no heat), photograph straight down at 5-8 cm macro, diffuse light from two sides, then press the sticker back. Same for U33 | NOR maker and exact part (EN29LV400 vs EN29LV400A, FBGA ordering code) |
| R2 | Side B under the battery | Unplug J7, lift the pack on its adhesive only as far as it goes freely (no bending, no sharp tools near the pouch), photograph the whole area straight down from 15-20 cm with a mm ruler in frame | J9 and the rest of the TP field |
| R3 | Side B yellow film | Two shots at 30-45° from opposite sides, light from the side to kill glare; or peel the film back if it lifts cleanly | TP numbers next to each round pad |
| R4 | Both sides, full board, square on | Camera parallel to the board at 30 cm, same distance for both sides, ruler in frame, mounting holes visible | Registration: which side-B pads sit behind the SoC |
| R5 | Under the copper foil (side A top right) and the black tape on the GPS shield lid | Peel carefully, photograph, re-stick | Hidden parts or pads |
| R6 | U27, U46, U16, U17 markings | Macro at 5 cm, light at a low angle | Identify the remaining ICs |
| R7 | The windscreen-mount side of the case | Any contacts or pogo pads, and where they land on the board | Whether a dock UART reaches the outside |

## 7. What to buy

| Item | Why | Tied to |
|---|---|---|
| FT2232H module with 3.3 V I/O (a mini-module or a bare FT2232HL breakout), or a J-Link EDU Mini | JTAG route J; the second FT2232H channel doubles as a 3.3 V UART | 5.4 |
| 0.1 mm enamelled wire (or 36 AWG Kynar), flux pen, 0.2 mm conical tip, Kapton tape | Wiring to round test pads | 5.4 |
| 1 kΩ and 100 Ω resistors | Series protection in every line | 5.1 |
| Sharp needle probes or pogo-pin probe tips for the multimeter | Touching 0.6-1 mm pads without slipping | 5.2, 5.3 |
| 8-channel 24 MHz logic analyser (sigrok-compatible) | Boot-time UART search | 5.3 |
| USB microscope or a phone macro clip | Retakes R1, R3, R6 | 6 |
| Only for chip-off: hot-air station with a bottom preheater, FBGA48 (6 x 8 mm) socket adapter for the T48-class programmer (check that its device list has EN29LV400 in FBGA), 48-ball reball stencil at the datasheet pitch, matching solder balls, gel flux, 2-3 spare EN29LV400AB in FBGA48 | Route P as BGA rework | 3.2 |

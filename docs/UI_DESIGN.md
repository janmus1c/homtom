# UI design: Home Assistant look on the 480x272 panel

Status: design spec, 2026-11-07; implemented in `workspace/gui` the same day (host-tested, not yet on the device; checklist in section 9 marked). Deviations from the mockups and later user changes (System hub, battery indicator in the header, screen lock, time zone, the 16 px wordmark): `docs/UI_STYLE_NOTES.md`. Config and behaviour: `docs/UI_CONFIG.md`. Mockups: `docs/ui-mockups/` (how they are made: `docs/ui-mockups/README.md`).

Source marks: **[V]** the page or file was fetched and read for this spec; **[S]** seen only in a search snippet; **[D]** derived here (a design decision or calculation, not a quoted source). Home Assistant frontend values come from `home-assistant/frontend` at commit `eebc57d89adc78ce0eeb6c92bc1035c4815a5ce2` (fetched 2026-11-07); file paths below are relative to that tree.

## 1. Why the current screens read as generated

Looking at `workspace/gui/out/cfg/full-0-home.png`, `full-4-tools.png` and `controls-0-controls.png`:

| What the screen does | Why it reads as generic | Fix in this spec |
|---|---|---|
| Every card is the same dark rounded box with a small caption above a value; the grid gives every item the same weight | Identical card grids with equal spacing are the most cited tell; nothing is dominant | One dominant element per page (thermostat card, brightness slider), tiles only for things you act on, read-only values grouped in one strip |
| The bottom navigation is five large filled buttons in the brightest colour on the screen | Navigation is louder than the data; NN/g: give navigation less weight than content | Page dots and swipe; colour is spent on entity state only |
| One cyan is used for nav, sliders, toggle, progress bar and the logo; green appears for "Charging", "GPS fix" and a status dot | Accent overuse, colour with no state meaning; a dot that never changes | State colours from Home Assistant (amber light on, deep orange heating, steel blue sensors), primary blue only for controls you can press |
| All-caps tracked captions (`BATTERY`, `AMBIENT LIGHT`, `GPS AID`) | Small tracked labels above values are a listed tell; HA uses sentence case everywhere | Sentence case, no tracking, captions only where the icon does not explain the value |
| Poppins (a geometric display face) for all text, bold centred button labels | Default display font everywhere, flat hierarchy | Roboto (what HA uses) at 12/14/16/20/28 px, weights 400 and 500; Poppins only in the wordmark |
| `up 1d 2h` and a green dot in the header on every page | Status that never carries news | Header shows the clock; a warning appears only when something is wrong (offline, stale) |

## 2. Research: what makes UI look generated, and what fixes it

### Tells

| Tell | Sources | Confidence |
|---|---|---|
| Purple, indigo or blue gradients and cyan-on-dark as the default accent; gradient text | [V] https://prg.sh/ramblings/Why-Your-AI-Keeps-Building-the-Same-Purple-Gradient-Website, [V] https://impeccable.style/slop, [V] https://www.thefountaininstitute.com/blog/signs-vibe-coded-ui | high |
| Several saturated colours with no meaning; status dots that map to no real state | [V] Fountain Institute, [V] impeccable.style | high |
| Decorative glow, glassmorphism, hairline border plus wide soft shadow on the same card | [V] impeccable.style, [V] Fountain Institute | high |
| Identical card grids, cards nested in cards, equal spacing everywhere, very large radii, an icon tile above every heading | [V] impeccable.style, [V] Fountain Institute | high |
| Default type with a flat hierarchy, small tracked labels above headings, pill badges everywhere | [V] impeccable.style, [V] prg.sh | medium-high |
| Emoji or generic icons as navigation or bullets | [V] Fountain Institute | high |
| Motion with no cause: pulsing dots, floating elements | [V] impeccable.style | medium |
| Vague or inflated copy | [V] impeccable.style | medium |
| Error, empty and stale states missing | [V] prg.sh | medium (author's argument) |

### Remedies

| Remedy | Sources | Confidence |
|---|---|---|
| Colour as a budget: neutrals plus one accent; about 2 primary colours and at most 3 contrast levels ("if everything is contrasted, nothing stands out") | [V] Fountain Institute, [V] https://www.nngroup.com/articles/visual-hierarchy-ux-definition/ | high |
| Colour encodes state: HA cards use the icon and colour of the entity state, the same everywhere; unavailable entities are greyed out | [V] https://contributors.home-assistant.io/design/dashboards/, [V] https://www.home-assistant.io/dashboards/tile | high |
| Never colour alone: pair it with an icon or text (WCAG 1.4.1); colour is weak for magnitude, length and position read fastest | [V] https://contributors.home-assistant.io/design/foundations/accessibility/, [V] https://www.nngroup.com/articles/dashboards-preattentive/ | high |
| Hierarchy from scale, contrast and grouping, not more containers; at most 3 type sizes per view and 2 large elements; squint test | [V] NN/g visual hierarchy, [V] impeccable.style | high |
| One job per card, controls look tappable, built-in control strips ("features") beat icon buttons | [V] https://www.home-assistant.io/blog/2024/07/26/dashboard-chapter-2/ | high |
| HA density: 56 px rows, 8 px gap, 36 px icon chip, 42 px feature strips, 12-column sections 320-500 px wide (480 px is one section) | [V] HA blog chapter 2, [V] HA dashboards design page | high |
| Navigation lighter than data, maximise data pixels | [S] https://analyticsdemystified.com/2008/06/19/stephen-fews-derivation-of-tufte-the-data-pixel-ratio/ | medium |
| Sentence case, candid copy, errors say what happened and what to do | [V] https://contributors.home-assistant.io/design/ui-text/, [V] https://contributors.home-assistant.io/design/principles/ | high |
| Touch targets: HA buttons 40 px high, icon buttons 48x48, 24 px is a floor; Material 48 dp with 8 dp spacing; Apple 44 pt | [V] HA accessibility page, [S] https://support.google.com/accessibility/android/answer/7101858, [S] https://dequeuniversity.com/rules/attest-ios/1.0/touch-target-size | high / medium |
| Tokens instead of one-off values; a needed one-off points at a gap in the foundations | [V] https://contributors.home-assistant.io/design/foundations/ | high |

## 3. Home Assistant design language (extracted)

### Colour, dark and light theme

| Token | Light | Dark | Source |
|---|---|---|---|
| `--primary-color` | `#009ac7` (`--ha-color-primary-40`) | same (not overridden) | [V] `src/resources/theme/color/color.globals.ts`, `color/core.globals.ts` |
| Brand blue (`--ha-color-primary-50`) | `#18bcf2` | | [V] `color/core.globals.ts`; also `brand/README.md` "Home blue" |
| `--accent-color` | `#ff9800` | same | [V] `color.globals.ts` |
| `--primary-background-color` | `#fafafa` | `#111111` | [V] `color.globals.ts` |
| `--card-background-color` | `#ffffff` | `#1c1c1c` | [V] |
| `--secondary-background-color` | `#e5e5e5` | `#282828` | [V] |
| `--divider-color` | `rgba(0,0,0,.12)` | `rgba(225,225,225,.12)` | [V] |
| `--primary-text-color` | `#141414` (`--ha-color-text-primary` = neutral-05) | `#e1e1e1` | [V] `color.globals.ts`, `color/semantic.globals.ts` |
| `--secondary-text-color` | `#5e5e5e` (neutral-40) | `#9b9b9b` | [V] |
| `--disabled-text-color` | `#bdbdbd` | `#6f6f6f` | [V] |
| `--disabled-color` | | `#464646` | [V] |
| `--state-icon-color` (active entity without a domain colour, e.g. sensors) | `#44739e` | same | [V] |
| `--state-inactive-color` | `#9e9e9e` (grey) | same | [V] |
| `--state-unavailable-color` | disabled text colour | same | [V] |
| `--state-light-active-color`, `--state-switch-active-color`, `--state-binary_sensor-active-color` | `#ffc107` (amber) | same | [V] |
| `--state-climate-heat-color` / `-cool-` / `-auto-` / `-fan_only-` | `#ff6f22` / `#2196f3` / `#4caf50` / `#00bcd4` | same | [V] |
| `--state-person-home-color` | `#4caf50` | same | [V] |
| `--rgb-error-color`, warning, success, info | `219,68,55` / `255,166,0` / `67,160,71` / `3,155,229` | same | [V] |

### Shape, spacing, type, motion

| Item | Value | Source |
|---|---|---|
| Card radius | 12 px (`--ha-card-border-radius` falls back to `--ha-border-radius-lg`); 1 px border in divider colour; no shadow | [V] `src/components/ha-card.ts`, `src/resources/theme/core.globals.ts` |
| Radius scale | 4, 8, 12, 16, 20, 24 ... px, pill, circle | [V] `core.globals.ts` |
| Spacing scale | 4 px steps (`--ha-space-1` 4 px ... `--ha-space-20` 80 px) | [V] `core.globals.ts` |
| Tile content row | min height 56 px, padding 0 10 px, gap 10 px | [V] `src/components/tile/ha-tile-container.ts` |
| Tile icon | 36 px chip (pill radius), 24 px icon; chip background = tile colour at 20 % opacity; icon in tile colour | [V] `src/components/tile/ha-tile-icon.ts` |
| Tile text | name 14 px weight 500, line height 1.6, letter spacing 0.1 px; state 12 px weight 400, line height 1.2, 0.4 px; both in primary text colour | [V] `src/components/tile/ha-tile-info.ts` |
| Tile colour | off: `--state-inactive-color`; on: domain/state colour; lights with `rgb_color` use the light's own colour, saturation raised to at least 0.4 (near white shown as value 225) | [V] `src/panels/lovelace/cards/hui-tile-card.ts` |
| Control slider | 40 px thick, radius 8 px, track = disabled colour at 20 %, fill in the control colour, white 4 px handle inset by thickness/8 | [V] `src/components/ha-control-slider.ts` |
| Control switch | 40 px thick, min touch 40 px, radius 12 px, padding 4 px, background at 20 % | [V] `src/components/ha-control-switch.ts` |
| Badge | 36 px high, pill, 12 px side padding, 8 px gap, card background with 1 px border; text 12 px medium | [V] `src/components/ha-badge.ts` |
| Big number (more-info) | 57 px, weight 400, unit at 0.42 em | [V] `src/components/ha-big-number.ts` |
| Font | `Roboto, Noto, sans-serif`; sizes 10/12/14/16/20/24/28/32/40 px; weights 300/400/500/700 (body 400, action 500, heading 700) | [V] `src/resources/theme/typography.globals.ts`; [V] https://contributors.home-assistant.io/design/foundations/typography/ |
| Motion | 75/150/250/350 ms; all 1 ms under reduced motion; tile colour changes 180 ms ease-in-out | [V] `core.globals.ts`, `ha-tile-icon.ts` |
| Pressed feedback | ripple at 12 % of the tile colour (hover 4 %) | [V] `ha-tile-container.ts` |

### Icons

Entity icons are Material Design Icons (`mdi:` prefix) from Pictogrammers. The package licence says: icons Apache 2.0, code MIT ([V] `LICENSE` in `@mdi/svg` 7.4.47, "Pictogrammers Free License"). Every icon is one SVG path on a 24x24 viewBox. `mdi:bluetooth` and `mdi:usb` draw trademarked symbols; using them as status glyphs is normal practice, but they are not ours to restyle.

## 4. Device facts that drive the numbers

| Fact | Value |
|---|---|
| Panel | 480x272, RGB565, about 4.3 in diagonal, so about 128 px per inch: 1 px = 0.2 mm [D] |
| Touch | Resistive, single point, pressure needed; no hover |
| Viewing | Arm's length or wall; TN-class panel, so dark greys crush at angles (assumption to check on the unit) |
| 40 px | 7.9 mm, the minimum touch target here (HA's own minimum control size) [D] |

## 5. Tokens for the device

### Quantisation rules

- RGB565 keeps 5 bits of red and blue and 6 of green. A neutral grey stays neutral only when `G6 = 2 x R5` (or `2 x R5 + 1`). HA's dark card `#1c1c1c` rounds to `#181c18`, a visible green cast. All greys below are chosen from the neutral-safe set.
- Expand 5/6-bit values back to 8 bits as `(v << 3) | (v >> 2)` and `(v << 2) | (v >> 4)`; the hex in the tables is that expanded colour, so the mockups show what the panel shows.
- Opacity tints (chip at 20 %, pressed at 12 %) are mixed once in 24-bit sRGB against the surface they sit on and stored as a fixed RGB565 colour. Nothing is alpha blended at run time except anti-aliased edges, glyphs and icons.
- No gradients anywhere except the colour temperature track. That one is drawn with a 4x4 ordered (Bayer) dither when converting to RGB565, so it does not band. Flat tokens are exact RGB565 values, so dithering them would change nothing; dither only the gradient rectangle.
- The dark card is one step lighter than HA (`#212021` instead of `#1c1c1c`): on this panel the 8-level step to the background is otherwise too small. Check on the unit; `#181818` (0x18C3) is the HA-faithful fallback.

### Dark theme (default)

| Role | Token | RGB565 | Shown as | From HA |
|---|---|---|---|---|
| Page background | `bg` | `0x1082` | `#101010` | `#111111` |
| Card, tile, list | `card` | `0x2104` | `#212021` | `#1c1c1c` (one step lighter, see above) |
| Card border, dividers | `border` | `0x3186` | `#313031` | divider 12 % over card |
| Off chip, button fill, slider track | `chip_off` | `0x2945` | `#292829` | disabled colour at 20 % |
| Primary text | `text` | `0xDEFB` | `#DEDFDE` | `#e1e1e1` |
| Secondary text, off icons | `text2` | `0x9CF3` | `#9C9E9C` | `#9b9b9b` / `#9e9e9e` |
| Disabled, unavailable | `disabled` | `0x6B6D` | `#6B6D6B` | `#6f6f6f` |
| Inactive page dot, switch off | `dot_off` | `0x4A49` | `#4A494A` | `#464646` |
| Primary (pressable controls only) | `primary` | `0x04D8` | `#009AC6` | `#009ac7` |
| Logo mark only | `mark` | `0x1DDD` | `#18BAEF` | `#18bcf2` |
| Light on, occupancy detected | `amber` | `0xFE01` | `#FFC308` | `#ffc107` |
| Heating | `heat` | `0xFB64` | `#FF6D21` | `#ff6f22` |
| Cooling | `cool` | `0x24BE` | `#2196F7` | `#2196f3` |
| Sensor icons | `steel` | `0x4393` | `#42719C` | `#44739e` |
| Person home, OK | `green` | `0x4D6A` | `#4AAE52` | `#4caf50` |
| Error, destructive action | `error` | `0xDA27` | `#DE4539` | `219,68,55` |
| Warning, offline, stale | `warning` | `0xFD20` | `#FFA600` | `255,166,0` |
| Chip, light on | `chip_amber` | `0x4A03` | `#4A4118` | amber 20 % over card |
| Chip, heating | `chip_heat` | `0x4984` | `#4A3021` | heat 20 % over card |
| Chip, sensor | `chip_steel` | `0x2987` | `#293039` | steel 20 % over card |
| Chip, person home | `chip_green` | `0x29E5` | `#293C29` | green 20 % over card |
| Light tile brightness fill | `light_fill` | `0x3984` | `#393021` | amber 10 % over card [D] |
| Pressed tile (light) | `pressed_amber` | `0x39A4` | `#393421` | amber 12 % over card |
| Pressed tile or button (neutral) | `pressed` | `0x3186` | `#313031` | text 8 % over card [D] |

### Light theme

| Role | RGB565 | Shown as | From HA |
|---|---|---|---|
| `bg` | `0xF7BE` | `#F7F7F7` | `#fafafa` |
| `card` | `0xFFFF` | `#FFFFFF` | `#ffffff` |
| `border` | `0xDEFB` | `#DEDFDE` | 12 % black over white |
| `chip_off` | `0xEF7D` | `#EFEFEF` | `#bdbdbd` at 20 % |
| `text` | `0x10A2` | `#101410` | `#141414` |
| `text2` | `0x5AEB` | `#5A5D5A` | `#5e5e5e` |
| `disabled`, `dot_off` | `0xBDF7` | `#BDBEBD` | `#bdbdbd` |
| `chip_amber` | `0xFF99` | `#FFF3CE` | amber 20 % over white |
| `chip_heat` | `0xFF1A` | `#FFE3D6` | heat 20 % over white |
| `chip_steel` | `0xDF1D` | `#DEE3EF` | steel 20 % over white |
| `light_fill` | `0xFFBB` | `#FFF7DE` | amber 12 % over white |

State colours (`primary`, `amber`, `heat`, `steel`, `green`, `error`, `warning`) are the same in both themes, as in HA. The light theme suits a bright room; dark is the default because the backlight is the brightest thing in a dark room.

## 6. Typography

**Font:** Roboto, the HA body font. The current Google Fonts release is a variable font (`wght` 100-900, `wdth` 75-100) under the **SIL Open Font License 1.1** ([V] `google/fonts` `ofl/roboto/OFL.txt` and `METADATA.pb`: "Copyright 2011 The Roboto Project Authors"). Older Roboto releases were Apache 2.0 [S]; use the OFL file and ship `OFL.txt` next to the generated tables, as is done for Poppins today. Poppins stays only in the wordmark (pre-rendered bitmap).

Render offline with Pillow (`ImageFont.truetype(vf, size)`, then `set_variation_by_axes([weight, 100])`; axis order verified as Weight, Width), 4 bits of alpha per pixel (16 levels; the existing `gen_font.py` format). 2 bits (4 levels) is visibly rough on 12 px stems; 4 bpp costs 30 KB in total, so there is no reason to go lower.

| Style | Size / weight | Use | Glyphs | Measured 4 bpp size |
|---|---|---|---|---|
| `t12` | 12 / 400 | state lines, secondary text, badges, footers | ASCII + extras | 4.6 KB |
| `t14` | 14 / 500 | tile and row names, button labels, sensor values | ASCII + extras | 5.7 KB |
| `t16` | 16 / 500 | page title in the header | ASCII + extras | 7.3 KB |
| `t20` | 20 / 400 | detail values (spare; not used in the mockups) | ASCII + extras | 10.2 KB |
| `t28` | 28 / 400 | big numbers: target temperature, brightness | `0-9 . , : % + - − ° C K` and space | 2.8 KB |

Extras (all sizes except 28): `° · − … µ ² ³ ± č š ž Č Š Ž ć Ć đ Đ ä ö ü Ä Ö Ü ß é` (entity names can be Slovenian or German). The table needs a codepoint index (sorted `u16` list, binary search) and a UTF-8 decoder in `gui_text`. Digits get tabular advances (all digits as wide as `0`) so values do not jitter as they change. Sizes measured on 2026-11-07 with the Roboto variable font, glyph boxes plus 8 bytes of metrics each.

Rules: sentence case everywhere; no letter spacing; at most three sizes on one page (the header title does not count); units in `text2` after a thin space, e.g. `21.5` then `°C`; never centre body text except inside a button.

## 7. Icons

Material Design Icons, `@mdi/svg` 7.4.47, Apache 2.0. Rasterised by `tools/gui-assets/gen_icons.py` (standard library only: SVG path parser with arcs, nonzero fill, 16 sub-scanlines per pixel with exact horizontal coverage). Default 2 bits per pixel (4 levels): crisp at 24 px, smooth enough at 20 and 32. Sizes: **24 px** in tile chips and list rows (the native grid, sharpest), **20 px** in badges, sensor strips and slider labels, **32 px** spare for a large state icon.

    npm pack @mdi/svg@7.4.47 && tar xzf mdi-svg-7.4.47.tgz       # once, any machine with npm; gives package/svg/
    python3 -I tools/gui-assets/gen_icons.py --svg-dir package/svg --sizes 20,24,32 --bpp 2 \
        --header build/gui-assets/icon_data.h --sheet build/gui-assets/icons.png

54 icons at three sizes is 27 000 bytes at 2 bpp (measured); 1 bpp at 24 px is 3.9 KB but loses the curves. Ship only the size each icon is used at (section 9).

| Use | mdi names |
|---|---|
| Lights | `lightbulb` (on), `lightbulb-outline` (off), `lightbulb-off-outline`, `ceiling-light`, `desk-lamp`, `floor-lamp`, `led-strip-variant` |
| Climate | `thermostat`, `radiator`, `radiator-off`, `fire`, `air-conditioner`, `snowflake`, `thermometer` |
| Sensors, presence | `water-percent`, `molecule-co2`, `motion-sensor`, `motion-sensor-off`, `account`, `account-outline` |
| Areas | `home`, `home-outline`, `chair-rolling`, `sofa`, `bed`, `silverware-fork-knife` |
| Controls | `power`, `minus`, `plus`, `check`, `close`, `chevron-left`, `chevron-right`, `arrow-left`, `brightness-6`, `white-balance-sunny`, `palette`, `cog` |
| Device tools | `crosshairs-gps`, `sync`, `bluetooth`, `microphone`, `volume-high`, `volume-off`, `usb`, `harddisk`, `restart`, `battery`, `battery-charging`, `wifi`, `wifi-off` |
| States | `alert-circle-outline`, `cloud-off-outline`, `information-outline` |

The entity's own `icon` attribute from HA should win when it names an icon in the table; otherwise fall back by domain and device class as HA does (light `lightbulb`, climate `thermostat`, CO2 `molecule-co2`, temperature `thermometer`, humidity `water-percent`, occupancy/motion `motion-sensor`).

## 8. Layout and components

### Grid

- Screen 480x272. Outer margin 8 px, gap 8 px (HA's section gap) [V]. Content area x 8..472, y 40..248.
- Header y 0..32. Page dots centred on y 260.
- Columns used by the mockups: two tile columns of 146 px (x 8 and 162) plus a 156 px side column (x 316); or two 228 px halves (x 8 and 244); or full width 464.
- Every coordinate is a multiple of 2 and most of 4; radii 12 (cards, tiles), 10 (buttons), 8 (slider tracks, swatches), full circle (chips), 16 (badges).

### Header (exact geometry)

| Element | Geometry |
|---|---|
| Strip | Pixel rows 0..31, same colour as the page (`bg`), no bottom border. Horizontal axis: between rows 15 and 16 (15.5) |
| Logo mark | 18x18 box at x 12, y 7, drawn by `gui_mark(12, 7, 18, mark)`; ink rows 8..23, columns 13..28 |
| Wordmark | `homtom`, Poppins SemiBold 14 px, letter spacing -0.045 em (-0.63 px), colour `text2`, baseline y 21 (glyphs sit on row 20), left x 35. Ink rows 10..20, columns 36..92. Pre-render it once as a 4 bpp bitmap (57x11, under 400 bytes) instead of shipping Poppins |
| Separator | 1x14 px at x 98, rows 9..22, colour `border` |
| Page title | Roboto 16/500, `text`, x 108, baseline y 22; ink rows 9..22 (ascenders included) |
| Right side | Clock, Roboto 14/500, `text2`, right-aligned to x 468, baseline 21. When the HA link is down: `cloud-off-outline` 20 px at y 6 and `Offline 4 min` in 12/500 `warning`, right-aligned to 468, replacing the clock |
| Detail pages | `arrow-left` 24 px at x 12, y 4 (touch target x 0..44, y 0..40), title at x 48 baseline 22, area name right-aligned in `text2` |

Measured in `docs/ui-mockups/office.png` (ink bounding boxes): mark, separator, title and clock are centred on 15.5 exactly; the wordmark on 15.0, half a pixel high, which reads as centred for a lowercase word.

### Tile (generic, after `ha-tile-*`)

- Card 146x76 (or wider), radius 12, `card` fill, 1 px `border`.
- Chip: 36 px circle centred at (x + 30, y + h/2), fill = state chip colour, 24 px icon in the state colour.
- Text at x + 58: name 14/500 baseline at centre - 3; state 12/400 baseline at centre + 14; both `text` (HA uses the primary text colour for both lines). Truncate with `…` at the card's right padding (8 px).
- Names drop the area prefix on an area page (`Office Ambient` shows as `Ambient` on the Office page), as HA does for entities in an area context.
- The whole tile is the touch target (76 px high, 146 wide). Tap toggles; a long press (600 ms) opens the detail page.

### Light tile

- As the tile, plus the brightness as a fill: the left `pct` % of the tile's inner width in `light_fill`, clipped to the rounded rectangle, hard edge, no handle. 0 % or off: no fill.
- On: chip `chip_amber`, icon `lightbulb` in `amber`, state `72 %`. Off: chip `chip_off`, icon `lightbulb-outline` in `text2`, state `Off`.
- RGB lights: icon and chip use the light's colour with HA's saturation rule (section 3), quantised once.

### Thermostat card

- 156x208, radius 12. Top: tile row (chip `radiator` or `thermostat` in the HVAC action colour: `heat` heating, `cool` cooling, `text2` idle/off) with name and HVAC action (`Heating`, `Idle`, `Off`).
- Middle: caption `Target` 12/400 `text2`, value 28/400 `text` plus `°C` 14/400 `text2`, then `Now 22.4 °C` 12/400 `text2`.
- Bottom: two 62x40 buttons (`minus`, `plus`, 24 px icons), radius 10, `chip_off`, 8 px apart, 12 px from the card edges. Each press changes the target by the entity's step (0.5 °C default) at once on screen; the service call is sent 1 s after the last press (debounce), and the value shows in `text2` until HA confirms it.

### Sensor strip (read-only values)

- One card 300x40 split into three cells by 1x20 dividers. Each cell: 20 px icon (state colour) at cell x + 12, value 14/500 at x + 38, baseline 233. No captions: the icon says what it is.
- Not touchable. Read-only values are never styled as buttons.

### Badges (overview)

- 32 px high, radius 16, `card` with 1 px `border`; 24 px chip with a 20 px icon, label 12/500 at chip + 18. Only for summaries (`3 lights on`, `Heating`, `Home`), at most three on a page.

### List rows (overview areas, tools)

- Row 42 px (areas) or 68 px (tools, two lines plus a button); dividers 1 px `border`, inset to the text column.
- Icon 24 px `text2` at x + 12, title 14/500, second line 12/400 `text2`, trailing facts or a 56x38 text button (radius 10, `chip_off`, label 14/500 `primary`; destructive labels in `error`), chevron 20 px `text2` when the row opens a page.

### Slider (HA control slider)

- 40 px high, radius 8, track `chip_off`, fill in the entity colour (`amber` for brightness), white 4x30 px handle 5 px inside the fill end. Leading 20 px icon outside the track at x 12; track x 44..472.
- Touch: the whole 40 px band plus 8 px above and below. The value follows the finger; the service call is sent on release, and at most every 300 ms while dragging.
- Colour temperature: same geometry with a warm-to-cool gradient (2000 K `#FF8B14` to 6500 K `#F3F2FF`), dithered; handle white with a 1 px `bg` outline so it shows on the light end.

### Switch

- 44x24 track, radius 12. On: `primary` track, white 18 px knob at x + 23. Off: `dot_off` track, `text2` knob at x + 3. The touch target is the whole row, never only the switch.

### Page dots

- 6 px dots, 12 px apart (centre to centre), centred horizontally on y 260; active `text`, others `dot_off`. Dots are not buttons; a swipe of more than 60 px changes page, and a tap in the left or right 40 px of the dot strip does too.

### States

| State | Look |
|---|---|
| On | Chip in the state chip colour, icon in the state colour, light fill for brightness |
| Off | Chip `chip_off`, icon outline variant in `text2`, state text `Off` |
| Unavailable | Icon `disabled`, name and state `disabled`, state text `Unavailable`; the tile still opens its detail page, taps do not send anything |
| Pressed | On touch-down, fill the tile with `pressed_amber` (lights) or `pressed` (everything else) at once; restore on release. No ripple, no scale |
| Pending | After a tap and before HA confirms (up to 3 s), show the new state in `text2`; if HA does not confirm, revert and show `No response` for 3 s in the state line |
| Stale or offline | When no update has arrived for 60 s: header shows `cloud-off-outline` and `Offline N min` in `warning`; every state colour drops to `text2`, chips to `chip_off`, controls draw in `disabled`; values stay, so the last reading is visible but plainly old. Taps show `Offline` in the state line and send nothing |
| No value | `No reading` in `text2` (never `--`, `null` or `0`) |
| Error with a reason | One line in the state slot, sentence case, what happened (`Thermostat did not respond`) |

### Motion

None, except feedback that the finger caused: pressed fill on touch-down and the slider following the finger. Page changes are instant (a full-screen redraw is cheap, a slide animation is not on this CPU). No pulsing, no fades, no spinners; a pending state is text.

### Do and do not on this screen

Do:
- Spend colour only on state: amber for lights on, deep orange for heating, steel blue for sensor icons, primary blue for things you press. A page with everything off is almost grey, and that is correct.
- Make one thing dominant per page (the thermostat number, the brightness slider) and keep the rest smaller.
- Group read-only values into one strip; give cards only to things you act on.
- Use HA's names, icons and state words (`Heating`, `Unavailable`, `Detected`) so the panel and the HA app read the same.
- Show stale data as stale: grey it and say how old it is.
- Keep tiles 76 px high and buttons 40 px or more; test every target with a stylus and a fingertip.
- Use tabular digits so numbers do not move as they change.

Do not:
- No gradients (except the colour temperature track), glows, shadows, glass, or outlines plus shadows.
- No all-caps labels, letter-spaced captions or pill badges for every value.
- No bright filled navigation buttons; navigation is dots and swipe.
- No status dot that is always green; nothing in the header unless it is news.
- No emoji, no decorative icons, no icon next to a heading for the sake of it.
- No centred body text, no identical cards in a symmetric grid.
- No `--`, `N/A` or `0` for a missing value; say `No reading` or `Unavailable`.
- No animation the user did not cause.

## 9. Implementation plan (for the renderer)

Budgets: the S3C2443 runs an ARM920T core; the cycle counts below are estimates to confirm with a timer on the device, not measurements. The existing renderer redraws the full 480x272 frame and blits about 255 KB to uncached memory on every change (`workspace/gui/README.md`).

### Assets (host side)

- [x] Vendor nothing large into the repo: add a fetch step (e.g. `tools/gui-assets/fetch.sh`) that downloads `@mdi/svg@7.4.47` with `npm pack` and Roboto from `google/fonts` `ofl/roboto` into `build/gui-assets/` (ignored by git), pinned by SHA-256.
- [x] Extend `gen_font.py` (or a new `gen_font_roboto.py`) for the five Roboto styles in section 6: variation axes, extra codepoints with a sorted index, tabular digits, Poppins wordmark as one bitmap. Keep the OFL notices in the generated header. Measured total: 30.6 KB.
- [x] `gen_icons.py --icons <list> --sizes 24` and a second run for the 20 px set; only the icon and size pairs in use. About 40 pairs at 2 bpp: about 6 KB.
- [x] Regenerate the tables in the build, not by hand; add the generators' hashes to the test goldens.

### Renderer (`gui.c`)

- [x] `gui_text`: UTF-8 decode, codepoint lookup, 4 bpp alpha blit with `gui_mix`; skip fully transparent nibbles, store fully opaque ones directly. Cost: about 15 cycles per covered pixel; a busy page has about 15 000 glyph pixels, about 0.25 M cycles.
- [x] `gui_icon(id, x, y, colour)`: 2 bpp blit, values 0 (skip), 3 (store), 1 and 2 (blend at 5/16 and 11/16). Under 600 pixels per 24 px icon.
- [x] `gui_chip(cx, cy, d, bg, icon, fg)`: anti-aliased circle (existing `gui_circle`) plus icon.
- [x] Tokens as a `const u16` table per theme, indexed by role (section 5); widgets take roles, not colours. Theme switch = pointer swap and full redraw.
- [x] `gui_rrect_border`: 12 px radius card with a 1 px border in one pass (avoid drawing the card twice). Done as `gui_card`, in two passes (border shape, then the fill inset by 1 px); the cost is small next to the blit.
- [x] Clip rectangle for the light-tile fill inside the rounded tile (existing `gui_clip`).
- [x] Dithered horizontal gradient for the colour temperature track: per column colour from a 3-stop table, Bayer 4x4 threshold before truncating to 565. One 428x40 rectangle, about 17 000 pixels.
- [x] Tabular digits: font table flag, or force the advance of `0-9` to the width of `0` at generation time.
- [x] Text truncation with `…` given a maximum width.

### Widgets and screens (`ui.c` / `cfg.c`)

- [x] Tile, light tile, thermostat card, sensor strip, list row, text button, slider, switch, page dots, header, as in section 8, with the on, off, unavailable, pressed, pending and stale looks (derived from the states, not a stored enum). Not done: badges (no page uses them since the overview was replaced by the System hub).
- [x] Page model: area pages (the Office sample has 3 light tiles, the Vent tile, a strip of 3 readings and the thermostat card); detail page per light; the Tools page became the System hub with subpages (user change). Not done: the overview page with area rows (`home.png`).
- [x] Swipe: horizontal travel over 60 px between touch-down and release changes page; shorter travel is a tap at the touch-down point. Resistive panels jitter: filter with the existing debounce and ignore moves under 6 px.
- [x] Pressed state drawn on touch-down within one frame; redraw only the tile's rectangle if dirty-rectangle blit is added.
- [x] Stale logic: one timestamp of the last HA update; at 60 s switch the page to the stale style.
- [ ] Optional dirty rectangles (not done: full redraws, as before; measure on the device first): tile-sized blits instead of 255 KB per change. Worth it for the slider drag (a 464x56 band is 52 KB).

### Sizes and costs (estimates unless marked measured)

| Item | Flash (binary) | RAM |
|---|---|---|
| Roboto tables, five styles | 30.6 KB (measured) | 0 (const) |
| Wordmark bitmap | under 0.4 KB | 0 |
| Icons, about 40 pairs at 2 bpp | about 6 KB (27 KB for all 54 x 3) | 0 |
| Token tables, two themes | under 0.2 KB | 0 |
| New widget code | 6-10 KB | under 1 KB of state |
| Back buffer | | 261 KB (exists today) |

| Operation | Estimate |
|---|---|
| Full page draw (fills, about 10 rounded cards, 15 000 glyph pixels, 12 icons) | 2-4 M cycles, i.e. 5-20 ms depending on the clock |
| Full blit, 255 KB to uncached memory | to be measured; likely the larger cost |
| Tile redraw on press | under 0.2 M cycles plus a 146x76 blit (22 KB) |

### Tests

- [x] Host render of every page (the light theme only for the Office page) and all tile states, hashed in `tests/golden.sha256`; compare against `docs/ui-mockups/` by eye on the first pass.
- [ ] (open) On the device: `tools/homtom-screen` screenshot of each page; check text and icon edges at 2x zoom, and touch every target with a fingertip and a stylus.
- [ ] (open) Measure the full-redraw and blit time on the device (log `uptime` deltas around 100 redraws) before deciding on dirty rectangles.

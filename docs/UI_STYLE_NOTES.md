# UI style notes: renderer against the mockups

Status: 2026-11-07, host renders only (nothing of the restyle has run on the device). Spec: `docs/UI_DESIGN.md`; mockups: `docs/ui-mockups/`; config and behaviour: `docs/UI_CONFIG.md`. Renders: `workspace/gui/out/test/*.png` after `workspace/gui/build.sh test` (hashes in `workspace/gui/tests/golden.sha256`).

This lists where the device renderer (`workspace/gui/gui.c`, `cfg.c`) differs from the mockups and from the spec, and why. Everything not listed follows the spec's geometry and tokens; the Office page and the light page were compared with the mockups side by side at 1x and 2x.

## Pages compared

| Mockup | Render | Result |
|---|---|---|
| `office.png` | `out/test/office-0-office.png` (`conf/office.conf`) | Same geometry: tiles 146 x 76 at x 8 and 162, the strip 300 x 40 at y 208, the thermostat card 156 x 208 at x 316, the header elements on their axis. Differences below (Vent tile, values from the sample states) |
| `office-light-theme.png` | `out/test/office-light.png` | Same tokens |
| `office-offline.png` | `out/test/ha-offline.png` | Same look; `Offline 5 min` (the test uses a 5 minute old file) |
| `light-detail.png` | `out/test/office-1-light_amb.png`, `office-2-light_hue1.png`, `ha-more.png` | Same rows (y 88, 136, 184), swatches, `More`, footer. Ambient has no colour, so no swatch row |
| `home.png` | none | Replaced: the user asked for a System hub instead of an overview (`out/test/full-0-system.png`) |
| `tools.png` | `out/test/full-2-gps.png`, `full-3-bluetooth.png`, `full-4-audio.png`, `full-5-device.png` | The same row style (icon, title, status, text button, switch, red destructive label), split over the hub's subpages |

## Deviations

| Item | Mockup or spec | Renderer | Why |
|---|---|---|---|
| Header right side | Clock only; "nothing in the header unless it is news" | Gear (area pages, when the config has a hub), clock, battery indicator (22 x 11 outline, level fill green from 40 %, amber from 15 %, red below, bolt while charging, `55%`) | User request after using the unit. The old uptime text and status dot are gone |
| Clock source | not specified | Device clock plus offset: `INTERNAL:/homtom/tz`, else `sys.tz_off` from the Mac, else `ui tz=`, else UTC; hidden before 2026 | User request |
| Wordmark | Poppins SemiBold 14 px, x 35, baseline 21, 57 x 11 ink | 16 px, baseline 20, 65 x 13 ink; separator and title move right to x 108 and 118 | User asked for 15 to 16 px next to the 18 px mark, x-height centred on the mark's axis (15.5). Checked against `brand/splash/homtom-splash-semibold.png`: ink width to height 5.42 here, 5.35 in the splash; x-height to height 0.75 against 0.77; an overlay scaled to the same width agrees except for 1 px anti-aliasing fringes (mean difference 19 of 255 per pixel). Colour: bone `#F7F3EF` (RGB565 `0xF79D`), dark theme; text colour in the light theme |
| Office content | Four lights | Three lights and a Vent tile (`fan` icon, `25 min ago` from the slot's `age`, long press opens the Vent page) | User request: IR buttons, one tile per area |
| Thermostat chip icon | `radiator` | `radiator`; `snowflake` when cooling | Cooling needs its own icon |
| Thermostat units | `°C` 14/400 | `°C` in the 14/500 font (`text2`) | The 14 px style is generated at 500 only; a 400 copy would cost 5 KB for one glyph pair |
| Badges 12/500, offline label 12/500 | 12/500 | 12/400 | Same reason: one 12 px weight |
| Light page brightness unit | `%` 16/400 | `%` 16/500 | Same reason |
| Light page Back | (spec) arrow in the header, and the old in-widget Back button | Header arrow only | The widget's own Back would sit on the big number |
| Swatch row | 7 swatches, `More` | 7 swatches; `More` swaps them for the hue strip (`Colours` swaps back). Swatches send `set_hue_sat` (warm white 30,46; cool white 240,5; red, orange, green, blue, purple) | The old 12 swatches and the separate hue strip do not fit under the colour temperature row. A light shown in colour temperature mode rings warm white below 4000 K and cool white from 5000 K |
| `Changed ... ago` | `Changed 4 min ago by the wall switch` | `Changed 4 min ago` | The states file has the age, not the cause |
| Light description | `Warm white, 3000 K` | Under 3400 K warm, under 5000 K neutral, else cool white, with the kelvin; a hue name for colour lights | The mockup shows one case |
| Charging bolt | not in the spec | Two triangles with a 1 px outline in the page colour, not anti-aliased | Readable on the green fill at 1x; an icon would need a third size |
| Card border | One pass | Border shape, then the fill inset by 1 px | Simpler; the cost is small next to the 255 KB blit |
| Grid configs | Mockup layouts use explicit columns (146, 156) | `conf/office.conf` uses `x= y= w= h=`; generated configs keep the 4 x 3 grid (110 x 64 cells), where tiles use a 28 px chip and readings share a strip | The generator's grid is not the mockup's; the renderer adapts instead of rejecting it |
| Sensor strip | A widget of its own | Read-only entities side by side on one row become one strip; a name line under the value from 56 px high | No config change needed; the generator's sensors line up already |
| Unknown entity class | HA domain and device class | Guessed from the states fields: light (`bri`/`ct`/`hue`), climate (`target`/`mode`), button (`tap=press`), presence (read-only on/off: `Detected`/`Clear`), sensor by unit; an `icon` field wins | The states file carries no domain or device class |
| Offline taps | "Taps show `Offline` and send nothing" | Taps still send (the events wait for the Mac) | Keeps the event contract unchanged; whether a delayed toggle is wanted is the user's call |
| Pending | Show the new state in `text2`, `No response` after 3 s | The new state shows in `text2` until the states file confirms (5 s, then the file's value again); no `No response` text | The existing optimistic logic; a reply text would need one more timer |
| Motion | Pressed fill at once | Pressed fill at once; no ripple, no animation | As spec |
| Dirty rectangles | Optional | Not done; every change redraws the frame | Measure the redraw and blit time on the device first |
| Light theme | Every page | Every page supports it (`ui theme=light`); the tests render Office only | Coverage |
| Tabs | Not in the spec (dots only) | `nav=tabs`/`both`: area page names as text tabs in the header, 2..5 pages, active one with a primary underline | The generator writes `nav=both`; this keeps it meaningful without a bottom tab row |
| Device actions | `tools.png`: one USB storage row | A Power subpage of Device: Restart, Power off, USB storage, each with a confirmation (red action button) | User request; three destructive actions get their own page, away from the information |
| Hold hints | not in the spec | A centred card over a dimmed screen: `Release to power off` (amber), `Restarting…` (red), also on a locked screen | User request |
| Page dots | Always on area pages | Only with two or more swipe neighbours; never on the hub or subpages | User request |

## Device-side unknowns

Dark greys on the TN panel (the spec's card is one step lighter than HA), Roboto's 12 px stems, the dithered gradient, and the touch size of the 36 x 40 compact thermostat buttons are not verified on the unit (`docs/HA_UI.md` section 6).

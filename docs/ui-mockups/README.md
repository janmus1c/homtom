# UI mockups (480x272)

Mockups for `docs/UI_DESIGN.md`, 2026-11-07. Every PNG is exactly 480x272 and already quantised to RGB565, so the colours are the ones the panel can show. Sample names and values are invented.

| File | Shows |
|---|---|
| `office.png` | Office area page, dark theme: four light tiles (two on with brightness fill, one off, one unavailable), sensor strip (occupancy detected, CO2, temperature), thermostat card (heating, target 21.5 °C, minus and plus), header with mark, wordmark and page title, page dots (page 2 of 6) |
| `office-light-theme.png` | The same page in the light theme |
| `office-offline.png` | The same page when the HA link has been silent for 4 minutes: warning in the header, state colours removed, controls disabled, last values kept |
| `light-detail.png` | Light detail page: back arrow, brightness 72 % with power button, brightness slider, colour temperature slider (dithered gradient), favourite colour swatches with the current one ringed, `More` button, last-changed footer |
| `home.png` | Overview: three summary badges, area list (Office, Living room, Bedroom, Kitchen) with lights-on count, temperature and a `No reading` case |
| `tools.png` | Tools page: GPS receiver switch, GPS assist, Bluetooth pairing, microphone test, test tone, USB storage restart (destructive label in red) |

## How they are made

`tools/gui-assets/mockups.py` describes each page with the same pixel geometry as the spec (rectangles, text baselines, icon positions). It writes an absolutely positioned HTML page with Roboto (variable font, SIL OFL 1.1) and the Poppins wordmark embedded, and the icons as the 2 bpp bitmaps from `tools/gui-assets/gen_icons.py`, so icon pixels are the ones the device tables will hold. Headless Google Chrome takes the 480x272 screenshot; the script then decodes it with the Python standard library and quantises every pixel to RGB565, with a 4x4 ordered dither only inside the colour temperature track.

    npm pack @mdi/svg@7.4.47 && tar xzf mdi-svg-7.4.47.tgz
    curl -L -o Roboto-VF.ttf 'https://raw.githubusercontent.com/google/fonts/main/ofl/roboto/Roboto%5Bwdth,wght%5D.ttf'
    python3 -I tools/gui-assets/mockups.py --roboto Roboto-VF.ttf --svg-dir package/svg

Differences from the device: glyph edges come from Chrome's grayscale anti-aliasing (the device will use 16-level glyph bitmaps from the same font), and the logo mark is the brand SVG rather than `gui_mark`. Layout, colours, icons and sizes are the intended ones.

## Against the renderer

`docs/UI_STYLE_NOTES.md` compares these mockups with the device renderer's host renders (`workspace/gui/out/test/` after `workspace/gui/build.sh test`) and lists every deviation and its reason; the overview (`home.png`) and the Tools layout (`tools.png`) were replaced by the System hub and its subpages at the user request.

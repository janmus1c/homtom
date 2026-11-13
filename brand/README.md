# Brand

The mark is a house drawn as a plain outline with a navigation arrow inside:
TomTom with one letter changed: the old navigator now runs the home. Round line ends, one
stroke weight, no fill, gradients or shadows.

| File | Use |
|---|---|
| `homtom-icon.svg` | App icon, boot banner source, README: mark on the blue tile (100×100, 6 px radius) |
| `homtom-mark.svg` | Mark in `currentColor`, for the on-device interface |

| Colour | Hex | Role |
|---|---|---|
| Home blue | `#18BCF2` | Tile background, primary accent (Home Assistant blue) |
| Bone | `#F5F3EE` | Mark on green, light surfaces |
| Ink | `#0E161E` | Text, dark surfaces and splash background |

The name is always lowercase: **homtom**.

The wordmark is the name in lowercase, set in Poppins (SIL Open Font License
1.1, `fonts/`) with -0.045em letter-spacing, centred under the mark.

## Boot splash

`render-splash.swift` draws the 480×272 splash with CoreText and writes a PNG
preview and the raw RGB565 framebuffer image the device shows at boot:

    swift brand/render-splash.swift brand/fonts/Poppins-Light.ttf 18BCF2 brand/splash/homtom-splash-light

`splash/` holds one variant per Poppins weight. The firmware ships
`homtom-splash-semibold.fb` as `/etc/homtom/splash.fb`.

# Seeing the device screen from the Mac

Screenshots, a live view and pixel diffs of the 480x272 LCD, for debugging and UI work. Everything is read-only on the device: the tool reads the LCD registers and the framebuffer through `/dev/mem` and never writes device memory. Verified on the live device on 2026-11-07; the sample is `.runtime/screen/sample-touch-slider.png`, captured while `homtom-touch` showed its slider test.

## Quick use

    tools/homtom-screen                      # .runtime/screen/shot.png (and shot.hts), about 2-4 s
    tools/homtom-screen -z 2                 # 960x544 PNG, nearest neighbour
    tools/homtom-screen --diff               # new shot, diffed against the previous one -> shot.diff.png
    tools/homtom-screen --watch 1            # live view at http://127.0.0.1:8731/ and .runtime/screen/live.png
    tools/homtom-screen --info               # LCD registers and derived geometry
    tools/homtom-screen --diff A.png B.hts   # offline diff of two images (no device)
    tools/homtom-screen --convert X.hts -o X.png

On first use (or when the local binary changes size) `homtom-screen` uploads `tools/devtools/homtom-shot` to `/tmp/homtom-shot` with `homtom-put`. `/tmp` is RAM, so a reboot removes it. It checks `homtom-state` first and stops unless the device is in `LINUX`.

For Claude on the Mac, the useful loop is: run `tools/homtom-screen` (or keep `--watch` running), then read `.runtime/screen/shot.png` or `live.png` as an image. `--diff` prints the dirty rectangle in text, which is often enough on its own.

## How the framebuffer is found

There is no `/dev/fb0`. The GO 630 scans out through the legacy S3C24xx LCD controller at `0x4D000000` (the TFT block at `0x4C800000` is unused on this board; `docs/emulator-plan.md`). `homtom-shot` maps that register page `PROT_READ` and derives:

| Register | Field | Live value | Meaning |
|---|---|---|---|
| LCDCON1 `+0x00` | ENVID bit 0, BPPMODE bits 4:1 | `0x00940579` | output on, 16 bpp TFT |
| LCDCON2 `+0x04` | LINEVAL bits 23:14 | `0x0043c08a` | 272 lines |
| LCDCON3 `+0x08` | HOZVAL bits 18:8 | `0x0169df07` | 480 pixels |
| LCDCON5 `+0x10` | FRM565 bit 11, HWSWP bit 0 | `0x00014b19` | RGB565, halfwords in little-endian order |
| LCDSADDR1 `+0x14` | address bits 30:1 | `0x19f80000` | framebuffer at `0x33F00000` |
| LCDSADDR3 `+0x1C` | PAGEWIDTH + OFFSIZE (halfwords) | `0x000001e0` | stride 960 bytes |
| TPAL `+0x50` | bit 24 | `0x00000000` | no solid-colour overlay |

The kernel moves the scan-out buffer to `0x33F00000` in `homtom_reserve()` (`kernel/homtom/mach-homtom-go630.c`), so that is also the fallback when the registers look implausible (not 16 bpp, size above 800x480, or address outside RAM). The fallback, an active TPAL overlay and a disabled LCD are flagged in the file header and printed as warnings, because in those cases the PNG is not what the panel shows. In loop mode the registers are re-read every frame, so a moved buffer is followed.

The framebuffer is mapped with `O_SYNC` (uncached), so the shot sees exactly what the LCD DMA reads, including writes from `homtom-gui`, `homtom-touch` and the kernel text console.

## Device tool: `homtom-shot`

Source `tools/devtools/homtom-shot.c`, built with `tools/devtools/build.sh homtom-shot` (10 KB, freestanding ARMv4T, no libc or libgcc, no divisions).

    homtom-shot [-o FILE] [-f hts|raw|bmp] [-s 1|2|4] [-l MS [-t S]] [-i]

- `-o FILE` output (default stdout). Files are written as `FILE.tmp` and renamed, so FTP never reads half a frame.
- `-f hts` (default): 24-byte header plus run-length RGB565. `raw`: same header plus plain RGB565. `bmp`: 24-bit BMP for any viewer.
- `-s 2|4` box-average downscale on the device.
- `-l MS` loop every MS milliseconds (minimum 100), `-t S` stop after S seconds (default 300, maximum 3600). A loop always ends on its own.
- `-i` print registers and geometry only.

HTS header, little endian: `"HTS1"`, u16 width, u16 height, u8 format (0 raw, 1 RLE), u8 scale, u16 flags (bit 0 TPAL on, bit 1 LCD off, bit 2 fallback geometry), u32 TPAL, u32 framebuffer address, u32 frame sequence. RLE: a control byte `c < 128` is followed by `c+1` literal pixels; `c >= 128` by one pixel repeated `c-126` times.

Measured sizes: kernel text console about 62 KB, the touch slider screen 6.7 KB, `-s 4` of the console 11 KB, against 261 KB raw. The RLE and raw output of the same instant decoded to identical pixels.

## Mac tool: `homtom-screen`

`tools/homtom-screen`, Python standard library only (`struct`, `zlib`, `http.server`), run as `python3 -I`. It runs the device tool through `homtom-sh`, fetches `/tmp/homtom-shot.hts` over FTP, and writes a PNG. It keeps the `.hts` next to the PNG (`shot.hts`, previous as `shot.prev.hts`) so diffs are exact and do not depend on `--zoom`.

`--watch SECONDS` starts `homtom-shot -l` in the background on the device writing `/tmp/homtom-live.hts`, polls that file over FTP, and serves:

- `/` a page that swaps in each new frame and outlines the dirty rectangle in cyan,
- `/frame.png` the latest frame, `/meta.json` frame number, text and dirty rectangle.

The server binds `127.0.0.1` only (port 8731, or a free port if that is taken). Frames whose pixels did not change are skipped. On exit (Ctrl-C or `--for S`, default 600 s) it kills the device loop by PID, after checking `/proc/PID/cmdline` names `homtom-shot`, and removes the live file. The device loop has its own cap of `--for` plus 5 s, so a lost Mac process cannot leave it running.

`--diff` writes a dimmed copy of the new frame with changed pixels in magenta and the dirty rectangle outlined in cyan, and prints `changed N px, dirty rect x= y= w= h=`. It reads `.hts`, 24-bit `.bmp` and 8-bit RGB/RGBA `.png`; both images must have the same size (use unzoomed PNGs).

## Limitations

- Frame rate is limited by the link, not the device. One FTP fetch of a 6-60 KB frame took 0.2 s to 3 s on 2026-11-07, almost all of it connection setup jitter (a busy `tdbg2` touch test was running at the time). Expect 0.3-1 frame/s in `--watch`, and 2-4 s per one-shot (one `homtom-sh` round trip with its fixed 2 s wait, plus FTP).
- A shot is not synchronised to the LCD refresh or to the drawing program, so a frame taken during a blit can show a partly drawn screen.
- Backlight level and panel power are not captured; a dark panel with a valid framebuffer still produces a normal image. TPAL and ENVID are reported.
- No input: the live view does not send touches. That would be a device write path and is out of scope here.
- RLE does little for photographic content; worst case is raw size plus 1/128.

## A true live view (design note, not implemented)

A VNC (RFB) server on the device is possible but poor value. RFB needs a persistent TCP listener, pixel-format negotiation, a framebuffer update loop and, to be useful, input injection; written freestanding it is a few thousand lines, and it would compete for the single ARM920T core with the program under test. The image would still be limited by the USB link.

The cheaper path keeps the device side as it is and moves intelligence to the Mac:

1. Persistent stream instead of FTP per frame. A `homtom-shot -l MS -o -` piped into a single long-lived TCP connection (for example started from the 2323 shell, or a dedicated `tcpsvd` port added to `init.sh` later) removes the per-frame connection setup that dominates today. Each frame is already self-delimiting (header plus RLE that decodes to exactly w*h pixels).
2. Dirty rectangles on the device. Keep the previous frame in RAM (261 KB), compare row by row, and send only the bounding box (or a few row bands) with an `x, y, w, h` header. Static UI screens then cost a few hundred bytes per frame, and frames with no change cost nothing. The Mac already computes the same rectangle in `dirty()`, so the protocol can be tested Mac-side first.
3. Optionally, a Mac-side RFB bridge: a small Python RFB server on `127.0.0.1` that feeds the decoded frames to any VNC client (Screen Sharing, TigerVNC). That gives a VNC experience without any RFB code on the device. Touch from the VNC client would need a device-side input path that does not exist yet.

Both 1 and 2 need device-side changes to the init script or a new listener; neither is required for screenshots.

## Rendering UI to PNG without the device

`workspace/gui` already builds the same `gui.c`/`ui.c` for macOS with a PNG writer in place of the framebuffer (`workspace/gui/README.md`):

    workspace/gui/build.sh preview                                   # out/home.png, gps.png, system.png
    workspace/gui/out/preview -f workspace/gui/sample.txt -p 2 -t 100,240 -o x.png

That is the right loop for layout and styling: no device, no upload, sub-second rebuilds. To check that the device draws what the preview draws, render the same page and data on both and compare:

    tools/homtom-screen --diff workspace/gui/out/home.png .runtime/screen/shot.hts

Both sides go through RGB565, so identical drawing code with identical input gives "no change". Differences then come from data (clock, sensor values), touch calibration or the device blit, not from the drawing code. The preview PNG must be rendered without `-z` for the sizes to match.

Two small extensions would close the remaining gap: an option in `host.c` to write `back[]` as an HTS file (24-byte header, format 0, raw RGB565) for byte-exact comparisons without PNG, and a tiny screen-by-screen test that renders every page from fixed `sample.txt` inputs and diffs against stored golden PNGs.

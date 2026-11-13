# Audio playback through DMA (homtom-audio)

Status 2026-11-07: built and host-tested, not yet run on the device.

Tags: [V] verified in this project (source read, build output, host emulation). [H] hypothesis that needs a device test. "User-reported" marks an observation made at the device that this project has not measured itself.

## Problem

`homtom-snd2` feeds the S3C2443 IIS TX FIFO from userland by polling. The FIFO holds 16 frames, about 0.34 ms at 46875 Hz, so any scheduling gap longer than that underruns and distorts the tone. SCHED_FIFO priority cut the gap sidebands by about 20 dB but cannot remove them (`docs/LESSONS.md`).

## Decision

A 14 KB out-of-tree module, `homtom-audio.ko`, drives one S3C DMA channel directly in auto-reload mode over a ring buffer. The hardware loops the ring with no CPU work and no gap between laps. A 1-jiffy timer zeroes what the DMA has already fetched and wakes the writer. No DMA interrupt and no `dmaengine` call is needed. Userland writes 16-bit stereo frames to `/dev/homtom-audio`. The default output rate stays at 46875 Hz, the rate homtom-snd2 already uses, and `homtom-play` resamples 44.1 and 48 kHz input to it. A codec-master mode that gives exactly 48000 Hz (or 44118 Hz) from the same 12 MHz clock is included as an experiment [H].

## Options considered

### (a) dmaengine (`s3c24xx-dma`) plus a small client module

- `CONFIG_S3C24XX_DMAC=y` and `CONFIG_DMA_ENGINE=y` are set in the shipped quiet tree, and the board file registers `s3c2443_device_dma` (`kernel/homtom/mach-homtom-go630.c:195`) [V].
- The platform data has the I2S TX channel: `DMACH_I2S_OUT = { APB, handshake, request source 4 }`, slave map `"s3c24xx-iis", "tx"` (`arch/arm/mach-s3c/s3c24xx.c:575,594`) [V]. Request source 4 matches stock `S3C_REQSEL_IIS_TX (4<<1)` [V].
- Blocker: for S3C2443 the driver sets `has_clocks = true` and calls `devm_clk_get(dev, "dma.N")` per channel (`drivers/dma/s3c24xx-dma.c:1158-1162,1251-1257`). `clk-s3c2443.c` registers the gates `dma0`..`dma5` but no `dma.N` lookups for `s3c2443-dma.0`, so every channel fails the clock lookup and stays `valid = false`; `s3c24xx_dma_get_phy()` skips invalid channels [V by source]. No 6.1 dmesg from the device is in the repo, so the expected log line ("unable to acquire clock for channel N") is [H]; test step 2 checks it.
- `s3c24xx_dma_filter` is not exported (absent from `vmlinux.symvers`), and the slave map names `s3c24xx-iis`, which a module can match only by naming its own device that way [V].
- Cyclic transfers in this driver use `NORELOAD` and restart each period from the DMA interrupt (`s3c24xx-dma.c:421,667-683`), so every period boundary still depends on interrupt latency staying under the 16-frame FIFO [V by source].
- Fix: `kernel/patches/audio/0001-clk-s3c2443-dma-channel-clock-lookups.patch` adds the six `dma.N` aliases. It needs a new zImage. It is optional and not required by the chosen design; it applies cleanly to the quiet tree (dry run) but has not been built or booted.

### (a') One DMA channel driven directly (chosen)

- Works on the running kernel with no image change, because the dmaengine driver holds no channel and has no client on this board: UART DMA is DT-only (`samsung_tty.c:2054-2060`), SDHCI and HSUDC use their own DMA, MMC/SDI DMA options are off [V].
- Auto-reload over the whole ring is what the stock 2.6.13 driver relied on (`S3C_DMAF_LOOPING`, `drivers/barcelona/sound/dma.c:216-226`) [V by source]; here no chunk interrupt is used at all.
- Cost: 64 KiB ring (coherent memory), about 100 short timer callbacks per second while open.

### (b) No DMA: FIFO interrupt, kernel thread, hrtimer or FIQ

- The S3C2443 IIS has no FIFO-level interrupt: the 2412-style IISCON has no interrupt enable bits (the underrun interrupt exists only from S3C64xx on, `regs-i2s-v2.h:30-31`), and the S3C2443 interrupt list has no IIS source (`irqs-s3c24xx.h:168-213`) [V]. A FIFO-level interrupt design is not possible.
- `HIGH_RES_TIMERS` is off and HZ is 100, so an hrtimer or kernel thread cannot wake every 0.3 ms [V].
- A FIQ from a spare PWM timer at about 4 kHz, refilling 8 frames per FIQ, would work but costs about 4000 interrupts per second and a hand-written FIQ handler. DMA is strictly better here.

### (c) ASoC

- Needs `SOUND`, `SND`, `SND_SOC`, the generic dmaengine PCM, `s3c2412-i2s` (driver name `s3c2412-iis`), `snd-soc-wm8750` over SPI (so `SPI_GPIO` and `SPI_BITBANG` for the write-only 3-wire bus), a machine driver modelled on `jive_wm8750.c`, ALSA userland, the clock patch above, plus a slave-map entry for `s3c2412-iis` and an `iis` clock lookup for that device name (the existing lookup is for `samsung-i2s.0`) [V by source]. The kernel side alone is an estimated 150-250 KB of code [H, not built], against 14 KB for (a'). Rejected for now; worth revisiting only if mixing, capture through ALSA or standard players are needed.

## Sample rate

### What the hardware does in homtom-snd2's setup

- `CLKSRC` was read as `0xD0` (`docs/PERF.md:60`) [V]: bits 8:7 = 01 (EPLL reference = MPLL reference), bit 3 = 0 (MPLL reference = crystal), bit 6 = 1 (ESYSCLK = EPLL). The crystal is 12 MHz (`s3c2443_init_clocks(12000000)`, consistent with MPLLCON `0x805B0300` giving 792 MHz and the measured 396/132/66 MHz) [V].
- homtom-snd2 sets `CLKSRC[15:14] = 2`, which selects the EPLL reference for the I2S clock (`clk-s3c2443.c:65,72`) [V]. So I2SCLK = 12 MHz.
- `IISMOD = 0x480`: master, I2SCLK source (IMS = 01), TX only, LR polarity bit set (as stock), I2S format, 256fs, 32fs bit clock, 16-bit. `IISPSR = 0x8000`: prescaler on, divide by 1. CDCLK (codec MCLK) = 12 MHz.
- fs = 12 000 000 / 256 = 46875 Hz. EPLL is not in this path at all, so it does not need to change.

### The measurements

- Lab: the PIO writer measured 43.7 kHz with FIFO stalls, and capture counted about 44k frames/s (`docs/LESSONS.md`). Both are lower bounds: frames lost while the process is descheduled lower the count, nothing can raise it above fs.
- User-reported: about 35 kHz. This conflicts with the two lower bounds and with the clock arithmetic, unless that measurement counted gaps or a pitch shift caused by underruns. It is not explained yet.
- The module settles it: the DMA advances exactly one frame per LRCK period, and `status` reports `fs_measured` from DMA progress over wall time (about 0.1 % accuracy after 10 s), independent of userland scheduling. `audio-rec.swift ... scan` gives an acoustic cross-check. If `fs_measured` disagrees with 46875, the status line also prints `CLKSRC`, `CLKDIV1`, `EPLLCON` and the EPLL rate, which locate the cause; play with `--rate <fs_measured>` meanwhile.

### Dividers for 44.1 and 48 kHz without touching EPLL

With the SoC as I2S master the only clean sources are the 12 MHz reference and PCLK (66 MHz). Best combinations over RFS 256/384/512/768 and prescaler 1-64:

| Source | 44100 target | 48000 target |
|---|---|---|
| 12 MHz, 256fs, /1 | 46875 (+6.3 %) | 46875 (-2.3 %) |
| PCLK 66 MHz, 256fs, /6 | 42969 (-2.6 %) | 42969 (-10.5 %) |
| PCLK 66 MHz, 256fs, /5 | | 51563 (+7.4 %) |

None is usable as 44.1 or 48 kHz. The I2S divider path (`CLKSRC[15:14] = 0`, ESYSCLK / I2SDIV) depends on the EPLL rate the bootloader set, which also clocks the moviNAND controller; it is not used. So:

1. Default (mode 0): keep 46875 Hz and resample in userland. `homtom-play` does linear interpolation in fixed point; 44.1 and 48 kHz WAVs play at the right pitch (host-tested, below). Codec setup is byte-identical to homtom-snd2 (R7 = 0x02 slave, R8 = 0x20 normal mode 256fs).
2. Experimental (mode 1, `--codec-master 48000|44100`): the SoC keeps sending 12 MHz on CDCLK but becomes I2S slave (IMS = 11); the WM8750 becomes master in USB mode, where 12 MHz gives exactly 48000 Hz (MCLK/250, R8 = 0x01) or 44118 Hz (MCLK/272, R8 = 0x23). These R8 values are the stock driver's 12 MHz table (`wm8971.c:278-316`), and R7 = 0xC2 (master, BCLK inverted) is what stock wrote for S3C2443 with the codec as master (`wm8971.c:395-404`) [V by source]. That the S3C2443 still outputs CDCLK from I2SCLK in slave mode, and accepts the codec's bit clock, is [H]: stock's own slave path used the PCLK source instead (`iis-s3c2443.c:74-77,126-133`). If it fails, the DMA does not advance (`frames` stays 0); mode 0 is unaffected.

## Design

### Data path

`homtom-play` (resample, mono mix) writes to `/dev/homtom-audio`, then copies into a 64 KiB coherent ring. DMA channel 5 (auto-reload, handshake, word, request source 4) moves the ring to IISTXD, the IIS FIFO sends it to the WM8750, and the codec drives the amplifier and speaker.

- Frame format: 4 bytes, left sample in bits 15:0, right in bits 31:16 (little-endian interleaved S16). Which half the IIS sends as "left" is [H]; it does not matter for the speaker because the default mono mix puts the same sample in both halves. The speaker sits across LOUT2/ROUT2 with ROUT2 inverted (R24 = 0x10), so it hears L+R; `--stereo` keeps channels apart for headphones.
- Ring: 64 KiB by default = 16384 frames = 350 ms at 46875 Hz. Power of two, 8-256 KiB (`ring_kb=`), halves itself if the allocation fails.
- Position: the timer reads the channel's current source address (`DCSRC`) every jiffy (10 ms). Bytes fetched since the last tick are zeroed, so any part of the ring that userland has not refilled plays as silence instead of a stale lap.
- Writer: `write()` blocks until space exists (or returns `-EAGAIN` with `O_NONBLOCK`). Space is counted from the last zeroed position, minus a 1 KiB guard. If the DMA has overtaken the write position, the next write counts an underrun and restarts 4 KiB (about 22 ms) ahead of the DMA. The timer never moves the write position, so it cannot race the copy.
- `fsync()` waits until everything written has been fetched (bounded by ring length + 0.5 s). `close()` does the same unless a signal is pending, then stops IIS and DMA.
- Single open. `open()` refuses with `EBUSY` if IISCON shows the IIS already active (homtom-snd2 running, override with `force=1`) or the DMA channel is already on.

### Interfaces

- `/dev/homtom-audio`: misc device, mode 0600. `homtom-play` creates the node from `/sys/class/misc/homtom-audio/dev` if devtmpfs did not.
- `/sys/class/misc/homtom-audio/status`: one line of counters (`running mode ch ring fs_nominal fs_measured meas_ms frames queued underruns bad_pos epll_hz pclk_hz`) and one line of registers (`CLKSRC CLKDIV1 EPLLCON HCLKCON IISCON IISMOD IISPSR DCON DSTAT DCSRC DMASKTRIG`).
- Module parameters: `ch` (0-5, default 5), `ring_kb` (default 64), `mode` (0 or 1, read at open, writable in `/sys/module/homtom_audio/parameters/`), `demand` (DMA demand mode as stock 2.6.13 instead of mainline's handshake), `force`.

### Split of duties

The module owns the IIS block, its clock gates and mux, GPE0-4, and the DMA channel. `homtom-play` owns DAC power (GPJ15), the codec over the L3 bit-bang (GPB2-4) and the amplifier (GPG11), with the same masked writes, table and order as homtom-snd2. Sequence: power up DAC, open the device (MCLK and silence start), wait 20 ms, codec init, amplifier on, stream, `fsync`, amplifier off and codec mute, read status, close (IIS and DMA stop), GPE4 low, DAC power off. SIGINT, SIGTERM and SIGHUP run the same shutdown.

## Register map (everything the module writes)

| Register | Address | Write | Notes |
|---|---|---|---|
| HCLKCON | 0x4C000030 | bit `ch` set (masked) | DMA channel bus clock; gate is CLK_IGNORE_UNUSED in CCF |
| PCLKCON | 0x4C000034 | bit 9 set (masked) | IIS bus clock, as homtom-snd2 |
| SCLKCON | 0x4C000038 | bit 9 set (masked) | I2SCLK gate, as homtom-snd2 |
| CLKSRC | 0x4C000020 | bits 15:14 = 2 (masked) | I2S clock = EPLL reference (12 MHz), as homtom-snd2 |
| GPECON | 0x56000040 | bits 9:0 = 0x2AA (masked) | GPE0-4 = I2S LRCK, SCLK, CDCLK, SDI, SDO |
| IISCON | 0x55000000 | 0, then 0x5 | I2S active + TX DMA active; 0 on close |
| IISMOD | 0x55000004 | 0x480 (mode 0), 0xC80 (mode 1) | master/slave, I2SCLK source, TX only, 256fs, 32fs, 16-bit |
| IISFIC | 0x55000008 | 0x8000 then 0 | TX FIFO flush |
| IISPSR | 0x5500000C | 0x8000 | prescaler on, divide by 1 |
| DISRC | 0x4B000000 + 0x100·ch | ring physical address | |
| DISRCC | +0x04 | 0 | AHB, increment |
| DIDST | +0x08 | 0x55000010 | IISTXD |
| DIDSTC | +0x0C | 3 | APB, fixed |
| DCON | +0x10 | 0x80200000 \| (ring/4) | handshake, PCLK sync, single service, unit, word, auto-reload, no interrupt |
| DMAREQSEL | +0x24 | 0x9 | hardware request, source 4 = IIS TX |
| DMASKTRIG | +0x20 | 0x2 to start, 0x4 to stop | |

Read only: `EPLLCON`, `CLKDIV1`, `DCSRC`, `DSTAT`. Never written: EPLLCON, CLKSRC bits 8:6, CLKDIV1, INFORM0, GPJ (module), GPL, any NOR or storage path. `homtom-play` keeps homtom-snd2's GPIO guard (no GPJ10/14, no GPL).

## Files and build artefacts

| Path | What |
|---|---|
| `kernel/patches/audio/homtom-audio/homtom-audio.c`, `Makefile` | module source |
| `kernel/patches/audio/build.sh` | clones `.runtime/linux-trim-quiet` to `.runtime/linux-trim-audio` (APFS clone, created once), checks the `.config` is identical, builds in Debian bookworm with `gcc-arm-linux-gnueabi` 12.2 like `build-modules-tree.sh`, fails on compiler warnings (W=1) |
| `kernel/patches/audio/0001-clk-s3c2443-dma-channel-clock-lookups.patch` | optional dmaengine fix, not needed, not built |
| `artifacts/audio-dma/homtom-audio.ko` | 14264 bytes, sha256 `306393a8814c588ce0e93230bbbf1634ed00f5d70259db04048eec0236a20640` |
| `artifacts/audio-dma/CONFIG_SHA` | `832fec49…6899af`, same as `artifacts/modules-trim/CONFIG_SHA` |
| `tools/devtools/homtom-play.c`, `homtom-play` | player, built with `tools/devtools/build.sh homtom-play`; binary 10396 bytes, sha256 `d7413967866291a6ca04252ae5430d36665094f86a71a53dc91ec73fe20ad77d` |
| `tools/host/audio-rec.swift` | new optional `scan` argument: strongest frequency near FREQ |

Checks done in the project:

- The module builds without warnings at W=1. All 39 imported symbols are exported by the shipped kernel's `vmlinux.symvers`, and `vermagic` (`6.1.191 mod_unload ARMv4 p2v8`) equals the shipped `homtom-sdi.ko` [V].
- `homtom-play` ran under `qemu-arm` with a fake `/dev/mem`, a fake status file and `/dev/homtom-audio` as a capture file [V]:
  - 44.1 kHz stereo, 48 kHz stereo, 22.05 kHz mono, 16 kHz raw mono and stdin input all came out at 46875 Hz with the input pitch preserved (1000/500/440/700 Hz peaks).
  - `--stereo` kept 500 Hz left and 3000 Hz right in the low and high halfwords.
  - `--codec-master 48000` wrote mode 1 and produced 48000 frames per second.
  - Bad arguments and a missing module are refused before any GPIO write.
  - Only GPBCON/GPBDAT, GPECON/GPEDAT, GPGCON/GPGDAT and GPJCON/GPJDAT were written.
- Not done: nothing has run on the device. Module behaviour on hardware (DMA start, `DCSRC` tracking, auto-reload) is [H] until the steps below pass.

## Risks

- DMA channel shared with dmaengine [H, low]: if the clock patch is ever applied and a dmaengine client appears, it could pick channel 5 while audio runs. Today there is no client. `open()` refuses a channel that is already on; use `ch=4` if 5 is busy.
- Bus or pin conflict with homtom-snd2 [V]: both drive the IIS. `open()` refuses while IISCON is active. Do not start homtom-snd2 while `/dev/homtom-audio` is open.
- Timer starvation [H, low]: if the 10 ms timer is delayed by more than the ring length (350 ms), one lap of already-played audio repeats. That needs softirqs blocked for a third of a second.
- Memory [H, low]: 64 KiB of physically contiguous memory at load. Insmod falls back to 32/16/8 KiB and logs the size it got.
- Mode 1 [H]: slave-mode CDCLK output and the 12 MHz bit clock from the codec are unproven on this SoC. Worst case: no sound and `frames=0`; `close()` then waits about 1 s and stops.
- Amplifier left on [V for the code path]: a SIGKILL of `homtom-play` skips its shutdown; the module still stops IIS and DMA on close, but GPG11 stays high. Recover with `homtom-snd2 --secs 0` or `devmem`.
- Loudness: same codec volume as homtom-snd2; `homtom-play` refuses volumes above 117 (stock level). Resampled full-scale WAVs can be louder than the parabolic test tone (peak 8192); start with `--vol 90`.
- LR order on headphones [H]: if left and right are swapped, it only matters with `--stereo`.

## Test steps on the device

Run from the repo root on the Mac with the device in LINUX state. `/tmp` on the device is RAM, so repeat the upload after a reboot. Nothing here writes storage or changes the image.

1. Upload both files:

   ```sh
   tools/homtom-put artifacts/audio-dma/homtom-audio.ko /tmp/homtom-audio.ko
   tools/homtom-put tools/devtools/homtom-play /tmp/homtom-play
   tools/homtom-sh 'chmod 755 /tmp/homtom-play; sha256sum /tmp/homtom-audio.ko /tmp/homtom-play'
   ```

   Compare with the hashes above.
2. Check the dmaengine hypothesis (read only): `tools/homtom-sh 'dmesg | grep -i -e dma -e s3c2443-dma'`. Expected [H]: six "unable to acquire clock for channel N" lines and "Loaded dma driver with 6 physical channels".
3. Load: `tools/homtom-sh 'insmod /tmp/homtom-audio.ko && dmesg | tail -3 && ls -l /dev/homtom-audio'`. The log line gives the ring size, EPLL and PCLK rates and `epllref 12000000 Hz`. If `/dev/homtom-audio` is missing, homtom-play creates it.
4. Silent DMA test: codec unpowered, amplifier off, as homtom-snd2 `--probe`:

   ```sh
   tools/homtom-sh '/tmp/homtom-play --silent-test 10 > /tmp/a1.txt 2>&1; cat /tmp/a1.txt' 15
   ```

   Pass: `frames` about 470000, `fs_measured` within 0.2 % of `fs_nominal=46875`, `underruns=0`, `bad_pos=0`, `DMASKTRIG=00000000` after close. Record `fs_measured`: it settles the sample-rate question. If `frames=0`: DMA did not start; send the register line.
5. Tone and pitch: open the recorder first, then play 6 s at moderate volume:

   ```sh
   (tools/homtom-sh '/tmp/homtom-play --tone 1000 --secs 6 --vol 90 > /tmp/a2.txt 2>&1' 12 &)
   sleep 2; swift tools/host/audio-rec.swift 3 1000 scan
   tools/homtom-sh 'cat /tmp/a2.txt'
   ```

   Pass: `strongest` within 1 Hz of 1000 (`ratio` 1.000), `underruns=0`. A ratio of about 0.75 would reproduce the user-reported 35 kHz and contradict `fs_measured`; send both outputs.
6. Glitch test under load: repeat step 5 while two busy loops run, then compare with homtom-snd2 under the same load:

   ```sh
   tools/homtom-sh '(while :; do :; done) & (while :; do :; done) & sleep 14; kill %1 %2' 16 &
   ```

   Run step 5 within those 14 s, then the same with `/bin/homtom-snd2 --hz 1000 --secs 6 --vol 90 --iiscon 1`. Pass: `underruns=0` and `tone_1000Hz_db` close to `rms_db + 3` for homtom-play (a pure tone), with a clearly larger gap for homtom-snd2.
7. Music: convert any file on the Mac, upload, play:

   ```sh
   afconvert -f WAVE -d LEI16@44100 -c 2 song.m4a /tmp/song.wav
   tools/homtom-put /tmp/song.wav /tmp/song.wav
   tools/homtom-sh '/tmp/homtom-play --vol 90 /tmp/song.wav > /tmp/a3.txt 2>&1' 30
   ```

   Keep the file short (device RAM is about 40 MB free). Streaming works too: `wget -O - http://HOST/song.wav | /tmp/homtom-play -`.
8. Interrupt test: start step 7 and run `tools/homtom-sh 'killall homtom-play'` after a few seconds. Pass: the sound stops, the output says "stopped by signal", the amplifier is off (`devmem 0x56000064` bit 11 = 0).
9. Optional, mode 1 [H]: `/tmp/homtom-play --codec-master 48000 --tone 1000 --secs 4 --vol 90`, recorded as in step 5. Pass: 1000 Hz and `frames` about 192000. If it is silent or `frames=0`, try `--bclkinv 0` once, then stop.
10. Unload: `tools/homtom-sh 'rmmod homtom_audio; dmesg | tail -2'`.

Send back the status lines from steps 4, 5, 6 and 9 and the recorder lines. Write the result into `docs/LESSONS.md` and turn the [H] tags above into [V] or corrections.

## Integration notes

- Ship `homtom-audio.ko` only next to a zImage built from the same `.config` (`CONFIG_SHA` above). Rebuild with `kernel/patches/audio/build.sh` whenever the quiet tree changes.
- To make the beep glitch-free, switch the UI `beep` action (`docs/UI_CONFIG.md`) from `homtom-snd2` to `homtom-play --tone HZ --secs 1 --vol RAW` once step 6 passes, and load the module at boot.

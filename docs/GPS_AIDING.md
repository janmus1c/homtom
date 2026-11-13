# GPS aiding for the SiRF III (U6)

Goal: shorten time to first fix on the GO 630 GPS (SiRF Star III on `ttySAC2`, NMEA at 4800 baud) under Linux 6.1. This page covers what stock TomTom software did, a minimal aiding design we can run from the Mac or Home Assistant, the `gpsaid` tool, and the device test plan.

Marks: [V] verified against an artifact or a lab measurement named next to it. [H] hypothesis (including protocol details recalled from the SiRF reference manuals, which are not in this repo). [U] reported by the user, not measured here.

Evidence: `artifacts/gps-aiding-20261107/` (local, gitignored) with `SHA256SUMS`, `README.txt`, the stock rc scripts, string dumps and annotated disassembly excerpts of `clmapp` and `sirfreset`.

## 1. Sources

| Item | Value |
|---|---|
| Stock image | `artifacts/backups/first-INTERNAL/ttsystem.bak`, SHA-256 `9259df2b…2c12`. The gzip member at file offset `0x1215c8` is the cpio initramfs; `etc/version` = `2013-04-04 16:25:58` [V] |
| `bin/clmapp` | ARM ELF, glibc, stripped, 100760 bytes, SHA-256 `07cd4771…29b6` [V] |
| `bin/sirfreset` | ARM ELF, glibc, stripped, 69600 bytes, SHA-256 `7e56d290…83e3` [V] |
| `bin/ttn` | navigation app, 8.2 MB, strings only [V] |
| OpenTom copies | `.runtime/emu/opentom/bin/rc.suspend`, `rc.restartgps` (same logic as stock, older) [V] |
| Unit flags | `/proc/barcelona/gpsephemeris` = 1, `gpstype` = 3, `gpsdev` = `ttySAC2` (`emulator/user/barcelona.txt:34-37`) [V] |

Reproduce: `zlib.decompressobj(31)` from offset `0x1215c8`, extract with `bsdtar -xf`, disassemble with `llvm-objdump -d --triple=armv4t-linux-gnueabi` (Homebrew LLVM).

## 2. What stock did

### Boot, suspend and restart flow (stock `etc/rc`, `rc.suspend`, `rc.resume`, `rc.restartgps`)

- `gpstype` 3 is not 128/129 (Hammerhead) or 130 (Barracuda/GL), so this unit takes the SiRF branch [V, script logic].
- `check_gps_receiver`: with `gpsephemeris=1`, runs `/bin/sirfreset -p -v -osirf &` (switch the receiver to SiRF binary); otherwise `-onmea` [V].
- `start_clm`: waits for `sirfreset`, copies `/mnt/sdcard/clmdata` to `/var/run/clmdata`, starts `clmapp &` [V].
- Suspend: copies `/var/run/clmdata` to `/mnt/sdcard/` (the SIGUSR1 handshake with `clmapp` via `/tmp/clmquit` is commented out in the 2013 stock script, active in the OpenTom copy) [V].
- Resume: `sirfreset -v -osirf` [V]. GPS restart: SIGUSR1 to `clmapp`, `sirfreset -rsnap -A -v -osirf`, SIGUSR2 to `clmapp` [V].
- `cbee_gen`, `glgps`, `gltt`, `lto.dat` and `/mnt/sdcard/ephem/cbee.cbee` belong to the Global Locate chips (gpstype 128-130). Not this unit [V].

### `sirfreset` (protocol switcher and reset tool)

- SiRF binary framing: `A0 A2`, 2-byte big-endian length, payload, 2-byte checksum, `B0 B3` (writer at `0xf350`). Checksum = 16-bit sum of payload bytes, low 15 bits (`0xfc38`) [V].
- NMEA to SiRF binary: `$PSRF100,0,57600,8,1,0` (call at `0xec68`); stock's binary mode is 57600 baud ("ForceMessage: SiRF 57600 already configured") [V].
- SiRF binary to NMEA: canned MID 129 payload at rodata `0x17b33`: `81 02 01 01 00 01 01 01 05 01 01 01 00 01 00 01 00 00 00 01 00 00 12 C0`. That is GGA 1 s, GLL off, GSA 1 s, GSV 5 s, RMC 1 s, VTG/MSS/ZDA off, 4800 baud [V bytes; field names H].
- Reset, binary: MID 128, 25 bytes, all position, drift, time and week fields zero, byte 23 = 12 channels, byte 24 = reset bitmap: snap `0x00`, hot `0x40` (help text: "discard RTC correlation"), warm `0x02`, cold `0x04`, factory `0x08`, `|0x10` nav-lib data, `|0x20` debug data, and bit 7 always set (`0xf634`) [V].
- Reset, NMEA: `$PSRF101,0,0,0,0,0,0,12,N` with N = 1/2/4/8 for hot/warm/cold/factory (`0xe328`) [V]. So the firmware generation stock shipped against accepts SiRF NMEA input sentences.
- Autodetect probe order (rodata `0x14ea8`, read as baud/protocol pairs): NMEA 4800 first, then NMEA 115200, SiRF 57600, SiRF 4800, … [V bytes; pair reading H].
- Other canned frames: MID 152 poll (`98 00`), a MID 14 almanac record for SV 3, and a builder for `0x95` + 90 bytes (MID 149 Set Ephemeris [H]) that nothing calls [V].
- Writes the receiver firmware version to `/var/run/gps_version.txt` [V]. We have never recorded our receiver's version.

### `clmapp` (extended ephemeris client)

- Identity: "CLM" engine `1.0-PROD-C1-B027-Linux-ARM-R`, built with GCC 3.3.4, statically contains zlib inflate 1.1.3 [V]. [H] This is SiRF's Client Location Manager from the SiRFInstantFix (server-generated extended ephemeris) product.
- Port: `/dev/gpsdata` (rc links it to `ttySAC2`), alternatives `/dev/ttySAC0` and `/dev/null`; baud defaults to 57600 when none is given (`moveq r4, #57600` at `0x125b4`) [V].
- NMEA for the navigator: creates FIFO `/var/run/gpspipe` and contains `$GPGGA/$GPRMC/$GPGLL/$GPGSA/$GPVTG/$GPGSV` format strings [V]; `ttn` reads `/var/run/gpspipe` ("Read GPS From pipe from GPS provider") [V]. [H, strong] Under stock the receiver stayed in SiRF binary and `clmapp` synthesized the NMEA that `ttn` used.
- Input file: `/mnt/sdcard/ephem/packedEphemeris.ee`, which it calls a "SEA file". The header holds generation, compaction, post-compaction, compression and encryption method fields, prediction interval, GPS week, GPS day of week, GMT year/month/date/seconds and a header checksum; the body is per-PRN segments, zlib-compressed and encrypted, plus an iono checksum and an encryption trailer [V strings]. Cipher and key were not analysed [H: inside `clmapp`].
- Receiver protocol, from the log dispatcher at `0x104e8-0x10578` (key = sub-ID << 8 | message ID) [V]:

  | Key | Message | Log string |
  |---|---|---|
  | `0x0138` | MID 56 (0x38) sub 1, output | ephemeris request received: gps_time_valid, gps_week, gps_tow |
  | `0x0338` | MID 56 sub 3, output | ephemeris state received |
  | `0xFF38` | MID 56 sub 255, output | ack received: msg_id, sub_id |
  | `0x01E8` | MID 232 (0xE8) sub 1, input | ephemeris response sent: num_sv, gps_week |
  | `0xFFE8` | MID 232 sub 255, input | debug command sent: debug_flags |

  MID 56 sub 2 = integrity warning is [H] (string present, key not traced). NMEA equivalents are compiled in too: `$PSRF151` (request), `$PSRF152` (integrity), `$PSRF154` (ack), `$PSRF107` (ephemeris input, about 27 or 54 numeric fields), `$PSRF108` (debug), `$PSRF110` [V strings; mapping H].
- Flow [H from the strings]: the receiver asks for ephemeris (MID 56/1 with week, TOW and a mask); `clmapp` decodes the SEA file for those satellites and answers with MID 232/1; it cannot answer until it knows the host-clock-to-GPS-time offset ("host rtc offset not known").
- Persistence: `/var/run/clmdata` holds the CLM storage (stored file date, GPS week/day, prediction interval, a version that is checked) [V strings]. It is CLM state, not receiver state.

### Who downloaded the file

- `clmapp` contains no URL and no HTTP code; it only logs "download: started; local_file=%s" [V, absence in strings].
- `ttn` contains `packedephemeris.ee`, `ee_meta.txt`, `ephem`, `Expiry=%d`, `CEphemerisMonitor`, `KFeaturePlusEphemeris`, and the generic TomTom directory URLs `http://t.tt1.nl/services/directory.php` and `http://t.tt1.nl/proxy/directory.php` [V].
- [H] TomTom HOME on a PC (QuickGPSfix) or the PLUS service wrote `ephem/packedephemeris.ee` and `ee_meta.txt` to the card, with service endpoints resolved through that directory. No ephemeris server URL appears in any local artifact. [H] The service is gone; do not plan around it.

## 3. Lab measurement

2026-11-07, one read-only GET of the status endpoint at uptime 1286 s, only time/date/status fields extracted (`artifacts/gps-aiding-20261107/measurement.txt`) [V]:

- RMC status `V`, date `030906` (2006-09-03), time `00:21:07`; GGA fix quality 0, 0 satellites.
- The receiver clock started at 00:00 when `homtom-hw gps-on` powered and reset it at boot, so it holds no time across boots. Every boot is at least a cold start. Indoors it had no fix (matches [U] "no fix indoors").

Consequences [H]:

- Week rollover. 2006-09-03 is the start of GPS week 1391. If the firmware resolves the 10-bit week against that date, its window ended on 2026-04-19 and it may now report dates in 2007 even with a fix. It may also reject or misread a full week number (2443 in November 2026) in an init message. The tool has `--week-offset -1024` to test that.
- Almanac. Any factory almanac is about 20 years old. Position and time aiding only helps if the receiver also has a usable almanac, which it collects in about 12.5 min of continuous tracking and loses at the next GPS power cycle.
- Aiding does not create signal. It shortens the search; a fix indoors still needs enough signal. Ephemeris aiding (level 3 below) is what lets a receiver fix without decoding the 50 bps navigation data, the weak-signal bottleneck.

## 4. Aiding design

Inputs come from the Mac or Home Assistant at run time: approximate position (the `zone.home` coordinates rounded to 0.01 degree are enough [H: the receiver needs tens of km, not metres]) and UTC. They are passed on the command line and never stored in the repo.

| Level | What | How | Status |
|---|---|---|---|
| 1 | Time + approximate position, warm start | `$PSRF104` in NMEA (no protocol switch), or `$PSRF101` (ECEF; stock sends this sentence) | `gpsaid` default |
| 1b | Same, SiRF binary | `$PSRF100,0,4800,8,1,0`, MID 128, stock MID 129 back to NMEA 4800 | `gpsaid --mode sirf` |
| 2 | Current almanac | MID 130 Set Almanac [H] from a public YUMA/SEM almanac downloaded on the Mac; keep the GPS powered across reboots if possible | not started |
| 3 | Ephemeris (hot start, weak signal) | MID 149 Set Ephemeris [H] from broadcast ephemeris (RINEX nav) re-encoded to subframe words, or answer MID 56/1 with MID 232/1 like `clmapp` | research |

Protocol facts used by level 1:

| Fact | Source |
|---|---|
| Binary frame `A0 A2 len payload ck B0 B3`, ck = sum & 0x7FFF | [V] `sirfreset` `0xf350`, `0xfc38`; also the manual's MID 128 example checksum `0x0A91` recomputes exactly |
| MID 128 is 25 bytes: ID, ECEF X/Y/Z int32 m, clock drift int32 Hz, TOW uint32 in 1/100 s, week uint16, channels, reset bitmap (big-endian) | [V] length, byte 23/24 from `sirfreset`; field units [H] from the manual, consistent with its example (X -2686727, Y -4304282, Z 3851642 is the SiRF San Jose site our ECEF code reproduces within 10 m) |
| Reset bitmap bit 0 = init data valid, bit 1 = clear ephemeris (warm), bit 7 set as stock | bit 1 and bit 7 [V]; bit 0 [H] |
| `$PSRF100,0,9600,8,1,0*0C` | [H] manual example; our NMEA checksum reproduces `0C` |
| `$PSRF104,lat,lon,alt,clkdrift,TOW,week,channels,resetcfg`; resetcfg 1 hot, 2 warm, 3 warm with init, 4 cold, 8 factory | [H] SiRF NMEA reference; 1/2/4/8 [V] in `sirfreset` |
| Clock drift 0 = keep the receiver's last value or its default | [H]; stock always sends 0 [V] |
| MID 129 payload for NMEA 4800 | [V] stock bytes |

Safe parameters: 12 channels, drift 0, warm start with init (NMEA 3, binary `0x83`) or hot with init (NMEA 1, binary `0x81`). `gpsaid` cannot send cold or factory resets, firmware-load messages, or anything that touches `GPS_REPRO` (GPJ8) or other GPIOs.

### Sharing the port with `homtom-statusd`

`homtom-statusd` opens `/dev/ttySAC2` read-only, sets raw 4800 8N1 with output processing off, and is the only reader [V, `workspace/statusd/homtom-statusd.c:252-270`]. A tty accepts more than one opener, so `gpsaid` opens it write-only and never reads. Without `--set-4800` it does not change the termios, so `statusd` keeps its settings. Replies from the receiver therefore go to `statusd`, and confirmation comes from the NMEA that `statusd` already publishes (RMC date and time).

- NMEA modes: the port stays NMEA 4800 throughout; `statusd` sees one receiver reset.
- `--mode sirf`: the receiver talks SiRF binary at 4800 for about 3 s. `statusd` drops those bytes (no `$`, failed checksums) and resumes with the next NMEA line [H, parser design].
- `gpsaid` refuses to send if the port is not at 4800 (for example `statusd` is not running), and refuses binary frames if output post-processing (OPOST) is on, because ONLCR would turn every `0x0A` inside a frame into `0D 0A`.
- Exclusive ownership (stop `statusd`, read acks) is not needed for level 1. If later levels need replies, add a release switch to `statusd` (for example: close the GPS fd while `/tmp/gps.release` exists) rather than killing it.

## 5. `gpsaid` tool

`tools/devtools/gpsaid.c`, freestanding ARMv4T, built with `tools/devtools/build.sh gpsaid` (binary 10996 bytes, SHA-256 `f6997282…7a9d` at the time of writing). Usage is in the file header; dry run is the default and `--send` is required to write.

```
gpsaid --selftest
gpsaid --lat 51.4778 --lon -0.0014 --alt 46 --time 1790000000                 # dry run, $PSRF104 (dummy: Greenwich)
gpsaid --lat 51.4778 --lon -0.0014 --alt 46 --time 1790000000 --mode sirf     # dry run, binary path
gpsaid --send --lat … --lon … --time $(date +%s)                              # on the device
gpsaid --send --to-nmea                                                       # recovery: stock MID 129
```

Computation: UTC to GPS week/TOW with `--leap` (default 18 s); WGS84 geodetic to ECEF in Q30 fixed point (CORDIC sin/cos, series for the prime-vertical radius), no floating point, libc or libgcc. Ranges are checked (latitude, longitude, altitude -500..9000 m, time after 2020, week 1024..4095); a 30 s interval timer ends a stalled run.

Tests (offline, all passing):

- `workspace/gpsaid/test_gpsaid.c` (clang on the Mac): checksum equals the stock routine on 2000 random buffers and the manual example; ECEF against double precision on 20000 random points, maximum error 0.57 m; GPS time vectors; MID 128 layout against the manual example; parser edge cases; `$PSRF104` text.
- ARM binary under `docker --platform linux/arm/v7`: `--selftest` ok; dry runs for every mode; `--send` into a raw pty at 4800 wrote exactly the printed bytes (55 for NMEA, 123 for `sirf`, 32 for `--to-nmea`) and left the termios unchanged; a 9600 port and an OPOST port were refused.
- Instruction check: only ARMv4T instructions (`mul`, `mla`, `umull` are the only multiplies, no `blx`, `clz`, `ldrd`).

Not run on the device yet.

### `homtom-gps-aid`: the button on the screen

`userland/homtom-gps-aid` (BusyBox script, in `/bin`) is what the "GPS aid" button of the Tools page runs (`cmd gps-aid`, `docs/UI_CONFIG.md`). It needs nothing from the Mac at run time:

| Input | Source |
|---|---|
| Position | `INTERNAL:/homtom/gps.aid` (`/mnt/internal/homtom/gps.aid`), first line `LAT LON [ALT]`: decimal degrees and metres, for example `51.48 -0.00 46` (Greenwich, a dummy). Written by the user in storage mode or over FTP; it never goes into the repo, a commit or a log. Two decimals are enough |
| Time | The device clock (`gpsaid --time now`). The Mac bridge sets it after every boot; the script refuses while the year reads before 2026 |

Steps: validate the file (two or three numbers, nothing else), run `gpsaid --send --lat … --lon … --alt … --time now` (level 1, `$PSRF104`, warm start with init) with its output discarded because it prints the position, then watch `/tmp/rmc` (written by `homtom-statusd`) for up to 15 s for the receiver's date to become today's UTC date. `homtom-statusd` keeps running and stays the only reader; the script only reads `/tmp/rmc` and the position file. The result goes to `/tmp/ui.extra` as `gps_aid=` and to the kernel log as `homtom-gps-aid: …`:

| `gps_aid` | Meaning |
|---|---|
| `OK 12:00` | Sent at 12:00 UTC and the receiver's RMC date became today within 15 s: the receiver took the time |
| `sent 12:00` | Sent, but the RMC date did not change within 15 s (receiver ignored it, or reports a rolled-over week: check the date on the GPS page or with `cut -d, -f10 /tmp/rmc`) |
| `no gps.aid` | No position file. Time-only aiding is not available: `gpsaid` always sends a position with the time (both `$PSRF104` and MID 128 carry one), and a made-up position would misdirect the satellite search. Adding a time-only form needs a `gpsaid` change |
| `bad gps.aid` | The first line is not two or three plain numbers, or `gpsaid` rejected one (exit 2) |
| `clock unset` | The device clock reads before 2026: wait for the bridge to set it |
| `refused` | `gpsaid` exit 3: the port is not at 4800 baud (is `homtom-statusd` running?) or a value is out of range (latitude, longitude, altitude -500..9000 m) |
| `port error` / `failed N` | `gpsaid` exit 4 (cannot open or write `ttySAC2`) / any other exit code |

Only the NMEA level-1 form is offered on the screen; `--mode sirf`, `--to-nmea` and the week offset stay manual (section 6).

## 6. Device test plan

Run with the user present, one step at a time, and stop at the first surprise. Do not stop or restart `homtom-statusd` or other `homtom-*` processes. Coordinates come from HA at run time and stay out of the repo, logs and commits.

0. Check the device is up: `tools/homtom-state`. Record the baseline RMC time/date/status: `curl -s http://192.168.1.10:8080/ | python3 -c 'import json,sys; r=json.load(sys.stdin)["rmc"].split(","); print(r[1], r[2], r[9])'` (prints only time, status, date).
1. Upload and self-test:
   `tools/homtom-put tools/devtools/gpsaid /tmp/gpsaid`
   `tools/homtom-sh 'chmod +x /tmp/gpsaid; /tmp/gpsaid --selftest'` → `selftest ok 0x00`.
2. Prepare the inputs on the Mac (shell variables only):
   `LAT=…; LON=…` from HA `zone.home`, rounded to 2 decimals; `ALT=` rough metres.
3. Dry run on the device: `tools/homtom-sh "/tmp/gpsaid --lat $LAT --lon $LON --alt $ALT --time $(date +%s)"`. Check the week and TOW.
4. Send level 1 (NMEA): `tools/homtom-sh "/tmp/gpsaid --send --lat $LAT --lon $LON --alt $ALT --time $(date +%s)"` → `port speed code 0x000C`, `sent`.
   Expected within 1-2 s [H]: the RMC time and date jump to the current UTC. If they do, the receiver accepted the init.
5. If the date stays 2006 or comes back wrong, repeat step 4 with `--week-offset -1024`, then with `--mode nmea101`.
6. Only if all NMEA forms are ignored: `--mode sirf`. Afterwards NMEA must resume within 5 s at the status endpoint. If it does not: `tools/homtom-sh '/tmp/gpsaid --send --to-nmea'`. If still not, power-cycle the GPS (`homtom-hw gps-off; sleep 2; homtom-hw gps-on`; this toggles GPJ6/GPJ7 and is an user action).
7. Measure the effect near a window or outdoors: after a reboot, note the time from `gps-on` to the first GGA with fix quality > 0 (HA history of `binary_sensor.homtom_gps_fix`), once without and once with step 4 sent right after boot. Repeat a few times; one run proves little.
8. Record results in `docs/LESSONS.md` (accepted form, week handling, fix times) and keep raw logs in `artifacts/` with hashes. Drop coordinates from anything kept.

## 7. Open questions

- Receiver firmware version (stock wrote it to `/var/run/gps_version.txt`; we can get it from MID 6 once we can read replies).
- Does the GPS keep any state across a soft reboot, or does `gps-on` always reset it (GPJ7 pulse)? Skipping the reset pulse when GPJ6 is already high would keep the almanac between boots.
- Week number handling after the 2026-04-19 window end [H].
- MID 130 and MID 149 formats for levels 2 and 3, and whether the SEA cipher in `clmapp` is worth analysing (only useful if a SEA source still exists).

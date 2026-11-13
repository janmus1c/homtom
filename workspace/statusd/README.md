# homtom-statusd

One freestanding ARMv4T daemon for the status endpoint and the GPS reader. It replaces `tcpsvd 8080 homtom-status` (about 45 forks and 2.2 s per request) and `homtom-gpsd` (docs/PERF.md sections 4 and 5).

| File | Role |
|---|---|
| `statusd_core.h` | Portable core: NMEA line assembler and parser (checksum verified), ADC filter, JSON and UI-file formatters. No libc |
| `homtom-statusd.c` | Device layer: raw EABI syscalls, `/dev/mem` mmap, ADC sampler, GPS reader, HTTP server, `poll()` loop |
| `test_core.c` | Host test (clang, macOS): sample sentences, exact JSON and UI output, overflow and chunking cases, ADC filter with glitch injection |
| `build.sh` | `dev` (docker, `out/homtom-statusd`, or `out/$OUT`), `test`, `all` |

## Build and test

    ./build.sh test    # clang, runs out/test_core
    ./build.sh dev     # docker debian:bookworm-slim + gcc-arm-linux-gnueabi, 20.6 KB stripped, links with no libc, memcpy or memset
    OUT=homtom-statusd.new ./build.sh dev   # same, written to out/homtom-statusd.new

Flags: `-nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 -fno-builtin -fno-tree-loop-distribute-patterns`. The binary has no ARMv5 instructions. `__aeabi_uidiv` and `__aeabi_uidivmod` are provided in the source.

The ARM binary was also run once in an arm32 container with no `/dev/mem` and no tty: HTTP answered in 0.45 ms with null hardware fields. It has not run on the device.

## Behaviour

- Usage: `homtom-statusd [PORT [IPV4]]` (default 8080 on all addresses; give the USB address to bind only there).
- ADC sampler, every 5 s: raise GPL14 (masked 0x4000 in GPLDAT) and LX_EN (GPJCON mask 0xC, GPJDAT mask 0x2), wait 250 ms (`SETTLE_MS`, the shell used 1 s; hypothesis, validate), ask homtom-touch for the ADC (`/tmp/adc.req`, acknowledged by `/tmp/adc.ack`, at most 40 ms, no wait when `/tmp/adc.touch` names no live process), read battery mux 0, reference mux 4, light mux 2 five times each, release, then lower both pins. Each read does two conversions, keeps the second, waits for ECFLG (bit 15) after a short delay so a stale flag from the previous conversion is not taken for ready, and is dropped when ADCMUX changed meanwhile. The medians pass a plausibility filter with hold-last-good (null after 60 s of rejected cycles) and `battery_pct_est` has a 2-point hysteresis. JSON object `adc` holds the filter and arbitration counters. Protocol, windows and device check: `docs/ADC_ARBITRATION.md`.
- Hardware writes are limited to those three masked writes by an allowlist in `hw_wr()`; GPJDAT bits 10/14, GPJCON pins 10/14 and GPLCON pins 0-9 are additionally rejected by name. Nothing touches ADC clock gating.
- Inputs read live at request and UI time (reads only): charging GPG1 low, external power GPF2 low, power key GPF0 low.
- GPS: opens `/dev/ttySAC2` non-blocking, TCSETS raw 4800 8N1, read only. Reopens after an error or EOF. GGA and RMC (any talker) are accepted only with a valid checksum. Keeps fix quality, satellites, speed, last-fix uptime. Writes `/tmp/gga`, `/tmp/rmc` (CRLF like homtom-gpsd, so the old script's `${gga%?}` still works) and `/tmp/gps_ts` (uptime seconds of the last GGA with fix), each by tmp file and rename.
- `/tmp/homtom-ui.txt`: rewritten every second by rename, keys of workspace/gui/README.md (`battery_pct_est`, `charging`, `external_power`, `light_raw`, `uptime_s`, `gga`, `rmc`) plus `gps_fix`, `gps_sats`, `gps_speed_kn` (ignored by the current GUI). Unknown values are left out.
- HTTP: one request at a time (the listener is not polled while a client is active), `Connection: close`, 2 s client timeout, `MSG_NOSIGNAL` plus SIGPIPE ignored, EINTR retried. JSON fields and order are those of `userland/homtom-status`; unknown is `null` (the shell printed 0 for a missing ADC value), no sentence is `""`. Added header `Content-Length`.
- System fields (2026-11-07), appended after `rmc` and read at request time, one open/read/close each, no forks: `cpu_load` (1-min load x100 from `/proc/loadavg`), `loadavg_x100` ([1, 5, 15 min] or null), `mem_free_kb` (MemAvailable, else MemFree) and `mem_total_kb` (first 256 bytes of `/proc/meminfo`), `bt_up` (`hci0` in `/sys/class/bluetooth`; 0 when the directory is missing) and `bt_links` (number of `hci0:<handle>` entries, one per ACL connection; not yet observed with a live link), `last_touch` (`{"x","y","type","age_s"}` from `/tmp/touch.last` = `X Y down|up|move`, age from the file mtime against CLOCK_REALTIME; null when the file is missing or malformed). If `bt_links` turns out not to follow real links, the cheap alternative is homtom-btd writing the count to `/tmp/bt.links` on connection/disconnection events and the daemon reading that file.
- `/tmp/ui.extra` (2026-11-07): `key=value` lines from the tool-page wrappers (`docs/SCREEN_TOOLS.md`), re-read with every UI file write and request (one read of at most 1 KB, a last line without `\n` is ignored). Valid lines (key `[a-z0-9_]` 1..31, value printable without `"` and `\`, at most 64 characters, 16 keys, no override of the daemon's own UI keys, first of a repeated key wins) are appended to `/tmp/homtom-ui.txt` and returned as strings in the JSON object `"extra"` (always present, `{}` when empty). Buffers: UI file 1536 bytes, JSON body 2048, response 2304. `homtom-btd` reads up to 3 KB of the response for its RFCOMM `status` (it read 1 KB before).
- `passive` third argument (`homtom-statusd 8081 0.0.0.0 passive`): no ADC sampling, no GPS reader, no file writes; GPIO inputs and system fields only. For testing a new build beside the live daemon.
- Never exits on errors: listener bind is retried every 2 s (so it can start while the old tcpsvd still holds the port), the tty every 5 s, `/dev/mem` mapping every sample.
- Not in scope: the watchdog feeder and the heartbeat stay in init.sh; GPS power and reset stay with `homtom-hw gps-on`.

## Integration plan

1. Add `out/homtom-statusd` to the image next to the other binaries (`kernel/scripts/build-ttsystem-6.1.sh` line 49 copies `/out/homtom-*` files into `$ROOTFS/bin`; add a `cp` or build step for it).
2. In init.sh replace the two supervised lines

       ( sv homtom-gpsd > /dev/null 2>&1 ) &
       ( sv tcpsvd 0.0.0.0 8080 /bin/homtom-status ) &

   with a gated start that keeps the scripts as the fallback:

       homtom-hw gps-on
       if [ -x /bin/homtom-statusd ] && [ ! -e /etc/homtom/no-statusd ]; then
           ( sv homtom-statusd ) &
       else
           ( sv homtom-gpsd > /dev/null 2>&1 ) &
           ( sv tcpsvd 0.0.0.0 8080 /bin/homtom-status ) &
       fi

   `homtom-gpsd` runs `homtom-hw gps-on` itself, so call it once before the daemon in the new branch. Fallback switch: create `/etc/homtom/no-statusd` on the image (or remove the binary) and reboot.
3. Keep `/bin/homtom-status` in the image: `tools/devtools/homtom-btd.c` executes it for its status JSON (5 s timeout). Later point btd at `127.0.0.1:8080` (or share `sd_json`) so it also stops forking.
4. Never run the old and new endpoints together on the live device (both use the ADC mux and GPL14). The daemon shares the ADC only with a `homtom-touch` that speaks the request/ack protocol (`docs/ADC_ARBITRATION.md`); deploy the two binaries together.
5. First device check, one change at a time: `curl -s http://192.168.1.10:8080/`, compare `battery_raw`/`battery_ref_raw`/`light_raw` with the shell values and check the `SETTLE_MS` assumption (raise it if the reference reads low); then `cat /tmp/homtom-ui.txt`, then the GUI. Expected request time under 10 ms and no forks per request.

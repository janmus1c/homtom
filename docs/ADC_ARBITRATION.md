# ADC sharing between homtom-statusd and homtom-touch

Status 2026-11-07: built for ARMv4T and host-tested, not yet run on the device.

Tags: [V] verified in this project (source read, build output, host test). [H] hypothesis that needs a device test. "User-reported" marks an observation made at the device that this project has not measured itself.

## Problem

- User-reported: a few times per minute the battery reading jumps to 0 % for one update and returns to 100 %, and `light_raw` jumps to about 1000 and returns to about 4.
- Measured earlier in this project: the live endpoint returned `battery_ref_raw` 1023 three times around uptime 453-465 s and then 380 steadily, while a second statusd instance ran in passive mode and never touched the ADC (`docs/LESSONS.md`).

## Cause

Two processes use the one S3C2443 ADC with no coordination [V, source read]:

| Process | What it does with the ADC |
|---|---|
| `homtom-statusd` | Every 5 s: battery mux 0, reference mux 4, light mux 2. Each read writes ADCMUX, starts a conversion, waits for ECFLG and reads ADCDAT0 |
| `homtom-touch events` (started by `userland/homtom-ui-start`) | Continuously, about every 11 ms idle and every 12-14 ms while pressed: ADCTSC and GPG12-15 for the detect, X and Y phases, mux 9 (detect), 5 (X), 9 (Y); one discarded and four averaged conversions per read |

ADCMUX, ADCCON and ADCDAT0 are single registers. When the touch tool selects its channel between statusd's mux write and its data read, statusd reads the touch value [H, mechanism consistent with the numbers]. The idle detect channel reads about 1022 (LESSONS "TOUCH WORKS"), which matches the light spikes near 1000. A reference read of 1023, or a battery read taken from a touch channel near 0, gives the 0 % estimate. `homtom-touch` already retried a conversion when it saw ADCMUX changed under it (`clashes`); statusd had no such check, so only statusd's values were corrupted visibly.

## Fix, part 1: arbitration through two files in /tmp

No new daemon, no shared memory, no locks across processes. Three tiny files on tmpfs:

| File | Written by | Content | Meaning |
|---|---|---|---|
| `/tmp/adc.touch` | homtom-touch at start, removed at exit if it still holds its own pid | pid and newline | A touch tool that speaks the protocol is running |
| `/tmp/adc.req` | homtom-statusd, `open(O_CREAT\|O_EXCL)` | statusd pid and newline | statusd wants the ADC |
| `/tmp/adc.ack` | homtom-touch | empty | Touch has finished its frame and stays off the ADC |

The holder file is the third file. Without it statusd could not tell "touch is slow" from "no touch tool runs", and it would wait 40 ms every cycle when the UI is disabled.

### homtom-statusd (requester)

1. After the 250 ms settle of GPL14 and LX_EN: remove any `/tmp/adc.ack` (only statusd requests, so one present now is left over).
2. Create `/tmp/adc.req` exclusively and write its pid. If it exists: older than 2 s, or more than 2 s in the future (clock step), remove it and create again; otherwise count `busy` and retry in 250 ms (it becomes stale after 2 s at the latest).
3. Read `/tmp/adc.touch`. Missing, garbage, or a pid for which `kill(pid, 0)` returns ESRCH (the file is then removed): count `no_touch` and sample at once. statusd does not wait.
4. Otherwise poll every 2 ms, inside the existing `poll()` loop so HTTP keeps being served: `access("/tmp/adc.ack")` succeeds (count `ack`), or 40 ms passed (count `timeout`), or the touch pid died (count `no_touch`). Then sample.
5. Sample: 5 reads each of mux 0, 4, 2 (each read two conversions, the first discarded, and dropped if ADCMUX no longer holds the channel afterwards), about 30 conversions at prescaler 49, well under 1 ms [H, timing from the clock tree, not measured].
6. Remove `/tmp/adc.ack`, then `/tmp/adc.req`, lower GPL14 and LX_EN, run the filter.

If `/tmp/adc.req` cannot be created for another reason (no /tmp), statusd samples without arbitration and the filter is the only protection.

### homtom-touch (holder)

- Once per sampling frame, before the frame starts: one `stat64("/tmp/adc.req")`. With no request that is the whole cost: one failing syscall per frame [V, counted in the host test].
- Request older than 2 s or more than 2 s in the future: remove it, continue sampling.
- Otherwise: create `/tmp/adc.ack`, sleep in 5 ms steps until `access("/tmp/adc.req")` fails or 400 ms passed, remove `/tmp/adc.ack`, resume. After a 400 ms timeout that same request (inode, mtime) is ignored until it disappears or turns stale and is removed.
- The check runs in `tpoll()` (all interactive modes and `events`), in `learn()`, in the `raw` loop and between `scan` configurations.
- A pause is not a frame: `tpoll()` returns EV_NONE without touching `tdown`, `dn_cnt` or `up_cnt`. A held finger therefore gets no spurious UP, the two-frame UP debounce counts only real released frames, and the three-frame DOWN settle continues with the next real frame [V, host test].
- Every frame starts with `phase(PH_DET)`, which rewrites ADCTSC and the GPG12-15 pin states and waits the settle time, and every conversion rewrites ADCMUX and ADCCON, so nothing statusd left in the registers carries over. statusd writes only ADCMUX and ADCCON; it never touches ADCTSC, ADCDLY or GPG12-15.

### Timing

Expected per 5 s cycle [H]: statusd waits until touch reaches the end of its current frame or its `msleep(8)`, typically under 15 ms; touch pauses one or two 5 ms steps. Worst cases are bounded: statusd 40 ms, touch 400 ms.

### Crash safety

| Failure | Outcome |
|---|---|
| statusd dies holding `/tmp/adc.req` | Touch resumes after 400 ms, ignores that request, removes it once older than 2 s; the next statusd also removes it once stale |
| Touch dies before acknowledging | statusd sees the pid gone (`kill` ESRCH) within 2 ms, or waits 40 ms at most |
| Touch dies after acknowledging | statusd samples; the next cycle removes the old acknowledgement before posting |
| Touch killed by SIGPIPE or SIGKILL (no exit path) | `/tmp/adc.touch` stays; statusd finds the pid dead and removes the file |
| pid reused by another process | statusd waits 40 ms per cycle until the touch tool writes a new holder file; samples are still taken |
| Clock step of more than 2 s | A fresh request can look stale and is removed; that cycle samples without a pause and the filter covers it |
| Old touch binary with new statusd | No holder file, statusd samples at once (old behaviour plus the filter) |
| New touch binary with old statusd | No request ever appears; touch only adds its holder file |

### Limits

- Only one requester is designed in (statusd). A second touch instance overwrites the holder file; statusd then waits for one acknowledgement only.
- `homtom-hw adc`, `homtom-status-legacy` and the read-only sweeps do not speak the protocol. Stop statusd and the UI before using them (PERIPHERALS hard rule).

## Fix, part 2: filtering in statusd

Pure code in `workspace/statusd/statusd_core.h`, host-tested in `test_core.c`:

- Per channel per cycle: median of the 5 samples. Lost conversions (timeout, foreign ADCMUX) are -1 and ignored; fewer than 3 valid samples reject the cycle.
- Plausibility, then hold-last-good:

| Channel | Absolute window | Largest step from the median of the last 5 accepted values | Real step accepted after |
|---|---|---|---|
| Battery, mux 0 | 200-1000 | 40 counts (about 260 mV at the current ratio) | 3 agreeing cycles (15 s) |
| Reference, mux 4 | 100-1000 | 30 counts | 3 agreeing cycles |
| Light, mux 2 | 0-1023 | 150 counts | 2 agreeing cycles (10 s) |

- A rejected cycle reports the last accepted value. 12 rejected cycles in a row (60 s) make the value unknown (`null` in the JSON) and clear the history, so a real change is never hidden for long and a dead input shows as missing, not as an old number.
- `battery_pct_est` hysteresis: the reported percentage moves only when the new value differs by 2 points or more, or reaches 0 or 100.
- The windows are first guesses from the values seen on this unit (battery 656, reference about 380, light 2-6) [H]. Adjust `sd_chcfg` in `statusd_core.h` if a real level sits outside them.

## Diagnostics

The JSON gains an `adc` object before `extra`:

    "adc":{"cycles":N,"bad_conv":N,"rejected":[bat,ref,light],"stale":[bat,ref,light],
           "ack":N,"no_touch":N,"timeout":N,"busy":N,"wait_ms":N,"wait_max_ms":N}

`cycles` counts sample cycles; `bad_conv` conversions lost to a timeout or a foreign ADCMUX change; `rejected` cycles rejected per channel since start; `stale` the current run of rejected cycles; `ack`, `no_touch`, `timeout`, `busy` how each cycle's arbitration ended; `wait_ms` and `wait_max_ms` the last and longest wait for the acknowledgement. `homtom-touch` prints `pauses` and `pause_timeouts` with its exit note.

## Build and host test results (2026-11-07)

- `workspace/statusd/test_core.c` [V]: all checks pass, also under AddressSanitizer and UBSan. Includes spikes inside a burst (removed by the median), whole bursts of 0 / 1023 / about 1000 (rejected, last value held, `battery_pct_est` stays 100), 2000 cycles of random glitch injection (reported values never move), real battery and light steps (accepted after 3 and 2 cycles), disagreeing outliers (never accepted), reference unpowered for 12 cycles (turns null, recovers at once), percentage hysteresis, and file-age handling including clock steps.
- `tools/devtools/tests/touch_arb_test.c` [V]: compiles `homtom-touch.c` on the host with mocked syscalls, clocks and frames. Checks one stat per frame with no request, DOWN held across a pause, no UP from pauses, UP after exactly two real released frames, DOWN settle across a pause, stale and future requests removed without a pause, 400 ms timeout followed by ignoring that request and honouring the next one, holder file write and owner-only removal.
- Cross builds [V]: `OUT=homtom-statusd.new workspace/statusd/build.sh dev` gives `workspace/statusd/out/homtom-statusd.new` (21064 bytes stripped, text 17744; the committed build is 16968 / 14501). `SUFFIX=.new tools/devtools/build.sh homtom-touch` gives `tools/devtools/homtom-touch.new` (42328 bytes unstripped like the committed one, text 34153 versus 32805). `Tag_CPU_arch: v4T` for both; an unstripped disassembly of each has no `blx`, `clz`, `ldrd`, `strd` or other ARMv5+ instruction (the `ldrd`/`strd` that objdump shows in the stripped statusd are literal-pool words, present in the committed binary too).

## Device check [H, pending]

Deploy both binaries together (`homtom-statusd.new` as `homtom-statusd`, `homtom-touch.new` as `homtom-touch`), then:

1. `ls -l /tmp/adc.*`: only `/tmp/adc.touch` with the pid of the running `homtom-touch events`; no `adc.req` or `adc.ack` left between cycles.
2. Poll the endpoint once a second for 5 minutes while tapping and dragging on the screen, and log `battery_raw`, `battery_ref_raw`, `battery_pct_est`, `light_raw` and the `adc` object. Expected: no 0 % and no `light_raw` near 1000; `ack` grows by one per 5 s; `timeout` and `busy` stay at or near 0; `wait_max_ms` under 40; `rejected` small. `bad_conv` above 0 means a foreign ADCMUX change still happened.
3. Touch quality while statusd runs: `homtom-touch-run targets` and `drag` as before, with no missed taps and no stroke broken every 5 s; the GUI slider drags without a jump or a lift every 5 s.
4. Stop the UI (`/mnt/internal/homtom/gui.disable` and reboot, or kill the launcher and touch by PID): `no_touch` should grow by one per cycle and `wait_ms` stay 0.
5. Optional: compare `rejected` with the arbitration disabled (old touch binary with the new statusd). Rejections there show the filter catching collisions; with both new binaries they should be near zero.

## Files

- `workspace/statusd/statusd_core.h`: filter, hysteresis, file-age helpers, JSON `adc` object.
- `workspace/statusd/homtom-statusd.c`: ADCMUX-checked reads, request/ack state machine in the 5 s sampler.
- `workspace/statusd/test_core.c`: filter tests with glitch injection.
- `workspace/statusd/build.sh`: `OUT=` names the output.
- `tools/devtools/homtom-touch.c`: `adc_yield()`, holder file, pause handling in `tpoll()`, host-test hooks (`TOUCH_HOST_TEST`, `TOUCH_FRAME`).
- `tools/devtools/tests/touch_arb_test.c`: mock test of the pause.
- `tools/devtools/build.sh`: `SUFFIX=` names the output.

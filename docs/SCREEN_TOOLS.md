# Tools on the screen

Status: built and tested on the Mac only (2026-11-07): GUI previews and traces, statusd host tests, the three scripts under the device's BusyBox 1.22.1 with stub tools. Nothing here has run on the device yet.

The Tools page of the default `ui.conf` (`userland/ui.conf.default`, same as `workspace/gui/conf/full.conf`) lets you test the GPS, Bluetooth and audio from the device itself. Open it with the **Tools** button in the bottom bar.

    [ GPS aid ] [ BT visible ] [ Mic test ] [ Beep ]
    [ GPS AID result ] [ BT PAIRING state ] [ MIC result ]
    [ GPS power  (switch) ]   [ Storage mode ]

Each top button starts one script in `/bin`; its result shows in the tile under it within a second or two. A second press while the script still runs is refused with a note in the header (`gps-aid busy or failed`). After a reboot the tiles show `--` until the button is pressed. The same results are in `/tmp/ui.extra`, in `/tmp/homtom-ui.txt`, under `"extra"` in the status JSON (`curl -s http://192.168.1.10:8080/`) and as `homtom-…` lines in `dmesg`.

## Before the first test

| Button | Needs |
|---|---|
| GPS aid | `INTERNAL:/homtom/gps.aid`: one line `LAT LON ALT` in decimal degrees and metres, for example `51.48 -0.00 46`. Two decimals are enough. Write it yourself in storage mode; it never goes into the repo or a log. The device clock must be set (the Mac bridge does that after boot) |
| BT visible | Bluetooth enabled (`INTERNAL:/homtom/bt.enable`) and the PIN in `INTERNAL:/homtom/bt.pin` |
| Mic test | Nothing |

## Buttons

| Button | What it does | Takes | Expected result |
|---|---|---|---|
| **GPS aid** | `homtom-gps-aid`: sends your approximate position and the current time to the GPS receiver (`gpsaid`, one `$PSRF104`, warm start), then watches whether the receiver's date becomes today's | up to 15 s | `sending`, then `OK 12:00`: the receiver took the time sent at 12:00 UTC. `sent 12:00`: sent but its date did not change. Other values below |
| **BT visible** | `homtom-bt-visible`: opens the Bluetooth pairing window for 5 minutes. The unit is visible to other devices all the time; only inside this window can a new device pair with the PIN | 5 min | Tile counts down `4:55 left` in green, every 5 s. Pair from the phone or Mac with the PIN. After a successful pairing the window closes at once and the tile shows `paired`; otherwise `closed` after 5 minutes |
| **Mic test** | `homtom-mic-test`: a 20 s live level meter on the internal microphone (`homtom-snd2 --micmeter 20`) | 20 s; the screen keeps updating | The MIC tile shows the live level (`-38 dB`, yellow) about once a second, `pk -12 dB  14 s` (highest peak so far, seconds left) at its right. Talk or clap while it runs: the level should jump 15 dB or more above the quiet value. At the end the tile shows `OK` (green: the loudest level was more than 12 dB above the quietest) or `quiet` (yellow: the level never moved) |
| **Beep** | 1 s tone at 1000 Hz at the Controls page volume (`homtom-snd2`) | 1 s | A tone from the speaker. Nothing when muted or at volume 0 (header says `muted`). Mic test and Beep never run at the same time |
| **GPS power** | Switches the GPS receiver off (`homtom-hw gps-off`) or on (`gps-on`, which also resets it) | 1 s | GPS page: NMEA stops when off (fix and satellites freeze), resumes when on. The switch shows the last state set from the screen; it starts as on because the GPS is on after boot |
| **Storage mode** | Reboots into storage mode after an OK/Cancel dialog | | As the button on the System page |

## Results that mean something went wrong

| Tile | Shows | Meaning and what to do |
|---|---|---|
| GPS AID | `no gps.aid` | The position file is missing. Time-only aiding is not possible with the current `gpsaid` |
| | `bad gps.aid` | The first line is not two or three plain numbers |
| | `clock unset` | Device clock before 2026; wait until the Mac bridge has set it |
| | `refused` | The GPS port is not at 4800 baud (status daemon not running?) or a number is out of range |
| | `port error`, `failed N` | Could not open or write the GPS port; other error |
| | `sent 12:00` | The receiver did not show today's date within 15 s. Look at the GPS page a little later; the full test plan is `docs/GPS_AIDING.md` section 6 |
| BT PAIRING | `no btd` | The Bluetooth daemon is not running (Bluetooth disabled or failed at boot) |
| | `no PIN` | `bt.pin` is missing or empty; every pairing would be refused |
| MIC | `error` | `homtom-snd2` failed or another sound program was running |
| | `silent` | The input does not move (peak and average almost equal): no sound reaches it, or the channel is stuck |

## Notes

- The microphone level is the RMS of the right channel in dB below full scale, measured per 250 ms after the codec's DC offset is removed (the earlier average-of-absolute-values test left the offset in and reported -8 dB). Earlier measurements found a lot of noise on this input (`docs/LESSONS.md`), so judge the change between quiet and loud, not the absolute value. `mic_peak` in `/tmp/ui.extra` is the highest sample peak seen after the first 0.5 s (the codec switch-on pop is ignored), `mic_t` the seconds left. `dmesg` shows `homtom-mic-test: start 20s`, a line every 5 s (`level=-61 peak=-48 left=15s floor=-62`) and `done lines=80 floor=-62 loudest=-31 peak=-14 state=ok`; `state=error` with `0 meter lines` means the codec was busy or `homtom-snd2` failed.
- `homtom-snd2 --micmeter SECS` (1 to 60) prints `m LEVEL PEAK LEFT` every 250 ms to stdout, whole dBFS, floor -90, for any script to read. It runs at normal priority and never touches the speaker amp. Maths: `tools/devtools/homtom-micmath.h`, checked on the Mac by `tools/devtools/tests/micmath_test.c`.
- The pairing window length can be changed by hand: `echo 600 > /tmp/bt.pair` opens it for 600 s (30..1800; `touch /tmp/bt.pair` gives 300).
- Nothing on this page stops or kills another program. The GPS status daemon keeps running during GPS aid.

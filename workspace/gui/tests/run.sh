#!/bin/sh
# Regression tests for the configurable UI, host only (no device):
#   1. every page of every conf/*.conf, of the base config merged behind the office page, of the fixtures and of the
#      built-in config render to PNG; hashes must match tests/golden.sha256. Home Assistant values come from
#      tests/ha.states.sample, the wall clock is fixed (-W) so the header clock is deterministic
#   2. scripted taps, drags, long presses and key presses produce the expected spawn/event trace (tests/expected/*.txt)
#   3. every tests/bad/*.conf is rejected with the error named in its "# expect:" first line
#   4. time zone rules: preview -Q against tests/tz.cases; the shipped base config equals conf/full.conf; the compiled-in
#      config equals conf/default.conf; regenerated font, wordmark and icon tables equal the checked-in ones (when the
#      pinned sources are in build/gui-assets); device binary size cap
# usage: tests/run.sh [--update]   (--update rewrites the golden hashes and traces after you checked the PNGs)
set -eu
cd "$(dirname "$0")/.."
UPDATE=${1:-}
OUT=out/test
rm -rf "$OUT"
mkdir -p "$OUT" tests/expected
clang -O1 -Wall -Wextra -Wno-unused-parameter -o out/preview host.c ui.c gui.c cfg.c
fail=0
WALL=1794058320		# 2026-11-07 13:32 UTC; the sample states say UTC+1, so the header shows 14:32
HA="-e tests/ha.states.sample -A 3 -W $WALL"
cat conf/office.conf conf/full.conf > "$OUT/merged.conf"
{ echo "ui theme=light"; cat conf/office.conf; } > "$OUT/light.conf"

# 1. renders
for c in conf/*.conf; do
	n=$(basename "$c" .conf)
	./out/preview -c "$c" -f sample.txt $HA -a "$OUT/$n" > /dev/null
done
./out/preview -c "$OUT/merged.conf" -f sample.txt $HA -a "$OUT/merged" > /dev/null
./out/preview -c "$OUT/light.conf" -f sample.txt $HA -p office -o "$OUT/office-light.png" > /dev/null
./out/preview -f sample.txt -W $WALL -a "$OUT/builtin" > /dev/null
./out/preview -c conf/full.conf -f sample.txt -W $WALL -p power -T 400,212 -o "$OUT/state-confirm.png" > /dev/null
./out/preview -c conf/controls.conf -f sample.txt -t 100,98 -t 60,98 -o "$OUT/state-drag.png" > /dev/null
# Power page confirmations (restart, power off); power key hold hints (5 s, 10 s, 5 s on a locked screen)
./out/preview -c conf/full.conf -f sample.txt -W $WALL -p power -T 400,74 -o "$OUT/state-restart.png" > /dev/null
./out/preview -c conf/full.conf -f sample.txt -W $WALL -p power -T 400,143 -o "$OUT/state-poweroff.png" > /dev/null
./out/preview -c "$OUT/merged.conf" -f sample.txt $HA -K hold5 -o "$OUT/hint-hold5.png" > /dev/null
./out/preview -c "$OUT/merged.conf" -f sample.txt $HA -K hold10 -o "$OUT/hint-hold10.png" > /dev/null
./out/preview -c "$OUT/merged.conf" -f sample.txt $HA -K short -K hold5 -o "$OUT/hint-locked.png" > /dev/null
./out/preview -c conf/controls.conf -o "$OUT/state-nodata.png" > /dev/null
# header battery: full and charging, 55 %, 12 %, missing (the status file lines differ only in those keys)
for b in 100,1 55,0 12,0; do
	{ grep -v '^battery_pct_est=\|^charging=\|^external_power=' sample.txt; echo "battery_pct_est=${b%,*}"; echo "charging=${b#*,}"; } \
		> "$OUT/bat.txt"
	./out/preview -c conf/office.conf -f "$OUT/bat.txt" $HA -o "$OUT/hdr-bat-${b%,*}.png" > /dev/null
done
# light sensor reading 4 (not fitted on this unit): unavailable_below=10 shows Unavailable
{ grep -v '^light_raw=' sample.txt; echo light_raw=4; } > "$OUT/bat.txt"
./out/preview -c conf/full.conf -f "$OUT/bat.txt" -W $WALL -p device -o "$OUT/state-light-na.png" > /dev/null
grep -v '^battery_pct_est=' sample.txt > "$OUT/bat.txt"
./out/preview -c conf/office.conf -f "$OUT/bat.txt" $HA -o "$OUT/hdr-bat-none.png" > /dev/null
# HA: offline for 5 min (grey states, warning in the header), no states file; a long press on Hue 1 (light page) and a
# brightness drag in progress; More shows the hue strip; optimistic values after taps; thermostat with a pending target;
# the vent page after a long press; an entity with confirm=1
./out/preview -c conf/office.conf -f sample.txt -e tests/ha.states.sample -A 300 -W $WALL -o "$OUT/ha-offline.png" > /dev/null
./out/preview -c tests/conf/ha-nav.conf -f sample.txt -e tests/ha.states.sample -A 300 -W $WALL -a "$OUT/ha-stale" > /dev/null
./out/preview -c conf/office.conf -f sample.txt -W $WALL -o "$OUT/ha-nofile.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -L 235,78 -o "$OUT/ha-hold-light.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -L 235,78 -t 200,108 -t 330,108 -o "$OUT/ha-drag.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -L 235,78 -T 430,204 -o "$OUT/ha-more.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -T 81,78 -T 81,162 -T 235,162 -o "$OUT/ha-optimistic.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -T 430,216 -o "$OUT/ha-thermo.png" > /dev/null
./out/preview -c conf/office.conf -f sample.txt $HA -L 235,162 -T 82,100 -o "$OUT/ha-vent-confirm.png" > /dev/null
./out/preview -c tests/conf/ha-nav.conf -f sample.txt $HA -T 181,144 -o "$OUT/ha-confirm.png" > /dev/null
# time zone set on the device (Time and date page, picker page 2)
echo "60,eu" > "$OUT/tz"
./out/preview -c conf/full.conf -f sample.txt $HA -Z "$OUT/tz" -p time -o "$OUT/tz-device.png" > /dev/null
./out/preview -c conf/full.conf -f sample.txt -W $WALL -p tz2 -o "$OUT/tz-utc.png" > /dev/null
# the generator's example output (compact lightctl and thermo, entities without icons, nav=both, a light group);
# swipe fixtures: dots only, group dots on hidden pages, tabs and dots with wrap=1; cycle: two areas and a hub
for c in generated swipe wrap cycle; do
	./out/preview -c tests/conf/$c.conf -f sample.txt $HA -a "$OUT/$c" > /dev/null
done
./out/preview -c conf/office.conf -f sample.txt $HA -L 235,78 -S 400,70,200,70 -o "$OUT/ha-swipe-group.png" > /dev/null
(cd "$OUT" && shasum -a 256 *.png) > "$OUT/golden.sha256"
if [ "$UPDATE" = --update ]; then
	cp "$OUT/golden.sha256" tests/golden.sha256
	echo "updated tests/golden.sha256"
elif ! diff -u tests/golden.sha256 "$OUT/golden.sha256"; then
	echo "FAIL renders differ from tests/golden.sha256 (PNGs in $OUT)"
	fail=1
else
	echo "ok   renders ($(wc -l < tests/golden.sha256 | tr -d ' ') PNGs)"
fi

# 2. action traces
trace() {
	name=$1
	shift
	./out/preview "$@" -o /dev/null > "$OUT/$name.txt"
	if [ "$UPDATE" = --update ]; then
		cp "$OUT/$name.txt" "tests/expected/$name.txt"
	elif ! diff -u "tests/expected/$name.txt" "$OUT/$name.txt"; then
		echo "FAIL trace $name"
		fail=1
	else
		echo "ok   trace $name"
	fi
}
# backlight drag 100 -> 15 %, beep, mute, beep while muted (no spawn), power key short and long
trace controls -c conf/controls.conf -f sample.txt -t 100,98 -t 47,98 -u -T 120,190 -T 350,190 -T 120,190 -K short -K long
# HA events, a confirm that is cancelled and one that is accepted, presets, toggle with a var
trace scenes -c conf/controls.conf -f sample.txt -p scenes -T 82,90 -T 396,90 -T 160,180 -T 396,90 -T 320,180 \
	-T 82,190 -T 239,190 -T 396,190
# storage mode: first tap only opens the dialog, Cancel does nothing, Restart runs /bin/reboot
trace storage -c conf/full.conf -f sample.txt -p power -T 400,212 -T 160,180 -T 400,212 -T 320,180
# Device, Power: restart cancelled, power off accepted (runs /bin/homtom-poweroff), restart accepted (runs /bin/reboot;
# on the device the reboot slot would refuse it while the power-off child runs; the preview does not model busy slots)
trace power -c conf/full.conf -f sample.txt -p device -T 358,61 -P -T 400,74 -T 160,180 -T 400,143 -T 320,180 -T 400,74 \
	-T 320,180
# hold hints: lock, hold5 lights the backlight, the short press after it neither unlocks nor locks and the backlight goes
# off again; a hint that times out (15 s) does the same; a long press unlocks and opens the hub; hold10, then the long
# press after it does not navigate
trace hold -c "$OUT/merged.conf" -f sample.txt $HA -K short -K hold5 -P -K short -P -K hold5 -w 16000 -P -T 20,20 -P \
	-K long -P -T 20,20 -P -K hold10 -T 81,78 -K long -P
# allow-listed commands, reload, plain event, header tabs
trace tools -c conf/system.conf -f sample.txt -p tools -T 82,90 -T 239,90 -T 320,180 -T 396,90 -T 82,190 -T 239,190 \
	-T 120,20 -T 82,90
# tool-page entries on the hub's subpages: GPS assist, GPS receiver off and on, Bluetooth pairing, mic test, test tone, USB
# storage (Device, Power) cancelled; a tap on a status row does nothing
trace ownertools -c conf/full.conf -f sample.txt -p gps -T 400,144 -T 400,74 -T 400,74 -T 100,61 -T 20,20 -T 396,94 \
	-T 400,74 -T 20,20 -T 82,210 -T 400,74 -T 400,143 -T 20,20 -T 396,210 -T 358,61 -T 400,212 -T 160,180 -P
# the built-in config: power key short locks the screen, a touch while locked does nothing, short unlocks; long opens
# the hub from a subpage
trace builtin -f sample.txt -K short -P -T 239,94 -K short -P -T 239,94 -P -K short -K long -P -K short
# the base config merged behind the office page: gear opens the hub, a hub tile its subpage, Back, Back to the office;
# a long key press from a subpage goes to the hub; locked, a long press unlocks and opens the hub; key lines name the page
trace keys -c "$OUT/merged.conf" -f sample.txt $HA -T 452,20 -P -T 239,94 -P -T 20,20 -P -T 20,20 -P \
	-T 452,20 -T 396,210 -K long -K short -T 81,78 -K long -T 20,20 -P
# vent: a tap sends press; a long press opens the vent page, High sends, On/off asks first; Back
trace vent -c conf/office.conf -f sample.txt $HA -T 235,162 -L 235,162 -T 239,100 -T 82,100 -T 320,180 -T 20,20 -P
# time zone: Time and date, Time zone, Central Europe (writes the file), swipe to the second page, +30 min (fixed offset
# from the current one), Auto
trace tz -c conf/full.conf -f sample.txt $HA -Z "$OUT/tz" -p time -T 240,219 -T 240,122 -S 400,150,200,150 -T 420,226 \
	-S 200,150,400,150 -T 240,60
# page:cycle (two areas, then the hub, then back to the first), a subpage goes back to its parent
trace cycle -c tests/conf/cycle.conf -f sample.txt $HA -K short -P -K short -P -T 239,94 -P -K short -P -K short -P \
	-K short -P
# autolock after 10 s without touch or key
trace autolock -c tests/conf/cycle.conf -f sample.txt $HA -w 5000 -T 100,100 -w 9000 -P -w 2000 -P
# HA entities: taps toggle; a display-only reading (presence) does nothing
trace ha-taps -c conf/office.conf -f sample.txt $HA -T 81,78 -T 81,162 -T 50,228 -T 235,162
# long press opens the light page and sends nothing; a brightness drag sends at once, then at most every 300 ms, and
# the last value after release
trace ha-light -c conf/office.conf -f sample.txt $HA -L 235,78 -t 135,108 -t 216,108 -w 100 -t 267,108 -w 250 \
	-t 400,108 -u -w 400
# More, hue strip, Colours, a swatch right after it (waits for the throttle), on/off, Back, then a tap on the office page
trace ha-colour -c conf/office.conf -f sample.txt $HA -L 235,78 -T 430,204 -T 109,204 -T 430,204 -T 64,204 -w 300 \
	-T 440,60 -T 20,20 -T 81,78
# a finger that moves 15 px is no long press (release taps the tile); one that moves 9 px is (release sends nothing,
# the light page's on/off button proves which page is shown)
trace ha-move -c conf/office.conf -f sample.txt $HA -t 235,78 -t 250,78 -w 700 -u -t 235,78 -t 244,78 -w 700 -u \
	-T 440,60
# thermostat: three minus taps send one set_target 1 s after the last; a mode tap goes out at once and does not cancel
# the waiting target; plus sends after its own second
trace ha-thermo -c conf/office.conf -f sample.txt $HA -T 360,216 -T 360,216 -T 360,216 -w 900 -T 394,70 -w 200 \
	-T 430,216 -w 1100
# swipes (dots only, no wrap): left to b, right to a, right again stays; one starting on the slider drags it; 40 px on
# A1 is a tap; a diagonal one is no swipe (taps the toggle); into the d1/d2 group and to its end, back to d1, Back goes to
# the parent b; a hidden page without a group does not swipe. "key short" prints the page
trace swipe -c tests/conf/swipe.conf -f sample.txt -S 300,200,200,200 -K short -S 100,200,300,200 -K short \
	-S 100,200,300,200 -K short -S 260,100,420,100 -K short -S 100,100,140,100 -S 100,190,180,240 -S 300,200,200,200 \
	-T 120,90 -K short -S 300,200,200,200 -K short -S 300,200,200,200 -K short -S 200,200,300,200 -T 240,150 -K short \
	-T 360,90 -S 300,200,200,200 -K short
# wrap=1: right from the first page goes to the last, left from there back to the first; the dot strip ends step too
trace wrap -c tests/conf/wrap.conf -f sample.txt -S 100,150,300,150 -K short -S 300,150,100,150 -K short -T 290,262 \
	-K short -T 190,262 -K short
# generator output: compact lightctl name area opens detail=, a swipe below the slider moves in the group, one on the
# header row back, Back to the parent; compact brightness drag; thermo step=0.5 twice then one send; mode via the
# compact thermo's left area; entity tap=press confirm=1; header tab to the second area; tap=toggle
trace generated -c tests/conf/generated.conf -f sample.txt $HA -T 50,72 -K short -S 400,180,200,180 -K short \
	-S 300,60,450,60 -K short -T 20,20 -K short -t 136,72 -t 207,72 -u -w 400 -T 450,72 -T 450,72 -w 1100 -T 260,72 \
	-T 181,184 -T 320,180 -T 190,20 -T 63,72
# office: long press on Hue 1, swipe left on the header row of the light page to the next light of the group, key
# prints the page
trace ha-group -c conf/office.conf -f sample.txt $HA -L 235,78 -S 400,60,200,60 -K short -S 200,60,400,60 \
	-S 200,60,400,60 -K short -T 20,20 -K short
# buttons with action=ha (set_bri:0, press, mode:cool, set_ct), an entity with confirm=1 cancelled then accepted
trace ha-buttons -c tests/conf/ha-nav.conf -f sample.txt $HA -T 63,200 -T 181,200 -T 299,200 -T 417,200 -T 181,144 \
	-T 160,180 -T 181,144 -T 320,180

# 3. invalid configs
for b in tests/bad/*.conf; do
	want=$(sed -n '1s/^# expect: //p' "$b")
	set +e
	err=$(./out/preview -c "$b" -o /dev/null 2>&1 > /dev/null)
	rc=$?
	set -e
	case "$err" in
	*"$want"*) [ $rc = 2 ] && echo "ok   rejects $(basename "$b"): $want" || { echo "FAIL $b exit $rc"; fail=1; } ;;
	*) echo "FAIL $b: want '$want', got '$err'"; fail=1 ;;
	esac
done

# 4. time zone rules, shipped files, generated tables, binary
args=$(grep -v '^#' tests/tz.cases | awk '{printf "-Q %s,%s,%s ", $1, $2, $3}')
# shellcheck disable=SC2086
./out/preview $args > "$OUT/tz.out"
if grep -v '^#' tests/tz.cases | diff -q - "$OUT/tz.out" > /dev/null; then
	echo "ok   summer time rules ($(wc -l < "$OUT/tz.out" | tr -d ' ') cases)"
else
	echo "FAIL summer time rules"; grep -v '^#' tests/tz.cases | diff - "$OUT/tz.out" | head; fail=1
fi
if diff -q conf/full.conf ../../userland/ui.conf.default > /dev/null; then
	echo "ok   userland/ui.conf.default matches conf/full.conf"
else
	echo "FAIL userland/ui.conf.default differs from conf/full.conf"
	fail=1
fi
grep -v '^#' conf/default.conf | grep -v '^$' > "$OUT/default.conf.lines"
sed -n '/^static const char builtin_conf/,/^;/p' cfg.c | sed -n 's/^	"\(.*\)\\n"$/\1/p' | sed 's/\\"/"/g' > "$OUT/builtin.conf.lines"
if diff -q "$OUT/default.conf.lines" "$OUT/builtin.conf.lines" > /dev/null; then
	echo "ok   builtin_conf matches conf/default.conf"
else
	echo "FAIL cfg.c builtin_conf differs from conf/default.conf"
	fail=1
fi
if [ -f ../../build/gui-assets/Roboto-VF.ttf ]; then
	./gen_assets.sh "$OUT/assets" 2> /dev/null
	for h in font_data.h wordmark_data.h icon_data.h; do
		if cmp -s "$h" "$OUT/assets/$h"; then echo "ok   $h regenerates identically"; else echo "FAIL $h differs from gen_assets.sh output"; fail=1; fi
	done
else
	echo "skip font/icon regeneration (run tools/gui-assets/fetch.sh)"
fi

# device binary, when built: size cap (160 KB, docs/UI_CONFIG.md "Binary size") and no libgcc/libc symbols pulled in
if [ -f out/homtom-gui-cfg ]; then
	size=$(wc -c < out/homtom-gui-cfg | tr -d ' ')
	if [ "$size" -lt 163840 ]; then echo "ok   out/homtom-gui-cfg $size bytes (< 160 KB)"; else echo "FAIL out/homtom-gui-cfg $size bytes"; fail=1; fi
fi
[ $fail = 0 ] && echo "all tests passed" || { echo "some tests FAILED"; exit 1; }

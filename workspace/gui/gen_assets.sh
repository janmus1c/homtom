#!/bin/sh
# Regenerate the font and icon tables from the pinned sources (tools/gui-assets/fetch.sh puts them in build/gui-assets).
#   font_data.h      Roboto 12/14/16/20/28 px (gen_font.py, needs Pillow)
#   wordmark_data.h  the homtom wordmark, Poppins SemiBold (gen_wordmark.py, needs Pillow)
#   icon_data.h  the Material Design Icons the renderer uses, 20 and 24 px, 2 bpp (tools/gui-assets/gen_icons.py)
# usage: ./gen_assets.sh [OUTDIR]     (default: this folder; tests/run.sh writes to out/assets and compares)
# Pillow: python3 with PIL, else `uv run --with pillow`.
set -eu
cd "$(dirname "$0")"
OUT=${1:-.}
SRC=../../build/gui-assets
ICONS=lightbulb,lightbulb-outline,thermostat,radiator,snowflake,fire,power,molecule-co2,molecule,thermometer
ICONS=$ICONS,water-percent,motion-sensor,motion-sensor-off,account,fan,air-conditioner,gesture-tap-button,eye
ICONS=$ICONS,minus,plus,arrow-left,cog,chevron-right,brightness-6,white-balance-sunny,palette,cloud-off-outline
ICONS=$ICONS,alert-circle-outline,crosshairs-gps,sync,bluetooth,microphone,volume-high,volume-off,usb,restart
ICONS=$ICONS,battery,battery-charging,power-plug,power-plug-off,information-outline,home,check,clock-outline
../../tools/gui-assets/fetch.sh > /dev/null
mkdir -p "$OUT"
PY="python3"
python3 -c 'import PIL' 2> /dev/null || PY="uv run --no-project --with pillow python"
$PY gen_font.py "$SRC/Roboto-VF.ttf" > "$OUT/font_data.h"
$PY gen_wordmark.py 16 > "$OUT/wordmark_data.h"
python3 -I ../../tools/gui-assets/gen_icons.py --svg-dir "$SRC/package/svg" --icons "$ICONS" --sizes 20,24 --bpp 2 \
	--table "$OUT/icon_data.h"
cp "$SRC/Roboto-OFL.txt" "$OUT/OFL.txt"

#!/bin/sh
# Fetch the pinned GUI asset sources into build/gui-assets/ (ignored by git) and check their SHA-256:
#   Roboto variable font and its OFL.txt from google/fonts (ofl/roboto, SIL Open Font License 1.1)
#   @mdi/svg 7.4.47 from the npm registry (Material Design Icons, icons Apache 2.0), unpacked to package/svg/
# usage: tools/gui-assets/fetch.sh      (then workspace/gui/gen_assets.sh regenerates font_data.h and icon_data.h)
set -eu
cd "$(dirname "$0")/../.."
OUT=build/gui-assets
FONTS_COMMIT=6183fc0d26361f6ddfd6f6b7a736e1467c6d8a43
RAW=https://raw.githubusercontent.com/google/fonts/$FONTS_COMMIT/ofl/roboto
mkdir -p "$OUT"

get() {
	url=$1 file=$2 sum=$3
	if [ -f "$OUT/$file" ] && [ "$(shasum -a 256 "$OUT/$file" | cut -d' ' -f1)" = "$sum" ]; then
		return 0
	fi
	curl -sfL -o "$OUT/$file.part" "$url"
	got=$(shasum -a 256 "$OUT/$file.part" | cut -d' ' -f1)
	if [ "$got" != "$sum" ]; then
		echo "$file: SHA-256 $got, expected $sum" >&2
		rm -f "$OUT/$file.part"
		exit 1
	fi
	mv "$OUT/$file.part" "$OUT/$file"
}

get "$RAW/Roboto%5Bwdth,wght%5D.ttf" Roboto-VF.ttf d7598e12c5dbef095ff8272cfc55da0250bd07fbdecbac8a530b9b277872a134
get "$RAW/OFL.txt" Roboto-OFL.txt 061402327a96aadb0bfb694a960ed289ecd38d383e396243831ab81feb109c41
get https://registry.npmjs.org/@mdi/svg/-/svg-7.4.47.tgz mdi-svg-7.4.47.tgz \
	de92e5dc9ce46c392ab5c53aa7190b19f82b40cb48872a083f788c7e13e91fef
if [ ! -f "$OUT/package/svg/lightbulb.svg" ]; then
	tar xzf "$OUT/mdi-svg-7.4.47.tgz" -C "$OUT" package/svg package/LICENSE
fi
echo "ok   $OUT: Roboto-VF.ttf, Roboto-OFL.txt, package/svg"

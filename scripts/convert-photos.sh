#!/bin/sh
# Convert board photos (DNG, HEIC, TIFF, JPG) in a folder to upright JPEGs for review.
# Usage: scripts/convert-photos.sh [DIR]   default .runtime/board-photos
# Writes DIR/converted/NAME.jpg (long edge 3000 px, EXIF orientation applied to the pixels) and DIR/converted/small/NAME.jpg (1600 px).
set -eu
D=${1:-.runtime/board-photos}
O=$D/converted
mkdir -p "$O/small"
n=0
for f in "$D"/*; do
	[ -f "$f" ] || continue
	case "$(echo "$f" | tr 'A-Z' 'a-z')" in
	*.dng|*.heic|*.tif|*.tiff|*.jpg|*.jpeg|*.png|*.cr2|*.arw|*.nef) ;;
	*) continue ;;
	esac
	b=$(basename "$f"); b=${b%.*}
	[ -f "$O/$b.jpg" ] && continue
	sips -s format jpeg -s formatOptions 92 -Z 3000 "$f" --out "$O/$b.jpg" > /dev/null 2>&1 || { echo "failed: $f"; continue; }
	# bake the orientation into the pixels: sips keeps the tag, so rotate by it and reset the tag
	o=$(sips -g orientation "$O/$b.jpg" 2> /dev/null | awk '/orientation/ {print $2}')
	case "$o" in
	3) sips -r 180 "$O/$b.jpg" > /dev/null 2>&1 ;;
	6) sips -r 90 "$O/$b.jpg" > /dev/null 2>&1 ;;
	8) sips -r 270 "$O/$b.jpg" > /dev/null 2>&1 ;;
	esac
	sips -s format jpeg -Z 1600 "$O/$b.jpg" --out "$O/small/$b.jpg" > /dev/null 2>&1
	n=$((n + 1))
done
echo "converted $n file(s) into $O"

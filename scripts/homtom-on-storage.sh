#!/bin/sh
# When /Volumes/INTERNAL appears: archive logs, restore lab, optionally stage next image.
# Usage: homtom-on-storage.sh [path-to-next-ttsystem]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LAB=$ROOT/artifacts/backups/ttsystem-pre-homtom61.bin
NEXT=${1:-}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
DEST=$ROOT/artifacts/homtom-boot-logs/$STAMP

echo "waiting for INTERNAL..."
until [ -d /Volumes/INTERNAL ]; do sleep 2; done
mkdir -p "$DEST"
cp -p /Volumes/INTERNAL/homtom-boot.log "$DEST/" 2>/dev/null || true
cp -p /Volumes/INTERNAL/homtom-boot.dmesg "$DEST/" 2>/dev/null || true
cp -p /Volumes/INTERNAL/homtom/logs/boot.log "$DEST/homtom-logs-boot.log" 2>/dev/null || true
cp -p /Volumes/INTERNAL/homtom/logs/boot.dmesg "$DEST/homtom-logs-boot.dmesg" 2>/dev/null || true
cp -p "/Volumes/NO NAME/homtom-boot.log" "$DEST/sd-homtom-boot.log" 2>/dev/null || true
shasum -a 256 /Volumes/INTERNAL/ttsystem > "$DEST/ttsystem.sha256" || true
ls -la "$DEST" || true
[ -f "$DEST/homtom-boot.log" ] && { echo "---- boot.log ----"; cat "$DEST/homtom-boot.log"; }

cp "$LAB" /Volumes/INTERNAL/ttsystem.new && sync
mv /Volumes/INTERNAL/ttsystem.new /Volumes/INTERNAL/ttsystem && sync
echo "lab restored $(shasum -a 256 /Volumes/INTERNAL/ttsystem | awk '{print $1}')"

if [ -n "$NEXT" ] && [ -f "$NEXT" ]; then
	cp "$NEXT" /Volumes/INTERNAL/ttsystem.homtom-next
	sync
	echo "staged next as ttsystem.homtom-next $(shasum -a 256 "$NEXT" | awk '{print $1}')"
fi

for vol in "/Volumes/INTERNAL" "/Volumes/NO NAME"; do
	[ -d "$vol" ] || continue
	D=$(diskutil info -plist "$vol" 2>/dev/null | plutil -extract ParentWholeDisk raw - 2>/dev/null || true)
	[ -n "$D" ] && diskutil unmountDisk force "$D" 2>/dev/null || true
	[ -n "$D" ] && diskutil eject "$D" 2>/dev/null || true
done
echo ejected

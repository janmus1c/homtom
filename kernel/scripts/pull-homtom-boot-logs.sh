#!/bin/sh
# After a diagnostic boot, reboot into bootloader storage and run this.
# Copies logs off INTERNAL, then restores lab 2.6.13 ttsystem.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
DEST=$ROOT/artifacts/homtom-boot-logs/$STAMP
LAB=$ROOT/artifacts/backups/ttsystem-pre-homtom61.bin

echo "waiting for /Volumes/INTERNAL..."
until [ -d /Volumes/INTERNAL ]; do sleep 2; done
mkdir -p "$DEST"
ls -la /Volumes/INTERNAL/homtom-boot* /Volumes/INTERNAL/ttsystem* 2>/dev/null || true
cp -p /Volumes/INTERNAL/homtom-boot.log "$DEST/" 2>/dev/null || echo "no homtom-boot.log"
cp -p /Volumes/INTERNAL/homtom-boot.dmesg "$DEST/" 2>/dev/null || echo "no homtom-boot.dmesg"
# also grab from SD LUN if present
if [ -d "/Volumes/NO NAME" ]; then
	cp -p "/Volumes/NO NAME/homtom-boot.log" "$DEST/sd-homtom-boot.log" 2>/dev/null || true
	cp -p "/Volumes/NO NAME/homtom-boot.dmesg" "$DEST/sd-homtom-boot.dmesg" 2>/dev/null || true
fi
shasum -a 256 /Volumes/INTERNAL/ttsystem > "$DEST/ttsystem.sha256" || true
ls -la "$DEST"
echo "logs in $DEST"

if [ -f "$LAB" ]; then
	echo "restoring lab ttsystem ($(wc -c <"$LAB") bytes)..."
	cp "$LAB" /Volumes/INTERNAL/ttsystem.new && sync
	mv /Volumes/INTERNAL/ttsystem.new /Volumes/INTERNAL/ttsystem && sync
	shasum -a 256 /Volumes/INTERNAL/ttsystem
	echo "lab restored on INTERNAL — eject both LUNs when ready to boot lab"
else
	echo "WARNING: no lab backup at $LAB — left diagnostic ttsystem in place"
fi

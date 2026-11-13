#!/bin/sh
# Install artifacts/kernel-builds/homtom-6.1/ttsystem via bootloader USB storage mode.
# Reboot the GO 630 with the USB cable connected, wait until /Volumes/INTERNAL appears, then run this.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=${1:-$ROOT/artifacts/kernel-builds/homtom-6.1/ttsystem}
SHA=$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)
echo "waiting for /Volumes/INTERNAL ($SHA)..."
until [ -d /Volumes/INTERNAL ]; do sleep 2; done
DISK=$(diskutil info -plist /Volumes/INTERNAL | plutil -extract ParentWholeDisk raw -)
[ "$(diskutil info -plist "$DISK" | plutil -extract MediaName raw -)" = GO ] || {
	echo "INTERNAL is not a GO volume"; exit 1
}
mkdir -p "$ROOT/artifacts/backups"
[ -f "$ROOT/artifacts/backups/ttsystem-pre-homtom61.bin" ] || \
	cp -p /Volumes/INTERNAL/ttsystem "$ROOT/artifacts/backups/ttsystem-pre-homtom61.bin"
[ -f /Volumes/INTERNAL/ttsystem.opentom ] || cp /Volumes/INTERNAL/ttsystem /Volumes/INTERNAL/ttsystem.opentom
cp "$IMAGE" /Volumes/INTERNAL/ttsystem.new && sync
[ "$(shasum -a 256 /Volumes/INTERNAL/ttsystem.new | cut -d' ' -f1)" = "$SHA" ] || {
	echo "copy verification failed"; exit 1
}
mv /Volumes/INTERNAL/ttsystem.new /Volumes/INTERNAL/ttsystem && sync
ls -l /Volumes/INTERNAL/ttsystem /Volumes/INTERNAL/ttsystem.opentom
# Bootloader stays in storage until BOTH LUNs are gone (INTERNAL + SD "NO NAME").
diskutil unmountDisk force "$DISK" 2>/dev/null || diskutil eject "$DISK" || true
if [ -d "/Volumes/NO NAME" ]; then
	SD=$(diskutil info -plist "/Volumes/NO NAME" | plutil -extract ParentWholeDisk raw -)
	diskutil unmountDisk force "$SD" 2>/dev/null || diskutil eject "$SD" || true
fi
echo "ejected; device should boot the new ttsystem"

#!/bin/sh
# Run the REAL built initramfs /init.sh under ARM emulation (qemu-user via the arm32v5 container) in a chroot.
# Catches missing libs/applets/script errors without flashing. Does NOT test the kernel, USB or hardware.
# Usage: kernel/scripts/test-initramfs.sh [seconds]   (default 20)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CPIO=${CPIO:-$ROOT/artifacts/kernel-builds/homtom-6.1/initramfs-min.cpio.gz}
SECS=${1:-20}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/homtom-initramfs.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
gunzip -c "$CPIO" | (cd "$WORK" && cpio -id 2>/dev/null)
mkdir -p "$WORK/sys/class/net/usb0" "$WORK/dev" "$WORK/proc"
: > "$WORK/dev/kmsg"
/usr/local/bin/docker run --rm --platform linux/arm/v5 -v "$WORK:/r" arm32v5/debian:bookworm-slim sh -c "
timeout $SECS chroot /r /bin/busybox sh /init.sh 2>&1 | grep -v "busybox ln -sf" | grep -v "continue$" | head -250
echo '--- /dev/kmsg (what would reach the LCD console):'
cat /r/dev/kmsg
" 

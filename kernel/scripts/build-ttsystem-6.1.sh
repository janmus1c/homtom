#!/bin/sh
# Pack Linux 6.1 homtom zImage + minimal BusyBox initramfs into a TTBL ttsystem.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ZIMAGE=${ZIMAGE:-$ROOT/artifacts/kernel-builds/homtom-6.1/zImage}
OUT=${OUT:-$ROOT/artifacts/kernel-builds/homtom-6.1}
MKTT=$ROOT/tools/ttimage/mkttimage
[ -f "$ZIMAGE" ] || { echo "missing $ZIMAGE"; exit 1; }
[ -x "$MKTT" ] || { echo "missing $MKTT"; exit 1; }
mkdir -p "$OUT"

cp "$ROOT/tools/devtools/devmem" "$OUT/devmem"
cp "$ROOT/kernel/scripts/busybox-applets.txt" "$OUT/busybox-applets.txt"
cp "$ROOT/tools/devtools/devcp" "$OUT/devcp"
MODDIR=${MODDIR:-$ROOT/artifacts/modules-6.1}
[ -f "$MODDIR/homtom-sdi.ko" ] || { echo "missing $MODDIR/homtom-sdi.ko: run kernel/scripts/build-modules-tree.sh for the kernel tree of this zImage"; exit 1; }
cp "$MODDIR/homtom-sdi.ko" "$OUT/homtom-sdi.ko"
# Modules must come from the tree that built the zImage (no module versioning): compare the recorded config hashes.
if [ -f "$MODDIR/CONFIG_SHA" ] && [ -f "$(dirname "$ZIMAGE")/.config" ]; then
	[ "$(cat "$MODDIR/CONFIG_SHA")" = "$(shasum -a 256 "$(dirname "$ZIMAGE")/.config" | cut -d" " -f1)" ] || { echo "module config hash differs from the zImage .config"; exit 1; }
fi
cp "$ROOT"/userland/homtom-* "$OUT/"
# /etc/homtom-release: version, build date, git state and kernel version, readable on the device.
VERSION=$(cat "$ROOT/VERSION" 2>/dev/null || echo unknown)
EPOCH=${SOURCE_DATE_EPOCH:-$(date +%s)}
BUILT=$(date -u -r "$EPOCH" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -d "@$EPOCH" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)
GITREV=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo none)
git -C "$ROOT" diff --quiet HEAD 2>/dev/null || GITREV="$GITREV-dirty"
KVER=$(sed -n 's/^# Linux\/arm \([^ ]*\) Kernel Configuration.*/\1/p' "$(dirname "$ZIMAGE")/.config" 2>/dev/null | head -n 1)
printf 'version=%s\nbuilt=%s\ngit=%s\nkernel=%s\n' "$VERSION" "$BUILT" "$GITREV" "${KVER:-unknown}" > "$OUT/homtom-release"
for t in hcictl hciattach homtom-ts homtom-touch homtom-gui homtom-bl homtom-snd homtom-btd hcictl-info homtom-statusd homtom-snd2 homtom-gui-cfg gpsaid homtom-keyd; do cp "$ROOT/tools/devtools/$t" "$OUT/$t"; done
mkdir -p "$OUT/btmods"
for m in bluetooth hci_uart serdev ecc ecdh_generic libaes uinput rfcomm bnep cmac aes_generic; do cp "$MODDIR/$m.ko" "$OUT/btmods/"; done

# ARMv4T probe (/init): tiny static program, built with the kernel toolchain image.
/usr/local/bin/docker run --rm --platform linux/amd64 \
	-v "$ROOT/kernel/homtom:/src:ro" -v "$OUT:/out:rw" debian:bookworm-slim bash -c "
set -eu
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -qq -y gcc-arm-linux-gnueabi >/dev/null
arm-linux-gnueabi-gcc -march=armv4t -DHELLO_EXEC -nostdlib -static -o /out/hello /src/hello.S
ls -l /out/hello
"

# Build armel BusyBox rootfs with the arm32v5 Debian image already on this machine.
/usr/local/bin/docker run --rm --platform linux/arm/v5 -e BADINIT="${BADINIT:-0}" \
	-v "$OUT:/out:rw" -v "$ROOT/.runtime/emu/initramfs:/emu:ro" \
	arm32v5/debian:bookworm-slim bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y cpio gzip >/dev/null
ROOTFS=/tmp/rootfs
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS/bin" "$ROOTFS/proc" "$ROOTFS/sys" "$ROOTFS/dev" "$ROOTFS/tmp" "$ROOTFS/mnt"
# ARMv4T userland from the stock firmware (BusyBox 1.22.1 + glibc 2.3.2, no ARMv5 instructions); Debian armel is ARMv5TE (clz => SIGILL on ARM920T).
cp /emu/bin/busybox "$ROOTFS/bin/busybox"
chmod 755 "$ROOTFS/bin/busybox"
for f in $(cat /out/busybox-applets.txt); do ln -sf busybox "$ROOTFS/bin/$f"; done
cp /out/devmem "$ROOTFS/bin/devmem"
cp /out/devcp "$ROOTFS/bin/devcp"
chmod 755 "$ROOTFS/bin/devcp"
cp /out/homtom-hw /out/homtom-gpsd /out/homtom-status /out/homtom-status-legacy /out/homtom-shell /out/homtom-bt-up /out/homtom-bt-start /out/homtom-ui-start /out/homtom-keyd-start /out/homtom-poweroff /out/homtom-usbkick /out/homtom-netwatch /out/homtom-lock /out/homtom-bt-discoverable /out/homtom-bt-handshake /out/homtom-ui-extra /out/homtom-gps-aid /out/homtom-bt-visible /out/homtom-mic-test "$ROOTFS/bin/"
cp /out/hcictl /out/hciattach /out/homtom-ts /out/homtom-touch /out/homtom-gui /out/homtom-bl /out/homtom-snd /out/homtom-btd /out/hcictl-info /out/homtom-statusd /out/homtom-snd2 /out/homtom-gui-cfg /out/gpsaid /out/homtom-keyd "$ROOTFS/bin/"
chmod 755 "$ROOTFS"/bin/homtom-* "$ROOTFS"/bin/hcictl "$ROOTFS"/bin/hciattach
mkdir -p "$ROOTFS/etc"
cp /out/homtom-release "$ROOTFS/etc/homtom-release"
mkdir -p "$ROOTFS/lib/modules"
cp /out/homtom-sdi.ko "$ROOTFS/lib/modules/homtom-sdi.ko"
cp /out/btmods/*.ko "$ROOTFS/lib/modules/"
chmod 755 "$ROOTFS/bin/devmem"
mkdir -p "$ROOTFS/lib"
cp /emu/lib/*.so* "$ROOTFS/lib/"
chmod 755 "$ROOTFS"/lib/*.so*
cat > "$ROOTFS/init.sh" << "EOF"
#!/bin/busybox sh
__BADINIT__
[ -e /bin/ls ] || for f in $(/bin/busybox --list); do /bin/busybox ln -sf busybox /bin/$f; done
# reboot: TTBL boots straight on only if INFORM0=5 before SWRST (docs/LESSONS.md); the kernel restart path did not work, so do it from userspace.
rm -f /bin/reboot /bin/poweroff /bin/halt
cat > /bin/reboot <<W
#!/bin/busybox sh
sync
umount /mnt/internal 2>/dev/null
devmem 0x4C000070 5 > /dev/null
devmem 0x4C000044 0x533C2443 > /dev/null
sleep 3
touch /tmp/wdt-stop
sleep 30
W
chmod 755 /bin/reboot
ln -sf reboot /bin/poweroff
ln -sf reboot /bin/halt
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts; mount -t devpts devpts /dev/pts 2>/dev/null || true
mkdir -p /tmp /var/run /etc /mnt/internal /mnt/sdcard
rm -f /initrd.image
k() { echo "homtom-init: $*" > /dev/kmsg 2>/dev/null; }
# Quiet boot (docs/QUIET_BOOT.md): the kernel keeps the bootloader splash and mutes the LCD console.
T0=$(cut -d. -f1 /proc/uptime)
# unmute REASON: say why, then show the log on the LCD (the kernel redraws the newest lines, then prints live).
unmute() { echo on 2>/dev/null > /proc/sys/kernel/printk_devkmsg; k "$*"; echo 0 2>/dev/null > /sys/kernel/homtom_splash; }
# boot_fail REASON: banner and the tail of the GUI log on the LCD, full dmesg kept on INTERNAL when mounted.
boot_fail() {
	echo on 2>/dev/null > /proc/sys/kernel/printk_devkmsg
	k "==== boot incomplete: $* ===="
	if [ -f /tmp/gui.log ]; then
		n=$(wc -l < /tmp/gui.log); s=$((n - 9)); [ $s -lt 1 ] && s=1
		sed -n "$s,\$p" /tmp/gui.log | while read -r l; do k "gui: $l"; done
	fi
	{ echo "---- $(cat /proc/uptime) boot incomplete: $*"; dmesg; } > /tmp/boot-incomplete.log 2>&1
	[ -d /mnt/internal/homtom/logs ] && cp /tmp/boot-incomplete.log /mnt/internal/homtom/logs/boot-incomplete.log 2>/dev/null && sync
	echo 0 2>/dev/null > /sys/kernel/homtom_splash
}
# gui_up: the GUI wrote /tmp/gui.ready after its first frame, or (GUI without the marker) the same GUI process is still
# alive 10 s later.
gui_up() {
	[ -f /tmp/gui.ready ] && return 0
	p=$(pidof homtom-gui-cfg homtom-gui)
	[ -n "$p" ] || return 1
	sleep 10
	[ "$(pidof homtom-gui-cfg homtom-gui)" = "$p" ]
}
# Boot watchdog: 45 s after init started, but never sooner than 30 s after the UI launcher is scheduled (the network and
# INTERNAL waits before it can take up to ~40 s).
boot_watchdog() {
	ui0=$(cut -d. -f1 /proc/uptime)
	w=$((T0 + 45)); [ $((ui0 + 30)) -gt $w ] && w=$((ui0 + 30))
	now=$(cut -d. -f1 /proc/uptime); [ $w -gt $now ] && sleep $((w - now))
	if [ -f /mnt/internal/homtom/gui.disable ]; then
		unmute "GUI disabled (INTERNAL:/homtom/gui.disable)"
	elif ! gui_up; then
		boot_fail "GUI not running $((w - T0)) s after init start"
	fi
}
# Userspace is alive: this write stands the kernel 60 s guard down and draws nothing while muted. Skipped on a verbose
# kernel (homtom.verbose=1), where writing 1 would replace the boot text with the splash.
VERBOSE=0
case " $(cat /proc/cmdline) " in *" homtom.verbose=1 "*) VERBOSE=1 ;; esac
[ $VERBOSE = 1 ] || echo 1 2>/dev/null > /sys/kernel/homtom_splash
ifconfig lo 127.0.0.1 up 2>/dev/null
sv() { while :; do "$@" < /dev/null 2>&1; k "$1 exited rc=$?, restart"; sleep 5; done; }
# Safety net: TTBL treats a reset with INFORM0=5 as "Reboot" (then storage mode); a stopped feeder lets the watchdog reset a hung unit.
devmem 0x4C000070 5 > /dev/null 2>&1
[ -c /dev/watchdog ] || k "no /dev/watchdog"
# The feeder stops once the main loop stops writing /tmp/hb (about 4 min), so a wedged init still ends in a reset.
( exec 9> /dev/watchdog; last=; n=0; while :; do cur=$(cat /tmp/hb 2>/dev/null); if [ "$cur" = "$last" ]; then n=$((n+1)); else n=0; last=$cur; fi; [ $n -lt 80 ] && [ ! -f /tmp/wdt-stop ] && echo w >&9; sleep 3; done ) < /dev/null > /dev/null 2>&1 &
insmod /lib/modules/homtom-sdi.ko 2> /dev/null || true
k "started pid=$$"

# Network first: gadget usb0, DHCP for the Mac, telnet.
IF=
i=0
while [ $i -lt 30 ]; do
	[ -d /sys/class/net/usb0 ] && IF=usb0 && break
	for n in /sys/class/net/*; do
		case "$n" in *usb*|*rndis*) IF=${n##*/}; break 2 ;; esac
	done
	i=$((i+1))
	sleep 1
done
if [ -n "$IF" ]; then
	ifconfig $IF 192.168.1.10 netmask 255.255.255.0 up
	ifconfig $IF:1 169.254.10.10 netmask 255.255.0.0 up
	touch /var/run/udhcpd.leases
	echo "start 192.168.1.100" > /etc/udhcpd.conf
	echo "end 192.168.1.100" >> /etc/udhcpd.conf
	echo "interface $IF" >> /etc/udhcpd.conf
	echo "opt subnet 255.255.255.0" >> /etc/udhcpd.conf
	echo "lease_file /var/run/udhcpd.leases" >> /etc/udhcpd.conf
	udhcpd /etc/udhcpd.conf </dev/null >/dev/null 2>&1 &
	# Listeners (each in the background; an exit is reported to the console): telnet 23, raw shell 2323, FTP 21.
	( sv telnetd -F -l /bin/sh -p 23 ) &
	( sv tcpsvd 0.0.0.0 2323 /bin/homtom-shell ) &
	( sv tcpsvd 0.0.0.0 21 ftpd -w / ) &
	if [ -e /etc/homtom/no-statusd ]; then
		( sv homtom-gpsd > /dev/null 2>&1 ) &
		( sv tcpsvd 0.0.0.0 8080 /bin/homtom-status-legacy ) &
	else
		homtom-hw gps-on > /dev/null 2>&1
		( sv homtom-statusd 8080 > /dev/null 2>&1 ) &
	fi
	# First enumeration can leave the ECM notification stuck (the host does not poll the interrupt endpoint until the
	# netdev is open): drop and raise the D+ pull-up once (== replug the cable). NOT soft_connect (stops the UDC; does not recover).
	(
		sleep 6
		k "usb: pull-up off"
		echo 0 > /sys/kernel/homtom_usb_pullup
		sleep 3
		echo 1 > /sys/kernel/homtom_usb_pullup
		k "usb: pull-up on"
	) &
	NETMSG="net: $IF 192.168.1.10, telnet 23 / raw 2323 / ftp 21"
else
	NETMSG="net: no usb gadget iface"
fi
k "$NETMSG"

# Persist diagnostics on the FAT volumes (best effort, short wait).
LOGDIR=
i=0
while [ $i -lt 8 ]; do
	for node in /dev/mmcblk0 /dev/mmcblk0p1 /dev/mmcblk1p1 /dev/mmcblk1; do
		[ -b "$node" ] || continue
		for fstype in vfat msdos; do
			mount -t "$fstype" -o rw,sync "$node" /mnt/internal 2>/dev/null || continue
			LOGDIR=/mnt/internal
			break 3
		done
	done
	i=$((i+1))
	sleep 1
done
k "logdir=$LOGDIR blockdevs: $(ls /dev/mmcblk* 2>/dev/null)"
[ -n "$LOGDIR" ] || boot_fail "INTERNAL not mounted (no FAT on /dev/mmcblk*)"
if [ -n "$LOGDIR" ] && [ -f "$LOGDIR/homtom/verbose" ]; then VERBOSE=1; unmute "verbose boot (INTERNAL:/homtom/verbose)"; fi
{
	echo "---- $(cat /proc/uptime 2>/dev/null) homtom-6.1 initramfs up"
	echo "cmdline: $(cat /proc/cmdline 2>/dev/null)"
	echo "uname: $(uname -a 2>/dev/null)"
	echo "$NETMSG"
	echo "logdir=$LOGDIR"
	ls /sys/class/udc/ /sys/class/net/ 2>&1
	ifconfig -a 2>&1
} > /tmp/boot.log 2>&1
dmesg > /tmp/boot.dmesg 2>/dev/null
if [ -n "$LOGDIR" ]; then
	mkdir -p "$LOGDIR/homtom/logs" 2>/dev/null
	cp /tmp/boot.log "$LOGDIR/homtom-boot.log" 2>/dev/null
	cp /tmp/boot.dmesg "$LOGDIR/homtom-boot.dmesg" 2>/dev/null
	cp /tmp/boot.log "$LOGDIR/homtom/logs/boot.log" 2>/dev/null
	cp /tmp/boot.dmesg "$LOGDIR/homtom/logs/boot.dmesg" 2>/dev/null
	sync
fi

( sleep 12; homtom-bt-start ) > /dev/null 2>&1 < /dev/null &
[ -x /bin/homtom-gui-cfg ] || boot_fail "missing /bin/homtom-gui-cfg"
( sleep 15; homtom-ui-start ) > /dev/null 2>&1 < /dev/null &
( boot_watchdog ) > /dev/null 2>&1 < /dev/null &
( sleep 17; homtom-keyd-start ) > /dev/null 2>&1 < /dev/null &
( sleep 60; homtom-netwatch ) > /dev/null 2>&1 < /dev/null &
# Never exit: PID 1 dying panics the kernel (there is no console, so a shell would see EOF).
# Every 30 s print one heartbeat line to the LCD console via /dev/kmsg (the old per-5-s dmesg greps echoed their own output).
while :; do
	read up rest < /proc/uptime
	echo "$up" > /tmp/hb
	k "up=$up carrier=$(cat /sys/class/net/$IF/carrier 2>&1) udc=$(cat /sys/class/udc/*/state 2>&1) $IF rx=$(cat /sys/class/net/$IF/statistics/rx_packets 2>&1) tx=$(cat /sys/class/net/$IF/statistics/tx_packets 2>&1)"
	sleep 30
done
EOF
chmod 755 "$ROOTFS/init.sh"
if [ "${BADINIT:-0}" = 1 ]; then sed -i "s/^__BADINIT__\$/while :; do :; done/" "$ROOTFS/init.sh"; else sed -i "/^__BADINIT__\$/d" "$ROOTFS/init.sh"; fi
cp /out/hello "$ROOTFS/init"
chmod 755 "$ROOTFS/init"
mknod -m 600 "$ROOTFS/dev/console" c 5 1
mknod -m 666 "$ROOTFS/dev/null" c 1 3
mknod -m 600 "$ROOTFS/dev/kmsg" c 1 11
cd "$ROOTFS"
find . | cpio -o -H newc 2>/dev/null | gzip -n -9 > /out/initramfs-min.cpio.gz
ls -l /out/initramfs-min.cpio.gz
'

MAX=$((0x31700000 - 0x31000000))
SIZE=$(wc -c <"$OUT/initramfs-min.cpio.gz" | tr -d " ")
[ "$SIZE" -lt "$MAX" ] || { echo "initramfs too large: $SIZE >= $MAX"; exit 1; }

"$MKTT" "$OUT/initramfs-min.cpio.gz" "$ZIMAGE" > "$OUT/ttsystem"
python3 - <<PY
import sys
sys.path.insert(0, "$ROOT/firmware")
import ttimage
from pathlib import Path
img = Path("$OUT/ttsystem").read_bytes()
secs, entry = ttimage.sections(img)
print("sections:", [(hex(a), len(d)) for a, d in secs], "entry", hex(entry))
assert any(a == 0x31700000 for a, _ in secs)
assert any(a == 0x31000000 for a, _ in secs)
PY
(cd "$OUT" && shasum -a 256 ttsystem initramfs-min.cpio.gz zImage > SHA256SUMS.ttsystem && cat SHA256SUMS.ttsystem)
ls -lh "$OUT/ttsystem"
echo "built $OUT/ttsystem"

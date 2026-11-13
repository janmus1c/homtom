#!/bin/sh
# USB Ethernet for the S3C2443 high-speed controller, Telnet and FTP. A watcher reloads the
# gadget when no packet arrives for a minute (the first load after boot never passes traffic), which recovers failed enumerations and replugs.
G=/lib/modules/`uname -r`/kernel/drivers/usb/gadget
LOGDIR=/mnt/sdcard/homtom/logs
mkdir -p $LOGDIR
test -f $LOGDIR/dmesg.log && mv $LOGDIR/dmesg.log $LOGDIR/dmesg.previous.log
test -f $LOGDIR/network.log && mv $LOGDIR/network.log $LOGDIR/network.previous.log

log() {
	echo "`cat /proc/uptime` $*" >>$LOGDIR/network.log
}

ATTEMPT=0

usb_up() {
	ATTEMPT=$((ATTEMPT + 1))
	# v9 (v8 plus a bus disconnect on unload); every third retry falls back to v8.
	UDC=s3c-udc-hs.ko
	test $((ATTEMPT % 3)) = 0 && test -f $G/s3c-udc-hs-alt.ko && UDC=s3c-udc-hs-alt.ko
	if test -d /sys/bus/platform/devices/s3c-hsudc; then
		# The only working enumeration replaced the stock driver about 20 s after boot.
		rmmod s3c24xx_udc 2>/dev/null
		log "rmmod s3c24xx_udc $?"
		insmod $G/$UDC
		log "attempt $ATTEMPT insmod $UDC $?"
		insmod $G/g_ether.ko dev_addr=02:54:54:00:06:30 host_addr=02:54:54:00:06:31
		log "insmod g_ether $?"
	else
		insmod $G/s3c24xx_udc.ko
		insmod $G/g_ether.ko
	fi
	sleep 1
	ifconfig usb0 192.168.1.10 netmask 255.255.255.0 up
	ifconfig usb0:1 169.254.10.10 netmask 255.255.0.0 up
	mkdir -p /var/run && touch /var/run/udhcpd.leases
	udhcpd /etc/udhcpd.conf </dev/null >/dev/null 2>&1
}

usb_down() {
	killall udhcpd 2>/dev/null
	ifconfig usb0 down 2>/dev/null
	rmmod g_ether 2>/dev/null
	rmmod s3c_udc_hs 2>/dev/null
	rmmod s3c24xx_udc 2>/dev/null
	sleep 2
}

rx_packets() {
	set -- `sed -n 's/^ *usb0:\(.*\)/\1/p' /proc/net/dev`
	echo ${2:-0}
}

tx_packets() {
	set -- `sed -n 's/^ *usb0:\(.*\)/\1/p' /proc/net/dev`
	echo ${10:-0}
}

watch_usb() {
	last=-1
	last_tx=-1
	idle=0
	mute=0
	while true; do
		sleep 20
		now=`rx_packets`
		tx=`tx_packets`
		# The host talks but the device never answers: the IN path is dead, reload too.
		if test "$tx" = "$last_tx" && test "$now" != "$last"; then
			mute=$((mute + 20))
		else
			mute=0
		fi
		last_tx=$tx
		dmesg >$LOGDIR/dmesg.log
		log "rx=$now tx=$tx idle=$idle mute=$mute"
		sync
		if test "$now" = "$last"; then
			idle=$((idle + 20))
		else
			idle=0
		fi
		last=$now
		if test $idle -ge 20 || test $mute -ge 40; then
			echo "homtom: no USB traffic for ${idle}s, reloading gadget" >/dev/console
			usb_down
			usb_up
			last=-1
			last_tx=-1
			idle=0
			mute=0
		fi
	done
}

insmod $G/s3c24xx_udc.ko
( cd / ; /usr/sbin/telnetd -F & )
tcpsvd -E 0.0.0.0 21 /usr/sbin/ftpd -w / </dev/null >/dev/null 2>&1 &
( sleep 20; usb_up; watch_usb ) </dev/null >/dev/null 2>&1 &

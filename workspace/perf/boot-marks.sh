#!/bin/busybox sh
# Read-only boot timeline for the homtom image. Process start times come from /proc/PID/stat field 22
# (clock ticks at HZ=100 since the kernel clock started), so they work without printk timestamps.
# usage: tools/homtom-sh "$(cat workspace/perf/boot-marks.sh)" 30
mark() { set -- $(cat /proc/$1/stat 2>/dev/null) ; [ -n "$1" ] && echo "$(( ${22} / 100 )).$(( ${22} % 100 )) s  pid $1 $2"; }
echo "uptime: $(cat /proc/uptime)"
for p in $(pidof busybox telnetd tcpsvd udhcpd homtom-gpsd homtom-bt-start hciattach 2>/dev/null); do mark $p; done
echo "loadavg: $(cat /proc/loadavg)"
echo "idle seconds are the second field of the uptime line above"

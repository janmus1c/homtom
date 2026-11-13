#!/bin/sh
# Copy the bootloader areas to the boot disk once, for analysis on the host.
OUT=/mnt/sdcard/homtom/dump
test -f $OUT/done && exit 0
mkdir -p $OUT
rawcopy /dev/mmcblk0 $OUT/mmcblk0-head.bin 0 8388608 >$OUT/mmcblk0-head.log 2>&1
rawcopy /dev/mmcblk1 $OUT/mmcblk1-head.bin 0 4194304 >$OUT/mmcblk1-head.log 2>&1
rawcopy /dev/mtd0 $OUT/mtd0.bin 0 524288 >$OUT/mtd0.log 2>&1
cat /proc/partitions /proc/mtd /proc/cmdline >$OUT/layout.txt 2>&1
ls -l /sys/block/*/ >>$OUT/layout.txt 2>&1
sync
touch $OUT/done

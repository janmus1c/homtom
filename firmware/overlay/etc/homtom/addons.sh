#!/bin/sh
# Start every add-on in /mnt/sdcard/homtom/addons/<name>/run, each with its own log.
BASE=/mnt/sdcard/homtom
mkdir -p $BASE/addons $BASE/logs
for RUN in $BASE/addons/*/run; do
	test -f "$RUN" || continue
	NAME=`basename \`dirname $RUN\``
	test -f $BASE/addons/$NAME/disabled && continue
	( sh $RUN >$BASE/logs/$NAME.log 2>&1 & )
done

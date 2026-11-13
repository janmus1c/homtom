#!/bin/busybox sh
# Sample ADC mux 0-9 N times each via homtom-hw (reads only; touches ADC registers, no GPIO).
# Stop other ADC users first (status endpoint). Usage: adc-sweep-ro.sh [N]
n=${1:-5}
for ch in 0 1 2 3 4 5 6 7 8 9; do
	line="mux$ch:"
	i=0
	while [ $i -lt $n ]; do
		line="$line $(homtom-hw adc $ch)"
		i=$((i+1))
	done
	echo "$line"
done
echo "# 0 battery (div), 2 light (needs LX_EN), 4 ref (needs GPL14), 5/9 touch, 6 charge; 1,3 gyro; 7,8 unused in stock"

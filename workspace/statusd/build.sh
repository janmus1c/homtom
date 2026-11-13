#!/bin/sh
# usage: ./build.sh dev     cross-build out/homtom-statusd inside docker (debian:bookworm-slim + gcc-arm-linux-gnueabi);
#                           OUT=homtom-statusd.new ./build.sh dev writes out/homtom-statusd.new instead
#        ./build.sh test    host test of the NMEA parser and formatters (clang)
#        ./build.sh all     both
set -e
cd "$(dirname "$0")"
mkdir -p out
OUT=${OUT:-homtom-statusd}
dev() {
	/usr/local/bin/docker run --rm -v "$PWD":/src -w /src debian:bookworm-slim sh -c '
		apt-get update -qq >/dev/null && apt-get install -y -qq gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi >/dev/null &&
		arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 \
			-fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra \
			-o out/'"$OUT"' homtom-statusd.c &&
		arm-linux-gnueabi-strip out/'"$OUT"' &&
		arm-linux-gnueabi-size out/'"$OUT"' &&
		arm-linux-gnueabi-readelf -h out/'"$OUT"' | grep -E "Class|Machine|Flags" &&
		arm-linux-gnueabi-readelf -A out/'"$OUT"' | grep -E "CPU_arch|THUMB|ARM_ISA"'
	ls -l "out/$OUT"
}
test_host() {
	clang -O1 -Wall -Wextra -o out/test_core test_core.c
	./out/test_core
}
case "$1" in
dev) dev ;;
test) test_host ;;
all) test_host; dev ;;
*) echo "usage: $0 dev|test|all" >&2; exit 1 ;;
esac

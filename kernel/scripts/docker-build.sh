#!/bin/sh
# Cross-build homtom 6.1 kernel inside Debian (arm-linux-gnueabi-gcc).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TREE=${TREE:-$ROOT/.runtime/linux-6.1}
OUT=${OUT:-$ROOT/artifacts/kernel-builds/homtom-6.1}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 2)}

"$ROOT/kernel/scripts/apply-homtom.sh" "$TREE"
mkdir -p "$OUT"

/usr/local/bin/docker run --rm --platform linux/amd64 \
	-v "$TREE:/linux:rw" \
	-v "$OUT:/out:rw" \
	-w /linux debian:bookworm-slim \
	bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y build-essential bc bison flex libssl-dev \
	gcc-arm-linux-gnueabi >/dev/null
rm -f .config
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- homtom_defconfig
grep -q '^CONFIG_ARCH_S3C24XX=y' .config
grep -q '^CONFIG_MACH_TOMTOMGO=y' .config
grep -q '^CONFIG_CPU_S3C2443=y' .config
grep -q '^CONFIG_UNUSED_BOARD_FILES=y' .config
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j'"$JOBS"' zImage
grep -q homtom_map_io System.map
grep -q s3c24xx_init_io System.map
cp -a arch/arm/boot/zImage System.map .config /out/
echo DONE
'

(cd "$OUT" && shasum -a 256 zImage System.map .config > SHA256SUMS && cat SHA256SUMS)
# Fail the host script if the image lacks board symbols (guards stale/broken configs).
grep -q homtom_map_io "$OUT/System.map"
grep -q __arm920_proc_info "$OUT/System.map"
echo "artifacts in $OUT"

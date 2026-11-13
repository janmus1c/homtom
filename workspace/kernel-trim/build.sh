#!/bin/sh
# Same docker command as kernel/scripts/docker-build.sh, TREE=.runtime/linux-trim, no apply-homtom (copy already holds the homtom files plus the trimmed defconfig).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TREE=$ROOT/.runtime/linux-trim
OUT=$ROOT/workspace/kernel-trim/out
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 2)}
mkdir -p "$OUT"
/usr/local/bin/docker run --rm --platform linux/amd64 \
	-v "$TREE:/linux:rw" -v "$OUT:/out:rw" -w /linux debian:bookworm-slim \
	bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y build-essential bc bison flex libssl-dev gcc-arm-linux-gnueabi >/dev/null
rm -f .config
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- homtom_defconfig
grep -q "^CONFIG_ARCH_S3C24XX=y" .config
grep -q "^CONFIG_MACH_TOMTOMGO=y" .config
grep -q "^CONFIG_CPU_S3C2443=y" .config
grep -q "^CONFIG_UNUSED_BOARD_FILES=y" .config
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j'"$JOBS"' zImage 2>&1 | tee /out/build.log | grep -E "warning|error|Error" || true
grep -q homtom_map_io System.map
grep -q s3c24xx_init_io System.map
cp -a arch/arm/boot/zImage System.map .config /out/
echo DONE
'

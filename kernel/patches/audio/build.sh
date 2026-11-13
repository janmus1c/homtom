#!/bin/sh
# Build homtom-audio.ko against .runtime/linux-trim-audio, an APFS clone of the shipped quiet tree (same .config, so the
# module matches the running zImage). The quiet tree itself is never written. Usage: kernel/patches/audio/build.sh [OUTDIR]
# Output: OUTDIR (default artifacts/audio-dma)/homtom-audio.ko, CONFIG_SHA, SHA256SUMS.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
QUIET=$ROOT/.runtime/linux-trim-quiet
TREE=$ROOT/.runtime/linux-trim-audio
SRC=$ROOT/kernel/patches/audio/homtom-audio
WORK=$ROOT/.runtime/audio-mod-build
OUT=${1:-$ROOT/artifacts/audio-dma}
DOCKER=${DOCKER:-$(command -v docker || echo /usr/local/bin/docker)}

[ -d "$TREE" ] || cp -Rc "$QUIET" "$TREE"
cmp -s "$QUIET/.config" "$TREE/.config" || { echo "linux-trim-audio/.config differs from the quiet tree"; exit 1; }
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"
cp "$SRC/homtom-audio.c" "$SRC/Makefile" "$WORK/"
OUT=$(cd "$OUT" && pwd)

"$DOCKER" run --rm --platform linux/amd64 -v "$TREE:/linux:rw" -v "$WORK:/mod:rw" -v "$OUT:/out:rw" -w /linux \
	debian:bookworm-slim bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y build-essential bc bison flex libssl-dev gcc-arm-linux-gnueabi >/dev/null
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- modules_prepare >/dev/null
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- M=/mod W=1 modules 2>&1 | tee /mod/build.log
grep -E "error|warning: " /mod/build.log | grep -v "Module.symvers is missing" && exit 1 || true
cp /mod/homtom-audio.ko /out/
arm-linux-gnueabi-strip --strip-debug /out/homtom-audio.ko
sha256sum .config | cut -d" " -f1 > /out/CONFIG_SHA
'
(cd "$OUT" && shasum -a 256 homtom-audio.ko CONFIG_SHA > SHA256SUMS && cat SHA256SUMS && ls -l homtom-audio.ko)

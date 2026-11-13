#!/bin/sh
# Rebuild every loadable module the image ships against ONE kernel tree (module layouts depend on the exact .config, so
# modules from another config can fail to load or misbehave). Usage: build-modules-tree.sh TREE OUTDIR
# in-tree: bluetooth core/rfcomm/bnep, hci_uart, serdev, uinput, crypto helpers; out-of-tree: kernel/modules/homtom-sdi.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TREE=$(cd "$1" && pwd)
OUT=$2; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
/usr/local/bin/docker run --rm --platform linux/amd64 \
	-v "$TREE:/linux:rw" -v "$ROOT/kernel/modules/homtom-sdi:/sdi:rw" -v "$OUT:/out:rw" -w /linux debian:bookworm-slim bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y build-essential bc bison flex libssl-dev gcc-arm-linux-gnueabi >/dev/null
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- modules_prepare >/dev/null
for d in net/bluetooth drivers/bluetooth drivers/tty/serdev drivers/input/misc lib/crypto crypto; do
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- M=$d modules 2>&1 | grep -E "error|Error" && exit 1 || true
done
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- M=/sdi modules 2>&1 | grep -E "error|Error" && exit 1 || true
for m in bluetooth/bluetooth bluetooth/rfcomm/rfcomm bluetooth/bnep/bnep drivers/bluetooth/hci_uart drivers/tty/serdev/serdev \
	drivers/input/misc/uinput lib/crypto/libaes crypto/aes_generic crypto/cmac crypto/ecc crypto/ecdh_generic; do
	f=$(echo $m | sed "s|^bluetooth/|net/bluetooth/|"); cp "$f.ko" /out/ || { echo "MISSING $f"; exit 1; }
done
cp /sdi/homtom-sdi.ko /out/
sha256sum .config | cut -d" " -f1 > /out/CONFIG_SHA
strip --strip-debug /out/*.ko 2>/dev/null || true
ls -l /out
'

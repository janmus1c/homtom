#!/bin/sh
# Build an out-of-tree module against the already built .runtime/linux-6.1. Usage: build-module.sh kernel/modules/NAME
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MOD=$(cd "$ROOT/$1" && pwd)
/usr/local/bin/docker run --rm --platform linux/amd64 \
	-v "$ROOT/.runtime/linux-6.1:/linux:rw" -v "$MOD:/mod:rw" -w /linux debian:bookworm-slim bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -qq -y build-essential bc bison flex libssl-dev gcc-arm-linux-gnueabi >/dev/null
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- modules_prepare >/dev/null
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- M=/mod modules
'
ls -l "$MOD"/*.ko

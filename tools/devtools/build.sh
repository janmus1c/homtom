#!/bin/sh
# Build every freestanding ARMv4T device tool in this directory (raw syscalls, no libc, no libgcc) in a Debian container.
# Usage: tools/devtools/build.sh [tool ...]   (default: all .c files)   Needs docker.
# SUFFIX=.new writes each binary as <tool>.new and leaves the committed one alone.
set -eu
cd "$(dirname "$0")"
DOCKER=${DOCKER:-$(command -v docker || echo /usr/local/bin/docker)}
SUFFIX=${SUFFIX:-}
TOOLS=${*:-$(ls *.c | sed 's/\.c$//' | tr '\n' ' ')}
"$DOCKER" run --rm --platform linux/amd64 -v "$PWD:/w" debian:bookworm-slim bash -c '
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -qq -y gcc-arm-linux-gnueabi >/dev/null
cd /w
for t in '"$TOOLS"'; do
	arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 -fno-tree-loop-distribute-patterns -o $t'"$SUFFIX"' $t.c
	echo "built $t'"$SUFFIX"'"
done'

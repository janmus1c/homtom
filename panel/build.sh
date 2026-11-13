#!/bin/sh
# Builds the static ARM panel with the TomTom gcc 3.3.4 toolchain: panel/build/homtom-panel.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TC=$ROOT/artifacts/source/toolchain/gcc-3.3.4_glibc-2.3.2
mkdir -p "$ROOT/panel/build"
docker run --rm --platform linux/386 --network none -v "$TC:/opt/cross:ro" -v "$ROOT/panel:/work" \
  tomtom-revival-build:gcc334-host sh -c 'cd /work &&
  /opt/cross/bin/arm-linux-gcc -std=gnu89 -Os -Wall -W -Wno-unused-parameter -static \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -o build/homtom-panel \
    src/main.c src/ui.c src/gfx.c src/proto.c src/layout.c src/http.c src/assets.c &&
  /opt/cross/bin/arm-linux-strip build/homtom-panel'
ls -l "$ROOT/panel/build/homtom-panel"

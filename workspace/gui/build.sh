#!/bin/sh
# usage: ./build.sh dev      cross-build out/homtom-gui inside docker (debian:bookworm-slim)
#        ./build.sh dev-cfg  cross-build out/homtom-gui-cfg (configurable UI, cfg.c + hw_cfg.c) the same way; -z norelro:
#                            a static binary has no loader to apply RELRO, and its page alignment padded the file by up to 4 KB.
#                            -fno-pie: the binary is linked at a fixed address anyway, and Debian's default PIE code generation
#                            costs about 3 KB of GOT-relative addressing and moves pointer tables into a writable segment;
#                            -ffunction-sections + --gc-sections drop unused functions. Still -O1, still no libgcc
#        ./build.sh preview  build out/preview with clang and write out/{home,gps,system}.png and out/cfg/<conf>-<n>-<page>.png
#        ./build.sh test     host regression tests (tests/run.sh)
#        ./build.sh assets   regenerate font_data.h, wordmark_data.h, icon_data.h from the pinned sources (gen_assets.sh)
set -e
cd "$(dirname "$0")"
mkdir -p out
case "$1" in
dev)
	/usr/local/bin/docker run --rm -v "$PWD":/src -w /src debian:bookworm-slim sh -c '
		apt-get update -qq >/dev/null && apt-get install -y -qq gcc-arm-linux-gnueabi >/dev/null &&
		arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 \
			-fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra \
			-o out/homtom-gui hw_dev.c ui.c gui.c &&
		arm-linux-gnueabi-strip out/homtom-gui &&
		arm-linux-gnueabi-size out/homtom-gui'
	ls -l out/homtom-gui
	;;
dev-cfg)
	/usr/local/bin/docker run --rm -v "$PWD":/src -w /src debian:bookworm-slim sh -c '
		apt-get update -qq >/dev/null && apt-get install -y -qq gcc-arm-linux-gnueabi >/dev/null &&
		arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 \
			-fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Wl,-z,norelro \
			-fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--build-id=none \
			-o out/homtom-gui-cfg hw_cfg.c cfg.c ui.c gui.c &&
		arm-linux-gnueabi-strip out/homtom-gui-cfg &&
		arm-linux-gnueabi-size out/homtom-gui-cfg'
	ls -l out/homtom-gui-cfg
	;;
test)
	exec ./tests/run.sh
	;;
assets)
	exec ./gen_assets.sh
	;;
preview)
	clang -O1 -Wall -Wextra -Wno-unused-parameter -o out/preview host.c ui.c gui.c cfg.c
	./out/preview -f sample.txt -p 0 -o out/home.png
	./out/preview -f sample.txt -p 1 -o out/gps.png
	./out/preview -f sample.txt -p 2 -o out/system.png
	mkdir -p out/cfg
	for c in conf/*.conf; do
		./out/preview -c "$c" -f sample.txt -e tests/ha.states.sample -A 3 -W 1794058320 -a "out/cfg/$(basename "$c" .conf)" > /dev/null
	done
	ls -l out/*.png
	;;
*)
	echo "usage: $0 dev|dev-cfg|preview|test|assets" >&2
	exit 1
	;;
esac

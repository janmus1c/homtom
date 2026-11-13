#!/bin/sh
# Append staged or built images to artifacts/kernel-builds/MANIFEST.sha256, one line per file:
#   <name> <size-bytes> <sha256> <UTC date>
# <name> is the path relative to artifacts/kernel-builds (no spaces). A line with the same name and sha256 is not added twice.
# Usage: scripts/manifest-add.sh FILE [FILE ...]     add (run after each tools/homtom-stage that printed "staged ...")
#        scripts/manifest-add.sh --verify            recompute every listed file that still exists, report mismatches
# Mac-only, never touches the device. Exit 1 on a missing file or a verify mismatch.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KB=$ROOT/artifacts/kernel-builds
M=$KB/MANIFEST.sha256
mkdir -p "$KB"
touch "$M"

if [ "${1:-}" = --verify ]; then
	bad=0
	while read -r name size sha date; do
		[ -n "$name" ] || continue
		[ -f "$KB/$name" ] || { echo "missing  $name"; continue; }
		now=$(shasum -a 256 "$KB/$name" | cut -d' ' -f1)
		if [ "$now" = "$sha" ]; then echo "ok       $name"; else echo "MISMATCH $name (manifest ${sha%${sha#????????????????}}, now ${now%${now#????????????????}})"; bad=1; fi
	done < "$M"
	exit $bad
fi

[ $# -gt 0 ] || { echo "usage: manifest-add.sh FILE [FILE ...] | --verify" >&2; exit 2; }
rc=0
for f in "$@"; do
	[ -f "$f" ] || { echo "missing $f" >&2; rc=1; continue; }
	abs=$(cd "$(dirname "$f")" && pwd)/$(basename "$f")
	case "$abs" in "$KB"/*) name=${abs#"$KB"/} ;; *) name=$(basename "$abs") ;; esac
	case "$name" in *" "*) echo "name with spaces: $name" >&2; rc=1; continue ;; esac
	sha=$(shasum -a 256 "$abs" | cut -d' ' -f1)
	size=$(wc -c < "$abs" | tr -d ' ')
	if grep -q -e "^$name [0-9]* $sha " "$M"; then echo "already listed: $name ${sha%${sha#????????????????}}"; continue; fi
	echo "$name $size $sha $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$M"
	echo "added $name $size ${sha%${sha#????????????????}}"
done
exit $rc
